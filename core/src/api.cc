// naina — C ABI implementation.
//
// Owns the runtime context, lazy-loads inference sessions per task, and glues
// the face/person modules to the backend abstraction.

#include "naina/backend.hpp"
#include "naina/model_loader.hpp"
#include "naina/naina.h"
#include "naina/tensor.hpp"

#include "image_ops.hpp"
#include "modules/face_detect.hpp"
#include "modules/face_embed.hpp"

#include <cmath>
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
}  // namespace

namespace {

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
    bool enable_research = false;
    int num_threads = 0;

    std::mutex sess_mu;
    std::unordered_map<std::string, std::unique_ptr<naina::backend::ISession>> sessions;

    naina::backend::ISession* session_for(const std::string& task, naina_status* out_status) {
        std::lock_guard<std::mutex> lk(sess_mu);
        auto it = sessions.find(task);
        if (it != sessions.end()) {
            *out_status = NAINA_OK;
            return it->second.get();
        }
        const naina::Tier tier = enable_research ? naina::Tier::Research : naina::Tier::Default;
        auto entry = registry.resolve(task, tier);
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
    ctx->enable_research = (cfg != nullptr) && cfg->enable_research_models != 0;
    ctx->num_threads = (cfg != nullptr) ? cfg->num_threads : 0;

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

// ── Face stack ────────────────────────────────────────────────────────

extern "C" naina_status naina_face_detect(naina_ctx_t* ctx,
                                          const naina_image_t* image,
                                          naina_face** out_faces,
                                          int32_t* out_count) {
    if (ctx == nullptr || image == nullptr || out_faces == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_faces = nullptr;
    *out_count = 0;

    naina_status s = NAINA_OK;
    auto* session = ctx->session_for("face_detect", &s);
    if (session == nullptr) {
        return s;
    }

    std::vector<naina_face> faces;
    s = naina::internal::face_detect::detect(session, view_of(image), {}, &faces);
    if (s != NAINA_OK) {
        return s;
    }

    if (faces.empty()) {
        return NAINA_OK;
    }
    auto* buf = static_cast<naina_face*>(std::malloc(sizeof(naina_face) * faces.size()));
    if (buf == nullptr) {
        return NAINA_E_OOM;
    }
    std::memcpy(buf, faces.data(), sizeof(naina_face) * faces.size());
    *out_faces = buf;
    *out_count = static_cast<int32_t>(faces.size());
    return NAINA_OK;
}

extern "C" void naina_free_faces(naina_face* faces, int32_t /*count*/) {
    std::free(faces);
}

extern "C" int32_t naina_face_embed_dim(const naina_ctx_t* ctx) {
    if (ctx == nullptr) {
        return 0;
    }
    // For SFace = 128; for EdgeFace / TransFace = 512. We don't know until
    // a session is loaded. v0.1 returns 0 ("query after first embed").
    auto* mctx = const_cast<naina_ctx_t*>(ctx);
    naina_status s = NAINA_OK;
    auto* session = mctx->session_for("face_embed", &s);
    if (session == nullptr) {
        return 0;
    }
    auto outs = session->outputs();
    if (outs.empty()) {
        return 0;
    }
    int32_t dim = 1;
    for (auto d : outs[0].shape) {
        if (d > 0) {
            dim *= static_cast<int32_t>(d);
        }
    }
    return dim;
}

extern "C" naina_status naina_face_embed(naina_ctx_t* ctx,
                                         const naina_image_t* image,
                                         const naina_face* face,
                                         float* out_embedding) {
    if (ctx == nullptr || image == nullptr || face == nullptr || out_embedding == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    naina_status s = NAINA_OK;
    auto* session = ctx->session_for("face_embed", &s);
    if (session == nullptr) {
        return s;
    }
    std::vector<float> emb;
    s = naina::internal::face_embed::embed(session, view_of(image), *face, {}, &emb);
    if (s != NAINA_OK) {
        return s;
    }
    std::memcpy(out_embedding, emb.data(), emb.size() * sizeof(float));
    return NAINA_OK;
}

extern "C" naina_status naina_face_liveness(naina_ctx_t* /*ctx*/,
                                            const naina_image_t* /*image*/,
                                            const naina_face* /*face*/,
                                            float* /*out_score*/) {
    // Stubbed: liveness module ships in v1.3.
    return NAINA_E_UNSUPPORTED;
}

extern "C" float naina_embed_similarity(const float* a, const float* b, int32_t dim) {
    if (a == nullptr || b == nullptr || dim <= 0) {
        return 0;
    }
    // Assumes L2-normalised inputs (dot product == cosine).
    float d = 0;
    for (int32_t i = 0; i < dim; ++i) {
        d += a[i] * b[i];
    }
    return d;
}

// ── Person / tracker stubs (v1.1) ─────────────────────────────────────

extern "C" naina_status naina_person_detect(naina_ctx_t*,
                                            const naina_image_t*,
                                            naina_person**,
                                            int32_t*) {
    return NAINA_E_UNSUPPORTED;
}
extern "C" void naina_free_persons(naina_person* persons, int32_t) {
    std::free(persons);
}
extern "C" int32_t naina_reid_embed_dim(const naina_ctx_t*) {
    return 0;
}
extern "C" naina_status naina_person_reid(naina_ctx_t*,
                                          const naina_image_t*,
                                          const naina_person*,
                                          float*) {
    return NAINA_E_UNSUPPORTED;
}
extern "C" naina_status naina_tracker_create(naina_ctx_t*, naina_tracker_t**) {
    return NAINA_E_UNSUPPORTED;
}
extern "C" void naina_tracker_release(naina_tracker_t*) {}
extern "C" naina_status naina_tracker_update(
    naina_tracker_t*, const naina_person*, int32_t, naina_person**, int32_t*) {
    return NAINA_E_UNSUPPORTED;
}
