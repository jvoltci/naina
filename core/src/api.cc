// naina — C ABI implementation.
//
// Owns the runtime context, lazy-loads inference sessions per task, and glues
// the OCR modules to the backend abstraction.

#include "naina/backend.hpp"
#include "naina/model_loader.hpp"
#include "naina/naina.h"
#include "naina/tensor.hpp"

#include "image_ops.hpp"
#include "modules/charset.hpp"
#include "modules/text_detect.hpp"
#include "modules/text_recognize.hpp"
#include "page.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>

// ── Internal opaque types ────────────────────────────────────────────

struct naina_image {
    const uint8_t* data;
    int32_t width;
    int32_t height;
    int32_t stride;
    naina_pixfmt fmt;
};

namespace {

naina::internal::ImageView view_of(const naina_image_t* img) {
    return naina::internal::ImageView{img->data, img->width, img->height, img->stride, img->fmt};
}

// File-kind → backend-id mapping for session selection.
naina_backend backend_for_kind(const std::string& kind) {
    if (kind == "onnx")
        return NAINA_BACKEND_ONNXRUNTIME;
    if (kind == "ncnn_param")
        return NAINA_BACKEND_NCNN;
    if (kind == "coreml")
        return NAINA_BACKEND_COREML;
    if (kind == "tensorrt")
        return NAINA_BACKEND_TENSORRT;
    if (kind == "openvino")
        return NAINA_BACKEND_OPENVINO;
    return NAINA_BACKEND_AUTO;
}

// Priority order for file kinds when multiple are present in the manifest.
// We prefer accelerated/native formats first, then ONNX as the portable
// fallback that every platform can read.
const std::vector<std::string>& kind_priority() {
    static const std::vector<std::string> p = {
#if defined(__APPLE__)
        "coreml",
#endif
        "tensorrt",
        "ncnn_param",
        "openvino",
        "onnx",
    };
    return p;
}

}  // namespace

struct naina_ctx {
    naina::ModelRegistry registry;
    naina_backend preferred_backend = NAINA_BACKEND_AUTO;
    naina::Tier tier = naina::Tier::Small;
    int num_threads = 0;

    std::mutex sess_mu;
    std::unordered_map<std::string, std::unique_ptr<naina::backend::ISession>> sessions;

    // The recognition charset, loaded once from the manifest's charset_yaml
    // artifact. Guarded by the same mutex as the session cache.
    naina::internal::charset::Charset rec_charset;
    bool rec_charset_loaded = false;

    // Resolve and parse the charset that belongs to the recognition model for
    // the active tier. Returns nullptr with a status on failure — without a
    // charset, decoding cannot map class indices to characters at all.
    const naina::internal::charset::Charset* charset_for_recognize(naina_status* out_status) {
        std::lock_guard<std::mutex> lk(sess_mu);
        if (rec_charset_loaded) {
            *out_status = NAINA_OK;
            return &rec_charset;
        }
        auto entry = registry.resolve("text_recognize", tier);
        if (!entry && tier != naina::Tier::Medium) {
            entry = registry.resolve("text_recognize", naina::Tier::Medium);
        }
        if (!entry || entry->files.find("charset_yaml") == entry->files.end()) {
            *out_status = NAINA_E_MODEL_NOT_FOUND;
            return nullptr;
        }
        std::filesystem::path path;
        const naina_status ls = registry.ensure_local(*entry, "charset_yaml", &path);
        if (ls != NAINA_OK) {
            *out_status = ls;
            return nullptr;
        }
        if (!naina::internal::charset::load_from_yaml(path, &rec_charset)) {
            *out_status = NAINA_E_IO;
            return nullptr;
        }
        rec_charset_loaded = true;
        *out_status = NAINA_OK;
        return &rec_charset;
    }

    naina::backend::ISession* session_for(const std::string& task, naina_status* out_status) {
        std::lock_guard<std::mutex> lk(sess_mu);
        auto it = sessions.find(task);
        if (it != sessions.end()) {
            *out_status = NAINA_OK;
            return it->second.get();
        }
        auto entry = registry.resolve(task, tier);
        // Tier fallback: a tier that lacks this task degrades to a larger one
        // rather than failing. layout_detect is medium-only today.
        if (!entry && tier != naina::Tier::Medium) {
            entry = registry.resolve(task, naina::Tier::Medium);
        }
        if (!entry) {
            *out_status = NAINA_E_MODEL_NOT_FOUND;
            return nullptr;
        }

        // Walk available file kinds in priority order, taking the first
        // (kind, backend) pair where the backend is compiled in.
        auto avail = naina::backend::available_backends();
        for (const auto& kind : kind_priority()) {
            if (entry->files.find(kind) == entry->files.end()) {
                continue;
            }
            const naina_backend wanted = backend_for_kind(kind);
            if (wanted == NAINA_BACKEND_AUTO) {
                continue;
            }
            // Honour the user's preferred backend when possible.
            if (preferred_backend != NAINA_BACKEND_AUTO && wanted != preferred_backend) {
                continue;
            }
            naina::backend::IBackend* be = nullptr;
            for (auto* b : avail) {
                if (b->id() == wanted) {
                    be = b;
                    break;
                }
            }
            if (be == nullptr) {
                continue;
            }

            std::filesystem::path path;
            const naina_status ls = registry.ensure_local(*entry, kind, &path);
            if (ls != NAINA_OK) {
                *out_status = ls;
                return nullptr;
            }
            naina::backend::SessionOptions opts;
            opts.device = naina::Device::Auto;
            opts.num_threads = num_threads;
            opts.enable_fp16 = true;
            auto sess = be->load(path, opts, out_status);
            if (sess == nullptr) {
                return nullptr;
            }
            auto* raw = sess.get();
            sessions.emplace(task, std::move(sess));
            *out_status = NAINA_OK;
            return raw;
        }

        // If preferred_backend was set but no (kind, preferred) match, try
        // again with no preference.
        if (preferred_backend != NAINA_BACKEND_AUTO) {
            preferred_backend = NAINA_BACKEND_AUTO;
            return session_for(task, out_status);
        }

        *out_status = NAINA_E_BACKEND_UNAVAIL;
        return nullptr;
    }
};

// ── Lifecycle ────────────────────────────────────────────────────────

extern "C" naina_status naina_init(const naina_config* cfg, naina_ctx_t** out_ctx) {
    if (out_ctx == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_ctx = nullptr;

    auto ctx = std::make_unique<naina_ctx>();
    try {
        // Resolve registry path. For v0.1, registry.yaml lives in $NAINA_REGISTRY,
        // or at <models_root>/registry.yaml, or at the compile-time fallback.
        std::filesystem::path registry_path;
        if (const char* env = std::getenv("NAINA_REGISTRY"); env != nullptr) {
            registry_path = env;
        } else if (cfg != nullptr && cfg->models_root != nullptr) {
            registry_path = std::filesystem::path(cfg->models_root) / "registry.yaml";
        } else {
            // Same-directory fallback for development; CI / installs should set NAINA_REGISTRY.
            registry_path = std::filesystem::current_path() / "models" / "registry.yaml";
        }
        ctx->registry = naina::ModelRegistry::load(registry_path);
    } catch (const std::exception&) {
        return NAINA_E_MODEL_NOT_FOUND;
    }

    // Verify at least one backend is compiled in; per-task selection happens
    // lazily in session_for() based on the file kinds available in the
    // manifest.
    if (naina::backend::available_backends().empty()) {
        return NAINA_E_BACKEND_UNAVAIL;
    }
    ctx->preferred_backend = (cfg != nullptr) ? cfg->backend : NAINA_BACKEND_AUTO;
    ctx->num_threads = (cfg != nullptr) ? cfg->num_threads : 0;
    // `tier` only exists in config version >= 2. Older callers get Small.
    if (cfg != nullptr && cfg->version >= 2) {
        switch (cfg->tier) {
            case NAINA_TIER_TINY:
                ctx->tier = naina::Tier::Tiny;
                break;
            case NAINA_TIER_MEDIUM:
                ctx->tier = naina::Tier::Medium;
                break;
            case NAINA_TIER_SMALL:
            case NAINA_TIER_AUTO:
                ctx->tier = naina::Tier::Small;
                break;
        }
    }

    *out_ctx = ctx.release();
    return NAINA_OK;
}

extern "C" void naina_release(naina_ctx_t* ctx) {
    delete ctx;
}

// ── Image wrap ────────────────────────────────────────────────────────

extern "C" naina_status naina_image_wrap(const uint8_t* data,
                                         int32_t width,
                                         int32_t height,
                                         int32_t stride,
                                         naina_pixfmt fmt,
                                         naina_image_t** out_image) {
    if (data == nullptr || width <= 0 || height <= 0 || stride <= 0 || out_image == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    auto* img = new (std::nothrow) naina_image{data, width, height, stride, fmt};
    if (img == nullptr) {
        return NAINA_E_OOM;
    }
    *out_image = img;
    return NAINA_OK;
}

extern "C" void naina_image_release(naina_image_t* image) {
    delete image;
}

// ── OCR surface ───────────────────────────────────────────────────────
//
// naina_read and the page accessors are stubbed NAINA_E_UNSUPPORTED until
// the modules land (text_detect, text_rectify, text_recognize). The
// argument-validation contracts are real from day one so bindings can be
// written and tested against them now.

// naina_page_t is naina::internal::Page. The opaque struct exists so the
// header need not expose the C++ type.
struct naina_page : naina::internal::Page {};

extern "C" naina_status naina_read(naina_ctx_t* ctx,
                                   const naina_image_t* image,
                                   naina_page_t** out_page) {
    if (ctx == nullptr || image == nullptr || out_page == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_page = nullptr;

    naina_status s = NAINA_OK;
    auto* det = ctx->session_for("text_detect", &s);
    if (det == nullptr) {
        return s;
    }
    auto* rec = ctx->session_for("text_recognize", &s);
    if (rec == nullptr) {
        return s;
    }
    const auto* cs = ctx->charset_for_recognize(&s);
    if (cs == nullptr) {
        return s;
    }

    const auto view = view_of(image);

    std::vector<naina_textbox> boxes;
    s = naina::internal::text_detect::detect(det, view, {}, &boxes);
    if (s != NAINA_OK) {
        return s;
    }

    std::vector<naina::internal::text_recognize::Line> lines;
    s = naina::internal::text_recognize::recognize(rec, view, boxes, *cs, {}, &lines);
    if (s != NAINA_OK) {
        return s;
    }

    auto page = std::make_unique<naina_page>();
    for (auto& line : lines) {
        page->add_line(line.box, line.text, line.confidence);
    }
    *out_page = page.release();
    return NAINA_OK;
}

extern "C" void naina_page_release(naina_page_t* page) {
    delete page;
}

extern "C" naina_status naina_page_lines(const naina_page_t* page,
                                         const naina_textline** out_lines,
                                         int32_t* out_count) {
    if (page == nullptr || out_lines == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    const auto& lines = page->lines();
    *out_lines = lines.empty() ? nullptr : lines.data();
    *out_count = static_cast<int32_t>(lines.size());
    return NAINA_OK;
}

extern "C" naina_status naina_page_regions(const naina_page_t* page,
                                           const naina_region** out_regions,
                                           int32_t* out_count) {
    if (page == nullptr || out_regions == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    const auto& regions = page->regions();
    *out_regions = regions.empty() ? nullptr : regions.data();
    *out_count = static_cast<int32_t>(regions.size());
    return NAINA_OK;
}

extern "C" const char* naina_page_markdown(const naina_page_t* page) {
    return page == nullptr ? "" : page->markdown();
}

extern "C" const char* naina_page_json(const naina_page_t* page) {
    return page == nullptr ? "" : page->json();
}

extern "C" naina_status naina_text_detect(naina_ctx_t* ctx,
                                          const naina_image_t* image,
                                          naina_textbox** out_boxes,
                                          int32_t* out_count) {
    if (ctx == nullptr || image == nullptr || out_boxes == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_boxes = nullptr;
    *out_count = 0;

    naina_status s = NAINA_OK;
    auto* session = ctx->session_for("text_detect", &s);
    if (session == nullptr) {
        return s;
    }

    std::vector<naina_textbox> boxes;
    s = naina::internal::text_detect::detect(session, view_of(image), {}, &boxes);
    if (s != NAINA_OK) {
        return s;
    }
    if (boxes.empty()) {
        return NAINA_OK;
    }

    auto* buf = static_cast<naina_textbox*>(std::malloc(sizeof(naina_textbox) * boxes.size()));
    if (buf == nullptr) {
        return NAINA_E_OOM;
    }
    std::memcpy(buf, boxes.data(), sizeof(naina_textbox) * boxes.size());
    *out_boxes = buf;
    *out_count = static_cast<int32_t>(boxes.size());
    return NAINA_OK;
}

extern "C" void naina_free_textboxes(naina_textbox* boxes, int32_t /*count*/) {
    std::free(boxes);
}

extern "C" naina_status naina_layout_detect(naina_ctx_t* ctx,
                                            const naina_image_t* image,
                                            naina_region** out_regions,
                                            int32_t* out_count) {
    if (ctx == nullptr || image == nullptr || out_regions == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_regions = nullptr;
    *out_count = 0;
    return NAINA_E_UNSUPPORTED;
}

extern "C" void naina_free_regions(naina_region* regions, int32_t /*count*/) {
    std::free(regions);
}

extern "C" const char* naina_region_kind_str(naina_region_kind k) {
    switch (k) {
        case NAINA_REGION_TITLE:
            return "title";
        case NAINA_REGION_TEXT:
            return "text";
        case NAINA_REGION_LIST:
            return "list";
        case NAINA_REGION_TABLE:
            return "table";
        case NAINA_REGION_FIGURE:
            return "figure";
        case NAINA_REGION_CAPTION:
            return "caption";
        case NAINA_REGION_FORMULA:
            return "formula";
        case NAINA_REGION_HEADER:
            return "header";
        case NAINA_REGION_FOOTER:
            return "footer";
        case NAINA_REGION_PAGENUM:
            return "pagenum";
        case NAINA_REGION_UNKNOWN:
            break;
    }
    return "unknown";
}
