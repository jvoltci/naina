// C++ ergonomic wrapper — thin glue over the C ABI defined in naina.h.

#include "naina/naina.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace naina {

namespace {

naina_pixfmt to_c(PixFmt f) {
    switch (f) {
        case PixFmt::RGB8:
            return NAINA_PIXFMT_RGB8;
        case PixFmt::BGR8:
            return NAINA_PIXFMT_BGR8;
        case PixFmt::NV12:
            return NAINA_PIXFMT_NV12;
        case PixFmt::YUV420P:
            return NAINA_PIXFMT_YUV420P;
        case PixFmt::Gray8:
            return NAINA_PIXFMT_GRAY8;
    }
    return NAINA_PIXFMT_RGB8;
}

naina_backend to_c(Backend b) {
    switch (b) {
        case Backend::Auto:
            return NAINA_BACKEND_AUTO;
        case Backend::ONNXRuntime:
            return NAINA_BACKEND_ONNXRUNTIME;
        case Backend::OpenVINO:
            return NAINA_BACKEND_OPENVINO;
        case Backend::NCNN:
            return NAINA_BACKEND_NCNN;
        case Backend::MNN:
            return NAINA_BACKEND_MNN;
        case Backend::CoreML:
            return NAINA_BACKEND_COREML;
        case Backend::TensorRT:
            return NAINA_BACKEND_TENSORRT;
        case Backend::ExecuTorch:
            return NAINA_BACKEND_EXECUTORCH;
    }
    return NAINA_BACKEND_AUTO;
}

naina_device to_c(Device d) {
    switch (d) {
        case Device::Auto:
            return NAINA_DEVICE_AUTO;
        case Device::CPU:
            return NAINA_DEVICE_CPU;
        case Device::GPU:
            return NAINA_DEVICE_GPU;
        case Device::NPU:
            return NAINA_DEVICE_NPU;
    }
    return NAINA_DEVICE_AUTO;
}

[[noreturn]] void throw_if(naina_status s, const char* where) {
    throw Error(s, where);
}

}  // namespace

// ── Image ────────────────────────────────────────────────────────────

Image::Image(const uint8_t* data, int w, int h, int stride, PixFmt fmt) {
    const auto s = naina_image_wrap(data, w, h, stride, to_c(fmt), &h_);
    if (s != NAINA_OK) {
        throw_if(s, "Image::Image");
    }
}

Image::~Image() {
    if (h_ != nullptr) {
        naina_image_release(h_);
    }
}

Image::Image(Image&& other) noexcept : h_(other.h_) {
    other.h_ = nullptr;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        if (h_ != nullptr) {
            naina_image_release(h_);
        }
        h_ = other.h_;
        other.h_ = nullptr;
    }
    return *this;
}

// ── Engine ───────────────────────────────────────────────────────────

Engine::Engine(const Config& cfg) {
    naina_config c{};
    c.version = 1;
    c.backend = to_c(cfg.backend);
    c.device = to_c(cfg.device);
    const std::string root = cfg.models_root.string();
    c.models_root = root.empty() ? nullptr : root.c_str();
    c.num_threads = cfg.num_threads;
    c.enable_research_models = cfg.enable_research_models ? 1 : 0;
    const auto s = naina_init(&c, &ctx_);
    if (s != NAINA_OK) {
        throw_if(s, "Engine::Engine");
    }
}

Engine::~Engine() {
    if (ctx_ != nullptr) {
        naina_release(ctx_);
    }
}

Engine::Engine(Engine&& other) noexcept : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

Engine& Engine::operator=(Engine&& other) noexcept {
    if (this != &other) {
        if (ctx_ != nullptr) {
            naina_release(ctx_);
        }
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

std::vector<Face> Engine::detect_faces(const Image& img) {
    naina_face* arr = nullptr;
    int32_t n = 0;
    const auto s = naina_face_detect(ctx_, img.handle(), &arr, &n);
    if (s != NAINA_OK) {
        throw_if(s, "Engine::detect_faces");
    }
    std::vector<Face> out;
    out.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        const auto& f = arr[i];
        Face g{};
        g.bbox = {f.bbox.x, f.bbox.y, f.bbox.w, f.bbox.h, f.bbox.score};
        for (size_t k = 0; k < 5; ++k) {
            g.landmarks[k] = {f.landmarks[k].x, f.landmarks[k].y};
        }
        g.quality = f.quality;
        g.track_id = f.track_id;
        out.push_back(g);
    }
    naina_free_faces(arr, n);
    return out;
}

int Engine::face_embed_dim() const noexcept {
    return naina_face_embed_dim(ctx_);
}

std::vector<float> Engine::embed_face(const Image& img, const Face& f) {
    const int dim = face_embed_dim();
    if (dim <= 0) {
        throw Error(NAINA_E_NOT_INITIALIZED, "Engine::embed_face");
    }
    naina_face cf{};
    cf.bbox = {f.bbox.x, f.bbox.y, f.bbox.w, f.bbox.h, f.bbox.score};
    for (size_t k = 0; k < 5; ++k) {
        cf.landmarks[k] = {f.landmarks[k].x, f.landmarks[k].y};
    }
    cf.quality = f.quality;
    cf.track_id = f.track_id;

    std::vector<float> out(static_cast<size_t>(dim));
    const auto s = naina_face_embed(ctx_, img.handle(), &cf, out.data());
    if (s != NAINA_OK) {
        throw_if(s, "Engine::embed_face");
    }
    return out;
}

float Engine::face_liveness(const Image& img, const Face& f) {
    naina_face cf{};
    cf.bbox = {f.bbox.x, f.bbox.y, f.bbox.w, f.bbox.h, f.bbox.score};
    for (size_t k = 0; k < 5; ++k) {
        cf.landmarks[k] = {f.landmarks[k].x, f.landmarks[k].y};
    }
    cf.quality = f.quality;
    cf.track_id = f.track_id;
    float score = 0;
    const auto s = naina_face_liveness(ctx_, img.handle(), &cf, &score);
    if (s != NAINA_OK) {
        throw_if(s, "Engine::face_liveness");
    }
    return score;
}

// Person / tracker — stubs for v1.0. Throw NAINA_E_UNSUPPORTED.
std::vector<Person> Engine::detect_persons(const Image&) {
    throw Error(NAINA_E_UNSUPPORTED, "Engine::detect_persons");
}
std::vector<float> Engine::embed_person(const Image&, const Person&) {
    throw Error(NAINA_E_UNSUPPORTED, "Engine::embed_person");
}
int Engine::reid_embed_dim() const noexcept {
    return naina_reid_embed_dim(ctx_);
}
Tracker Engine::make_tracker() {
    throw Error(NAINA_E_UNSUPPORTED, "Engine::make_tracker");
}

// ── Tracker (stubs) ──────────────────────────────────────────────────

Tracker::~Tracker() {
    if (h_ != nullptr) {
        naina_tracker_release(h_);
    }
}
Tracker::Tracker(Tracker&& other) noexcept : h_(other.h_) {
    other.h_ = nullptr;
}
Tracker& Tracker::operator=(Tracker&& other) noexcept {
    if (this != &other) {
        if (h_ != nullptr) {
            naina_tracker_release(h_);
        }
        h_ = other.h_;
        other.h_ = nullptr;
    }
    return *this;
}
std::vector<Person> Tracker::update(const std::vector<Person>&) {
    throw Error(NAINA_E_UNSUPPORTED, "Tracker::update");
}

}  // namespace naina
