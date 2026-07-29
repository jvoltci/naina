// N-API binding for naina. Wraps naina::Engine with async read and
// detectText methods (run on a worker thread so Node's event loop isn't
// blocked during inference).

#include "naina/naina.hpp"

#include <napi.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ── Buffer → naina::Image ───────────────────────────────────────────
// Caller passes { data: Buffer | Uint8Array, width, height, channels? }.
// We don't copy the pixels — keep the JS-side TypedArray alive for the
// duration of the call (the worker holds a reference).
struct ImageRef {
    Napi::Reference<Napi::Object> handle_ref;  // keeps the data alive
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 3;
    naina::PixFmt fmt = naina::PixFmt::RGB8;

    naina::Image to_image() const {
        const int stride = width * channels;
        return naina::Image(data, width, height, stride, fmt);
    }
};

ImageRef parse_image_arg(const Napi::Object& obj) {
    ImageRef ref;
    if (!obj.Has("data") || !obj.Has("width") || !obj.Has("height")) {
        throw std::invalid_argument("image must be { data, width, height, channels? }");
    }
    Napi::Value data_val = obj.Get("data");
    if (!data_val.IsTypedArray() && !data_val.IsBuffer()) {
        throw std::invalid_argument("image.data must be a Buffer or Uint8Array");
    }

    if (data_val.IsBuffer()) {
        auto buf = data_val.As<Napi::Buffer<uint8_t>>();
        ref.data = buf.Data();
    } else {
        auto ta = data_val.As<Napi::Uint8Array>();
        ref.data = ta.Data();
    }
    // Anchor the JS object so its underlying ArrayBuffer isn't collected
    // while a worker is using it.
    ref.handle_ref = Napi::Reference<Napi::Object>::New(obj, 1);

    ref.width = obj.Get("width").As<Napi::Number>().Int32Value();
    ref.height = obj.Get("height").As<Napi::Number>().Int32Value();
    if (obj.Has("channels") && !obj.Get("channels").IsUndefined()) {
        ref.channels = obj.Get("channels").As<Napi::Number>().Int32Value();
    }
    if (obj.Has("format") && !obj.Get("format").IsUndefined()) {
        const std::string fs = obj.Get("format").As<Napi::String>();
        if (fs == "rgb")
            ref.fmt = naina::PixFmt::RGB8;
        else if (fs == "bgr")
            ref.fmt = naina::PixFmt::BGR8;
        else if (fs == "gray")
            ref.fmt = naina::PixFmt::Gray8;
    } else {
        ref.fmt = (ref.channels == 1) ? naina::PixFmt::Gray8 : naina::PixFmt::RGB8;
    }
    return ref;
}

// ── naina types → JS objects ─────────────────────────────────────────

Napi::Object point_to_object(Napi::Env env, const naina::Point& p) {
    Napi::Object o = Napi::Object::New(env);
    o.Set("x", p.x);
    o.Set("y", p.y);
    return o;
}

Napi::Array quad_to_array(Napi::Env env, const std::array<naina::Point, 4>& quad) {
    Napi::Array arr = Napi::Array::New(env, 4);
    for (uint32_t i = 0; i < 4; ++i) {
        arr.Set(i, point_to_object(env, quad[i]));
    }
    return arr;
}

Napi::Object line_to_object(Napi::Env env, const naina::Line& l) {
    Napi::Object o = Napi::Object::New(env);
    o.Set("text", Napi::String::New(env, l.text));
    o.Set("confidence", l.confidence);
    o.Set("score", l.score);
    o.Set("quad", quad_to_array(env, l.quad));
    return o;
}

Napi::Object page_to_object(Napi::Env env, const naina::Page& page) {
    Napi::Object o = Napi::Object::New(env);
    o.Set("markdown", Napi::String::New(env, page.markdown()));
    o.Set("json", Napi::String::New(env, page.json()));
    const auto lines = page.lines();
    Napi::Array arr = Napi::Array::New(env, lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        arr.Set(static_cast<uint32_t>(i), line_to_object(env, lines[i]));
    }
    o.Set("lines", arr);
    return o;
}

// ── Engine wrapper class ────────────────────────────────────────────

class Engine : public Napi::ObjectWrap<Engine> {
public:
    static Napi::Function init(Napi::Env env);

    explicit Engine(const Napi::CallbackInfo& info);

private:
    Napi::Value read(const Napi::CallbackInfo& info);
    Napi::Value detect_text(const Napi::CallbackInfo& info);

    std::shared_ptr<naina::Engine> engine_;
};

class ReadWorker : public Napi::AsyncWorker {
public:
    ReadWorker(Napi::Env env,
               std::shared_ptr<naina::Engine> engine,
               ImageRef image,
               Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env)
        , engine_(std::move(engine))
        , image_(std::move(image))
        , deferred_(std::move(deferred)) {}

    void Execute() override {
        try {
            page_.emplace(engine_->read(image_.to_image()));
        } catch (const std::exception& e) {
            SetError(e.what());
        }
    }
    void OnOK() override {
        Napi::HandleScope scope(Env());
        deferred_.Resolve(page_to_object(Env(), *page_));
    }
    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

private:
    std::shared_ptr<naina::Engine> engine_;
    ImageRef image_;
    Napi::Promise::Deferred deferred_;
    std::optional<naina::Page> page_;
};

class DetectTextWorker : public Napi::AsyncWorker {
public:
    DetectTextWorker(Napi::Env env,
                      std::shared_ptr<naina::Engine> engine,
                      ImageRef image,
                      Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env)
        , engine_(std::move(engine))
        , image_(std::move(image))
        , deferred_(std::move(deferred)) {}

    void Execute() override {
        try {
            quads_ = engine_->detect_text(image_.to_image());
        } catch (const std::exception& e) {
            SetError(e.what());
        }
    }
    void OnOK() override {
        Napi::HandleScope scope(Env());
        Napi::Array arr = Napi::Array::New(Env(), quads_.size());
        for (size_t i = 0; i < quads_.size(); ++i) {
            arr.Set(static_cast<uint32_t>(i), quad_to_array(Env(), quads_[i]));
        }
        deferred_.Resolve(arr);
    }
    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

private:
    std::shared_ptr<naina::Engine> engine_;
    ImageRef image_;
    Napi::Promise::Deferred deferred_;
    std::vector<std::array<naina::Point, 4>> quads_;
};

Napi::Function Engine::init(Napi::Env env) {
    return DefineClass(env,
                       "Engine",
                       {
                           InstanceMethod("read", &Engine::read),
                           InstanceMethod("detectText", &Engine::detect_text),
                       });
}

Engine::Engine(const Napi::CallbackInfo& info) : Napi::ObjectWrap<Engine>(info) {
    Napi::Env env = info.Env();
    naina::Config cfg;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto opts = info[0].As<Napi::Object>();
        if (opts.Has("backend") && opts.Get("backend").IsString()) {
            const std::string s = opts.Get("backend").As<Napi::String>();
            if (s == "auto")
                cfg.backend = naina::Backend::Auto;
            else if (s == "onnxruntime")
                cfg.backend = naina::Backend::ONNXRuntime;
            else if (s == "openvino")
                cfg.backend = naina::Backend::OpenVINO;
            else if (s == "ncnn")
                cfg.backend = naina::Backend::NCNN;
            else if (s == "coreml")
                cfg.backend = naina::Backend::CoreML;
            else if (s == "tensorrt")
                cfg.backend = naina::Backend::TensorRT;
        }
        // Recognition alphabet. Absent or "" is Latin + CJK; "devanagari"
        // reads Hindi, Marathi, Nepali and Sanskrit. An unknown value throws
        // rather than reading with the wrong alphabet.
        if (opts.Has("language") && opts.Get("language").IsString()) {
            cfg.language = opts.Get("language").As<Napi::String>();
        }
        if (opts.Has("tier") && opts.Get("tier").IsString()) {
            const std::string s = opts.Get("tier").As<Napi::String>();
            if (s == "auto")
                cfg.tier = naina::Tier::Auto;
            else if (s == "tiny")
                cfg.tier = naina::Tier::Tiny;
            else if (s == "small")
                cfg.tier = naina::Tier::Small;
            else if (s == "medium")
                cfg.tier = naina::Tier::Medium;
        }
        if (opts.Has("modelsRoot") && opts.Get("modelsRoot").IsString()) {
            cfg.models_root = opts.Get("modelsRoot").As<Napi::String>().Utf8Value();
        }
        if (opts.Has("numThreads") && opts.Get("numThreads").IsNumber()) {
            cfg.num_threads = opts.Get("numThreads").As<Napi::Number>().Int32Value();
        }
    }
    try {
        engine_ = std::make_shared<naina::Engine>(cfg);
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    }
}

Napi::Value Engine::read(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto deferred = Napi::Promise::Deferred::New(env);
    try {
        ImageRef ref = parse_image_arg(info[0].As<Napi::Object>());
        auto* w = new ReadWorker(env, engine_, std::move(ref), deferred);
        w->Queue();
    } catch (const std::exception& e) {
        deferred.Reject(Napi::Error::New(env, e.what()).Value());
    }
    return deferred.Promise();
}

Napi::Value Engine::detect_text(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto deferred = Napi::Promise::Deferred::New(env);
    try {
        ImageRef ref = parse_image_arg(info[0].As<Napi::Object>());
        auto* w = new DetectTextWorker(env, engine_, std::move(ref), deferred);
        w->Queue();
    } catch (const std::exception& e) {
        deferred.Reject(Napi::Error::New(env, e.what()).Value());
    }
    return deferred.Promise();
}

// ── Module init ────────────────────────────────────────────────────

Napi::Object init(Napi::Env env, Napi::Object exports) {
    exports.Set("Engine", Engine::init(env));
    exports.Set("version", Napi::String::New(env, naina_version_string()));
    return exports;
}

NODE_API_MODULE(naina, init)

}  // namespace
