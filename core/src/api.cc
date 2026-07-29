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
#include "modules/doc_assemble.hpp"
#include "modules/layout_detect.hpp"
#include "modules/text_detect.hpp"
#include "modules/text_recognize.hpp"
#include "page.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

    // Recognition alphabet. Empty means the default (Latin + CJK). Detection
    // and layout ignore this: they are script-agnostic.
    std::string language;
    int num_threads = 0;

    std::mutex sess_mu;

    // Keyed by "<task>@<language>", not by task alone. Recognition models differ
    // per alphabet, so one entry per task would hand back the wrong model the
    // moment two alphabets are used in one context -- which is exactly what
    // automatic script selection does. Detection and layout are script-agnostic
    // and always key with an empty language, so they are loaded once and shared.
    std::unordered_map<std::string, std::unique_ptr<naina::backend::ISession>> sessions;

    // Charsets, one per alphabet, parsed from each recognition model's own
    // charset_yaml artifact. Also keyed by language for the same reason.
    std::unordered_map<std::string, naina::internal::charset::Charset> charsets;

    static std::string cache_key(const std::string& task, const std::string& lang) {
        return task + "@" + lang;
    }

    // Resolve and parse the charset that belongs to the recognition model for
    // the active tier. Returns nullptr with a status on failure — without a
    // charset, decoding cannot map class indices to characters at all.
    const naina::internal::charset::Charset* charset_for_recognize(naina_status* out_status,
                                                                   const std::string& lang) {
        std::lock_guard<std::mutex> lk(sess_mu);
        const std::string key = cache_key("text_recognize", lang);
        if (const auto it = charsets.find(key); it != charsets.end()) {
            *out_status = NAINA_OK;
            return &it->second;
        }
        auto entry = registry.resolve("text_recognize", tier, lang);
        if (!entry && tier != naina::Tier::Medium) {
            entry = registry.resolve("text_recognize", naina::Tier::Medium, lang);
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
        naina::internal::charset::Charset parsed;
        if (!naina::internal::charset::load_from_yaml(path, &parsed)) {
            *out_status = NAINA_E_IO;
            return nullptr;
        }
        const auto [it, _] = charsets.emplace(key, std::move(parsed));
        *out_status = NAINA_OK;
        return &it->second;
    }

    // The layout model's input is a fixed square whose side differs per
    // variant: 480 for PP-DocLayout-S, 640 for -M, 800 for V3. Read it from the
    // loaded session rather than hardcoding, so a registry change cannot
    // silently feed the wrong resolution.
    int32_t layout_input_side() {
        naina_status s = NAINA_OK;
        auto* sess = session_for("layout_detect", &s);
        if (sess == nullptr) {
            return 800;
        }
        for (const auto& d : sess->inputs()) {
            if (d.name == "image" && d.shape.size() == 4 && d.shape[2] > 0) {
                return static_cast<int32_t>(d.shape[2]);
            }
        }
        return 800;
    }

    // `lang_override` lets a caller ask for a specific alphabet without mutating
    // the context, which is what script probing needs: it compares several
    // alphabets on one image and must not leave the context pointing at the last
    // one it tried.

    // ── automatic alphabet selection ──────────────────────────────────
    //
    // Recognise a SAMPLE of the detected boxes with each candidate alphabet and
    // keep the one whose mean confidence is highest, provided it beats the
    // default alphabet by a margin.
    //
    // Both constants come from measurement across four scripts. Best-alphabet
    // mean against the default's, on the same image:
    //
    //   Hindi     devanagari  0.937 vs 0.511   +0.426
    //   Cyrillic  cyrillic    0.998 vs 0.894   +0.104
    //   Greek     el          0.979 vs 0.913   +0.066
    //   Latin     arabic      0.989 vs 0.983   +0.006
    //
    // Two consequences.
    //
    // Comparing alphabets on the SAME input works where an absolute threshold
    // cannot: Cyrillic read with the wrong Devanagari model still scored 0.918,
    // above any cutoff that would catch Hindi-read-as-Latin at 0.511.
    //
    // Every alphabet contains Latin, so on Latin input they all tie near 0.98 and
    // a plain argmax picks `arabic` for an English page. The default therefore has
    // to be DISPLACED by a margin rather than merely beaten. 0.03 leaves 2x
    // headroom under the tightest true positive and 5x over the Latin tie.
    //
    // Candidates are whatever the registry describes at this tier, so this grows
    // with the registry and needs no list here. An alphabet whose weights are not
    // cached simply fails to load and is skipped -- naina does not download nine
    // models to answer this question.
    static constexpr float kAutoMargin = 0.03F;
    static constexpr float kAutoDecisive = 0.25F;
    static constexpr size_t kAutoSampleBoxes = 8;

    std::string detect_language(const naina::internal::ImageView& view,
                                const std::vector<naina_textbox>& boxes,
                                naina_status* out_status) {
        *out_status = NAINA_OK;
        if (boxes.empty()) {
            return {};
        }

        // A sample is enough: this is a vote on which alphabet fits, not the read
        // that gets returned. Highest-scoring boxes first, since a marginal
        // detection is a poor witness.
        std::vector<naina_textbox> sample = boxes;
        std::sort(sample.begin(), sample.end(), [](const naina_textbox& a, const naina_textbox& b) {
            return a.score > b.score;
        });
        if (sample.size() > kAutoSampleBoxes) {
            sample.resize(kAutoSampleBoxes);
        }

        const auto score_for = [&](const std::string& cand) -> float {
            naina_status st = NAINA_OK;
            auto* sess = session_for("text_recognize", &st, &cand);
            if (sess == nullptr || st != NAINA_OK) {
                return -1.0F;
            }
            const auto* cs = charset_for_recognize(&st, cand);
            if (cs == nullptr || st != NAINA_OK) {
                return -1.0F;
            }
            std::vector<naina::internal::text_recognize::Line> out;
            if (naina::internal::text_recognize::recognize(sess, view, sample, *cs, {}, &out) !=
                NAINA_OK) {
                return -1.0F;
            }
            double sum = 0.0;
            int64_t n = 0;
            for (const auto& l : out) {
                if (!l.text.empty()) {
                    sum += static_cast<double>(l.confidence);
                    ++n;
                }
            }
            return n == 0 ? -1.0F : static_cast<float>(sum / static_cast<double>(n));
        };

        const float baseline = score_for(std::string());
        if (baseline < 0.0F) {
            // The default alphabet could not even be loaded; nothing to compare
            // against, so let the caller fail on the real read rather than here.
            return {};
        }

        std::string best;
        float best_score = baseline;
        for (const auto& m : registry.all()) {
            if (m.task != "text_recognize" || m.lang.empty()) {
                continue;
            }
            const float sc = score_for(m.lang);
            if (sc > best_score) {
                best_score = sc;
                best = m.lang;
            }
            // A margin this large is not a close call; stop paying for the rest.
            if (sc >= baseline + kAutoDecisive) {
                break;
            }
        }

        return (!best.empty() && best_score >= baseline + kAutoMargin) ? best : std::string();
    }

    naina::backend::ISession* session_for(const std::string& task,
                                          naina_status* out_status,
                                          const std::string* lang_override = nullptr) {
        std::lock_guard<std::mutex> lk(sess_mu);

        // Language applies to recognition only. Detection and layout are
        // script-agnostic — measured on a Devanagari page, DBNet located all 82
        // lines correctly — so they resolve with no language and are shared
        // across alphabets rather than loaded once per script.
        const std::string task_lang = (task == "text_recognize")
                                          ? (lang_override != nullptr ? *lang_override : language)
                                          : std::string();

        const std::string key = cache_key(task, task_lang);
        auto it = sessions.find(key);
        if (it != sessions.end()) {
            *out_status = NAINA_OK;
            return it->second.get();
        }

        auto entry = registry.resolve(task, tier, task_lang);
        // Tier fallback: a tier that lacks this task degrades to a larger one
        // rather than failing. Every task currently exists at every tier, so
        // this is a safety net for future partial registries.
        if (!entry && tier != naina::Tier::Medium) {
            entry = registry.resolve(task, naina::Tier::Medium, task_lang);
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
            sessions.emplace(key, std::move(sess));
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

        // models_root means "keep the models here", not merely "find the
        // manifest here". Without this the manifest's own cache_root wins, which
        // defaults to ~/.cache — unusable in a sandboxed app, where HOME is
        // unset and "~" stays literal on a read-only filesystem.
        if (cfg != nullptr && cfg->models_root != nullptr && cfg->models_root[0] != '\0') {
            ctx->registry.set_cache_root(cfg->models_root);
        }
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

    // `language` only exists in config version >= 3. Older callers get the
    // default alphabet, which is what they were already getting.
    if (cfg != nullptr && cfg->version >= 3 && cfg->language != nullptr &&
        cfg->language[0] != '\0') {
        ctx->language = cfg->language;

        // "auto" is resolved per read, not here: it depends on the image.
        if (ctx->language != "auto") {
            // Reject an unknown language HERE, at init, rather than letting it
            // surface later as a missing model or -- far worse -- as a silent
            // fall back to the Latin alphabet. Recognising Devanagari with a
            // Latin alphabet is what returned "3rarearanlus Tarafaaa:" at 0.758
            // confidence, and refusing up front is the whole point of this field.
            if (!ctx->registry.resolve("text_recognize", ctx->tier, ctx->language)) {
                return NAINA_E_UNSUPPORTED;
            }
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
    const auto view = view_of(image);

    // Detection runs before recognition regardless, and it is script-agnostic, so
    // automatic alphabet selection reuses these boxes rather than detecting per
    // candidate. Measured on a Devanagari page: DBNet located all 82 lines
    // correctly even while the wrong recogniser produced garbage.
    std::vector<naina_textbox> boxes;
    s = naina::internal::text_detect::detect(det, view, {}, &boxes);
    if (s != NAINA_OK) {
        return s;
    }

    std::string lang = ctx->language;
    if (lang == "auto") {
        lang = ctx->detect_language(view, boxes, &s);
        if (s != NAINA_OK) {
            return s;
        }
    }

    auto* rec = ctx->session_for("text_recognize", &s, &lang);
    if (rec == nullptr) {
        return s;
    }
    const auto* cs = ctx->charset_for_recognize(&s, lang);
    if (cs == nullptr) {
        return s;
    }

    std::vector<naina::internal::text_recognize::Line> lines;
    s = naina::internal::text_recognize::recognize(rec, view, boxes, *cs, {}, &lines);
    if (s != NAINA_OK) {
        return s;
    }

    // Layout analysis is best-effort. If its weights are unavailable the page
    // still returns recognised text, just without structure — degrading to
    // text-only beats failing the whole read.
    std::vector<naina_region> regions;
    naina_status ls = NAINA_OK;
    if (auto* lay = ctx->session_for("layout_detect", &ls); lay != nullptr) {
        naina::internal::layout_detect::Config lcfg;
        lcfg.input_side = ctx->layout_input_side();
        if (naina::internal::layout_detect::detect(lay, view, lcfg, &regions) != NAINA_OK) {
            regions.clear();
        }
    }

    // doc_assemble owns reading order and markdown. Its Line type mirrors the
    // recogniser's, so convert rather than leaking one module's type into the
    // other's interface.
    std::vector<naina::internal::doc_assemble::Line> dlines;
    dlines.reserve(lines.size());
    for (const auto& l : lines) {
        naina::internal::doc_assemble::Line d;
        d.box = l.box;
        d.text = l.text;
        d.confidence = l.confidence;
        dlines.push_back(std::move(d));
    }
    const naina::internal::doc_assemble::Config dcfg;
    naina::internal::doc_assemble::assign_lines_to_regions(regions, dcfg, &dlines);
    naina::internal::doc_assemble::order_regions(dcfg, &regions);
    std::string markdown = naina::internal::doc_assemble::to_markdown(regions, dlines);

    auto page = std::make_unique<naina_page>();
    for (size_t i = 0; i < lines.size(); ++i) {
        page->add_line(lines[i].box, lines[i].text, lines[i].confidence);
        page->set_line_region(i, dlines[i].region_id);
    }
    for (const auto& r : regions) {
        page->add_region(r.bbox, r.kind, r.order);
    }
    page->set_markdown(std::move(markdown));
    // Report the alphabet actually used. With "auto" the caller cannot otherwise
    // know what was chosen, and an unreported choice is a silent guess.
    page->set_language(lang);
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

    naina_status s = NAINA_OK;
    auto* session = ctx->session_for("layout_detect", &s);
    if (session == nullptr) {
        return s;
    }
    naina::internal::layout_detect::Config cfg;
    cfg.input_side = ctx->layout_input_side();
    std::vector<naina_region> regions;
    s = naina::internal::layout_detect::detect(session, view_of(image), cfg, &regions);
    if (s != NAINA_OK) {
        return s;
    }
    naina::internal::doc_assemble::order_regions({}, &regions);
    if (regions.empty()) {
        return NAINA_OK;
    }
    auto* buf = static_cast<naina_region*>(std::malloc(sizeof(naina_region) * regions.size()));
    if (buf == nullptr) {
        return NAINA_E_OOM;
    }
    std::memcpy(buf, regions.data(), sizeof(naina_region) * regions.size());
    *out_regions = buf;
    *out_count = static_cast<int32_t>(regions.size());
    return NAINA_OK;
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

// ── Model staging ─────────────────────────────────────────────────────
//
// Shared by every host that fetches weights itself: Android (the NDK has no
// libcurl) and the browser (no sockets, and GitHub release assets send no CORS
// header). The WASM binding wraps this same logic through embind.

namespace {

// Escape only what can appear in a filesystem path or a release URL. Model ids
// and URLs are ASCII by construction, so the full escape table is unnecessary.
void json_escape_into(const std::string& in, std::string* out) {
    for (const char c : in) {
        if (c == '"' || c == '\\') {
            *out += '\\';
        }
        *out += c;
    }
}

}  // namespace

extern "C" naina_status naina_staging_plan(const char* registry_path,
                                           const char* models_root,
                                           naina_tier tier,
                                           const char* language,
                                           char** out_json) {
    if (registry_path == nullptr || out_json == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_json = nullptr;

    naina::Tier want = naina::Tier::Small;
    if (tier == NAINA_TIER_TINY) {
        want = naina::Tier::Tiny;
    } else if (tier == NAINA_TIER_MEDIUM) {
        want = naina::Tier::Medium;
    }
    const std::string lang = (language == nullptr) ? std::string() : std::string(language);

    naina::ModelRegistry reg;
    try {
        reg = naina::ModelRegistry::load(registry_path);
    } catch (const std::exception&) {
        return NAINA_E_IO;
    }
    // Must match what naina_init will use, or the caller stages files where the
    // core will not look for them.
    if (models_root != nullptr && models_root[0] != '\0') {
        reg.set_cache_root(models_root);
    }

    std::string out = "[";
    bool first = true;
    bool have_recognizer = false;
    for (const naina::ModelEntry& m : reg.all()) {
        if (m.tier != want) {
            continue;
        }
        // Recognition is language-specific; detection and layout are shared
        // across alphabets and must not be filtered out by a language request.
        //
        // "auto" needs EVERY alphabet staged, because it decides by recognising a
        // sample with each one and a build with no network (browser, Android)
        // cannot fetch a candidate mid-decision. That is the honest cost of auto,
        // and a caller who wants to pay it incrementally should stage the default
        // first and ask for the full set only when a read comes back weak.
        const bool lang_ok =
            (m.task == "text_recognize") ? (lang == "auto" || m.lang == lang) : m.lang.empty();
        if (!lang_ok) {
            continue;
        }
        if (m.task == "text_recognize") {
            have_recognizer = true;
        }
        for (const auto& [kind, file] : m.files) {
            if (file.url.empty()) {
                continue;
            }
            if (!first) {
                out += ',';
            }
            first = false;
            out += "{\"path\":\"";
            json_escape_into(reg.cache_path_for(m, kind).string(), &out);
            out += "\",\"url\":\"";
            json_escape_into(file.url, &out);
            out += "\",\"bytes\":";
            out += std::to_string(file.bytes);
            out += '}';
        }
    }
    out += ']';

    if (first) {
        return NAINA_E_MODEL_NOT_FOUND;
    }

    // Detection and layout match every language, so an unknown language still
    // produces a non-empty plan — one with no recogniser in it. Staging that and
    // then failing at init would be a confusing way to learn the language was
    // wrong, and returning OK for it is worse: measured, "klingon" returned ok.
    if (!have_recognizer) {
        return NAINA_E_UNSUPPORTED;
    }

    auto* buf = static_cast<char*>(std::malloc(out.size() + 1));
    if (buf == nullptr) {
        return NAINA_E_OOM;
    }
    std::memcpy(buf, out.c_str(), out.size() + 1);
    *out_json = buf;
    return NAINA_OK;
}

extern "C" void naina_free_string(char* s) {
    std::free(s);
}
