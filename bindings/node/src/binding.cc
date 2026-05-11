// N-API binding for naina. Wraps naina::Engine with async detectFaces and
// embedFace methods (run on a worker thread so Node's event loop isn't
// blocked during inference).

#include "naina/naina.hpp"

#include <napi.h>

#include <cstdint>
#include <memory>
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

Napi::Object face_to_object(Napi::Env env, const naina::Face& f) {
    Napi::Object o = Napi::Object::New(env);

    Napi::Object bbox = Napi::Object::New(env);
    bbox.Set("x", f.bbox.x);
    bbox.Set("y", f.bbox.y);
    bbox.Set("w", f.bbox.w);
    bbox.Set("h", f.bbox.h);
    bbox.Set("score", f.bbox.score);
    o.Set("bbox", bbox);

    Napi::Array lm = Napi::Array::New(env, 5);
    for (uint32_t i = 0; i < 5; ++i) {
        Napi::Object p = Napi::Object::New(env);
        p.Set("x", f.landmarks[i].x);
        p.Set("y", f.landmarks[i].y);
        lm.Set(i, p);
    }
    o.Set("landmarks", lm);
    o.Set("quality", f.quality);
    o.Set("trackId", f.track_id);
    return o;
}

naina::Face object_to_face(const Napi::Object& obj) {
    naina::Face f{};
    auto bbox = obj.Get("bbox").As<Napi::Object>();
    f.bbox.x = bbox.Get("x").As<Napi::Number>().FloatValue();
    f.bbox.y = bbox.Get("y").As<Napi::Number>().FloatValue();
    f.bbox.w = bbox.Get("w").As<Napi::Number>().FloatValue();
    f.bbox.h = bbox.Get("h").As<Napi::Number>().FloatValue();
    f.bbox.score = bbox.Get("score").As<Napi::Number>().FloatValue();
    auto lm = obj.Get("landmarks").As<Napi::Array>();
    for (uint32_t i = 0; i < 5; ++i) {
        auto p = lm.Get(i).As<Napi::Object>();
        f.landmarks[i] = {p.Get("x").As<Napi::Number>().FloatValue(),
                          p.Get("y").As<Napi::Number>().FloatValue()};
    }
    f.quality = obj.Has("quality") ? obj.Get("quality").As<Napi::Number>().FloatValue() : 0.0F;
    f.track_id = obj.Has("trackId") ? obj.Get("trackId").As<Napi::Number>().Int32Value() : -1;
    return f;
}

// ── Engine wrapper class ────────────────────────────────────────────

class Engine : public Napi::ObjectWrap<Engine> {
public:
    static Napi::Function init(Napi::Env env);

    explicit Engine(const Napi::CallbackInfo& info);

private:
    Napi::Value detect_faces(const Napi::CallbackInfo& info);
    Napi::Value embed_face(const Napi::CallbackInfo& info);
    Napi::Value face_liveness(const Napi::CallbackInfo& info);
    Napi::Value face_embed_dim(const Napi::CallbackInfo& info);

    std::shared_ptr<naina::Engine> engine_;
};

class DetectWorker : public Napi::AsyncWorker {
public:
    DetectWorker(Napi::Env env,
                 std::shared_ptr<naina::Engine> engine,
                 ImageRef image,
                 Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env)
        , engine_(std::move(engine))
        , image_(std::move(image))
        , deferred_(std::move(deferred)) {}

    void Execute() override {
        try {
            faces_ = engine_->detect_faces(image_.to_image());
        } catch (const std::exception& e) {
            SetError(e.what());
        }
    }
    void OnOK() override {
        Napi::HandleScope scope(Env());
        Napi::Array arr = Napi::Array::New(Env(), faces_.size());
        for (size_t i = 0; i < faces_.size(); ++i) {
            arr.Set(static_cast<uint32_t>(i), face_to_object(Env(), faces_[i]));
        }
        deferred_.Resolve(arr);
    }
    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

private:
    std::shared_ptr<naina::Engine> engine_;
    ImageRef image_;
    Napi::Promise::Deferred deferred_;
    std::vector<naina::Face> faces_;
};

class LivenessWorker : public Napi::AsyncWorker {
public:
    LivenessWorker(Napi::Env env,
                   std::shared_ptr<naina::Engine> engine,
                   ImageRef image,
                   naina::Face face,
                   Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env)
        , engine_(std::move(engine))
        , image_(std::move(image))
        , face_(face)
        , deferred_(std::move(deferred)) {}

    void Execute() override {
        try {
            score_ = engine_->face_liveness(image_.to_image(), face_);
        } catch (const std::exception& e) {
            SetError(e.what());
        }
    }
    void OnOK() override {
        Napi::HandleScope scope(Env());
        deferred_.Resolve(Napi::Number::New(Env(), score_));
    }
    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

private:
    std::shared_ptr<naina::Engine> engine_;
    ImageRef image_;
    naina::Face face_;
    Napi::Promise::Deferred deferred_;
    float score_ = 0.0F;
};

class EmbedWorker : public Napi::AsyncWorker {
public:
    EmbedWorker(Napi::Env env,
                std::shared_ptr<naina::Engine> engine,
                ImageRef image,
                naina::Face face,
                Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(env)
        , engine_(std::move(engine))
        , image_(std::move(image))
        , face_(face)
        , deferred_(std::move(deferred)) {}

    void Execute() override {
        try {
            embedding_ = engine_->embed_face(image_.to_image(), face_);
        } catch (const std::exception& e) {
            SetError(e.what());
        }
    }
    void OnOK() override {
        Napi::HandleScope scope(Env());
        const size_t n = embedding_.size();
        Napi::Float32Array out = Napi::Float32Array::New(Env(), n);
        std::memcpy(out.Data(), embedding_.data(), n * sizeof(float));
        deferred_.Resolve(out);
    }
    void OnError(const Napi::Error& e) override { deferred_.Reject(e.Value()); }

private:
    std::shared_ptr<naina::Engine> engine_;
    ImageRef image_;
    naina::Face face_;
    Napi::Promise::Deferred deferred_;
    std::vector<float> embedding_;
};

Napi::Function Engine::init(Napi::Env env) {
    return DefineClass(env,
                       "Engine",
                       {
                           InstanceMethod("detectFaces", &Engine::detect_faces),
                           InstanceMethod("embedFace", &Engine::embed_face),
                           InstanceMethod("faceLiveness", &Engine::face_liveness),
                           InstanceMethod("faceEmbedDim", &Engine::face_embed_dim),
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
            else if (s == "ncnn")
                cfg.backend = naina::Backend::NCNN;
            else if (s == "coreml")
                cfg.backend = naina::Backend::CoreML;
            else if (s == "tensorrt")
                cfg.backend = naina::Backend::TensorRT;
        }
        if (opts.Has("modelsRoot") && opts.Get("modelsRoot").IsString()) {
            cfg.models_root = opts.Get("modelsRoot").As<Napi::String>().Utf8Value();
        }
        if (opts.Has("numThreads") && opts.Get("numThreads").IsNumber()) {
            cfg.num_threads = opts.Get("numThreads").As<Napi::Number>().Int32Value();
        }
        if (opts.Has("enableResearchModels") && opts.Get("enableResearchModels").IsBoolean()) {
            cfg.enable_research_models =
                opts.Get("enableResearchModels").As<Napi::Boolean>().Value();
        }
    }
    try {
        engine_ = std::make_shared<naina::Engine>(cfg);
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    }
}

Napi::Value Engine::detect_faces(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto deferred = Napi::Promise::Deferred::New(env);
    try {
        ImageRef ref = parse_image_arg(info[0].As<Napi::Object>());
        auto* w = new DetectWorker(env, engine_, std::move(ref), deferred);
        w->Queue();
    } catch (const std::exception& e) {
        deferred.Reject(Napi::Error::New(env, e.what()).Value());
    }
    return deferred.Promise();
}

Napi::Value Engine::embed_face(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto deferred = Napi::Promise::Deferred::New(env);
    try {
        ImageRef ref = parse_image_arg(info[0].As<Napi::Object>());
        naina::Face f = object_to_face(info[1].As<Napi::Object>());
        auto* w = new EmbedWorker(env, engine_, std::move(ref), f, deferred);
        w->Queue();
    } catch (const std::exception& e) {
        deferred.Reject(Napi::Error::New(env, e.what()).Value());
    }
    return deferred.Promise();
}

Napi::Value Engine::face_liveness(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto deferred = Napi::Promise::Deferred::New(env);
    try {
        ImageRef ref = parse_image_arg(info[0].As<Napi::Object>());
        naina::Face f = object_to_face(info[1].As<Napi::Object>());
        auto* w = new LivenessWorker(env, engine_, std::move(ref), f, deferred);
        w->Queue();
    } catch (const std::exception& e) {
        deferred.Reject(Napi::Error::New(env, e.what()).Value());
    }
    return deferred.Promise();
}

Napi::Value Engine::face_embed_dim(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), engine_->face_embed_dim());
}

// ── Module init ────────────────────────────────────────────────────

Napi::Value similarity(const Napi::CallbackInfo& info) {
    auto a = info[0].As<Napi::Float32Array>();
    auto b = info[1].As<Napi::Float32Array>();
    if (a.ElementLength() != b.ElementLength()) {
        Napi::TypeError::New(info.Env(), "vectors must have equal length")
            .ThrowAsJavaScriptException();
        return info.Env().Null();
    }
    const float s =
        naina::Engine::similarity(a.Data(), b.Data(), static_cast<int>(a.ElementLength()));
    return Napi::Number::New(info.Env(), s);
}

Napi::Object init(Napi::Env env, Napi::Object exports) {
    exports.Set("Engine", Engine::init(env));
    exports.Set("similarity", Napi::Function::New(env, similarity));
    exports.Set("version", Napi::String::New(env, naina_version_string()));
    return exports;
}

NODE_API_MODULE(naina, init)

}  // namespace
