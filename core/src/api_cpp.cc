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

naina_tier to_c(Tier t) {
    switch (t) {
        case Tier::Auto:
            return NAINA_TIER_AUTO;
        case Tier::Tiny:
            return NAINA_TIER_TINY;
        case Tier::Small:
            return NAINA_TIER_SMALL;
        case Tier::Medium:
            return NAINA_TIER_MEDIUM;
    }
    return NAINA_TIER_AUTO;
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
    c.version = 3;
    c.backend = to_c(cfg.backend);
    c.device = to_c(cfg.device);
    const std::string root = cfg.models_root.string();
    c.models_root = root.empty() ? nullptr : root.c_str();
    c.num_threads = cfg.num_threads;
    c.enable_research_models = 0;
    c.tier = to_c(cfg.tier);
    // Borrowed for the duration of naina_init, which copies it.
    c.language = cfg.language.empty() ? nullptr : cfg.language.c_str();
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
// ── Page ─────────────────────────────────────────────────────────────

Page::~Page() {
    if (h_ != nullptr) {
        naina_page_release(h_);
    }
}

Page::Page(Page&& other) noexcept : h_(other.h_) {
    other.h_ = nullptr;
}

Page& Page::operator=(Page&& other) noexcept {
    if (this != &other) {
        if (h_ != nullptr) {
            naina_page_release(h_);
        }
        h_ = other.h_;
        other.h_ = nullptr;
    }
    return *this;
}

std::vector<Line> Page::lines() const {
    std::vector<Line> out;
    if (h_ == nullptr) {
        return out;
    }
    const naina_textline* arr = nullptr;
    int32_t n = 0;
    if (naina_page_lines(h_, &arr, &n) != NAINA_OK || arr == nullptr) {
        return out;
    }
    out.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        Line l;
        // Copy the text: the C ABI pointer is owned by the page.
        l.text = arr[i].text != nullptr ? arr[i].text : "";
        l.confidence = arr[i].confidence;
        l.score = arr[i].box.score;
        for (int c = 0; c < 4; ++c) {
            l.quad[static_cast<size_t>(c)] =
                Point{arr[i].box.corners[c].x, arr[i].box.corners[c].y};
        }
        out.push_back(std::move(l));
    }
    return out;
}

std::string Page::markdown() const {
    if (h_ == nullptr) {
        return {};
    }
    const char* s = naina_page_markdown(h_);
    return s != nullptr ? std::string(s) : std::string();
}

std::string Page::json() const {
    if (h_ == nullptr) {
        return {};
    }
    const char* s = naina_page_json(h_);
    return s != nullptr ? std::string(s) : std::string();
}

// ── Engine OCR surface ───────────────────────────────────────────────

Page Engine::read(const Image& img) {
    naina_page_t* p = nullptr;
    const auto s = naina_read(ctx_, img.handle(), &p);
    if (s != NAINA_OK) {
        throw_if(s, "Engine::read");
    }
    return Page(p);
}

std::vector<std::array<Point, 4>> Engine::detect_text(const Image& img) {
    naina_textbox* arr = nullptr;
    int32_t n = 0;
    const auto s = naina_text_detect(ctx_, img.handle(), &arr, &n);
    if (s != NAINA_OK) {
        throw_if(s, "Engine::detect_text");
    }
    std::vector<std::array<Point, 4>> out;
    out.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        std::array<Point, 4> q{};
        for (int c = 0; c < 4; ++c) {
            q[static_cast<size_t>(c)] = Point{arr[i].corners[c].x, arr[i].corners[c].y};
        }
        out.push_back(q);
    }
    naina_free_textboxes(arr, n);
    return out;
}

}  // namespace naina
