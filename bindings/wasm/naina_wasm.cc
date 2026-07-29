// Emscripten binding: exposes naina's C ABI to JavaScript through embind.
//
// Deliberately thin. Every algorithm — resize geometry, DBNet decode, quad
// rectification, CTC decode, layout mapping, document assembly — stays in the
// shared C++ core, so a page in a browser runs the same arithmetic as a server.
// This file marshals and nothing else.
//
// The JSON surface mirrors the Node binding's shape so callers moving between
// them do not have to relearn field names.

#include "naina/model_loader.hpp"
#include "naina/naina.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

// Where the registry and the staged weights live inside Emscripten's virtual
// filesystem. Set from C++ rather than from the host page so that every JS
// caller — the demo, a user's bundle, the test — agrees without having to
// remember to configure it. setenv only fills in what the embedder has not
// already chosen, so overriding stays possible.
//
// registry.yaml is baked into the binary by --embed-file at this exact path.
void configure_paths_once() {
    setenv("NAINA_REGISTRY", "/naina/registry.yaml", 0);
    setenv("NAINA_CACHE", "/naina/models", 0);
}

// Context and page are owned by JS through explicit release(), matching the C
// ABI rather than inventing a different lifetime model for one platform.
class Reader {
public:
    Reader(int tier, int backend) {
        configure_paths_once();
        naina_config cfg{};
        cfg.version = 2;
        cfg.backend = static_cast<naina_backend>(backend);
        cfg.device = NAINA_DEVICE_AUTO;
        cfg.tier = static_cast<naina_tier>(tier);
        status_ = naina_init(&cfg, &ctx_);
    }

    ~Reader() {
        if (ctx_ != nullptr) {
            naina_release(ctx_);
        }
    }

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&) = delete;
    Reader& operator=(Reader&&) = delete;

    int status() const { return static_cast<int>(status_); }
    bool ok() const { return status_ == NAINA_OK && ctx_ != nullptr; }

    // rgb is a JS Uint8Array; embind copies it into a std::string, which is the
    // cheapest byte-container embind offers without a second heap allocation
    // dance. The pixels are read-only to naina.
    std::string read_markdown(const std::string& rgb, int width, int height) {
        naina_page_t* page = nullptr;
        if (!read_page(rgb, width, height, &page)) {
            return {};
        }
        std::string md = naina_page_markdown(page);
        naina_page_release(page);
        return md;
    }

    std::string read_json(const std::string& rgb, int width, int height) {
        naina_page_t* page = nullptr;
        if (!read_page(rgb, width, height, &page)) {
            return {};
        }
        std::string js = naina_page_json(page);
        naina_page_release(page);
        return js;
    }

    int last_error() const { return static_cast<int>(last_); }

private:
    bool read_page(const std::string& rgb, int width, int height, naina_page_t** out) {
        last_ = NAINA_OK;
        if (!ok()) {
            last_ = status_;
            return false;
        }
        const size_t need = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
        if (width <= 0 || height <= 0 || rgb.size() < need) {
            last_ = NAINA_E_INVALID_ARG;
            return false;
        }

        naina_image_t* im = nullptr;
        last_ = naina_image_wrap(reinterpret_cast<const uint8_t*>(rgb.data()),
                                 width,
                                 height,
                                 width * 3,
                                 NAINA_PIXFMT_RGB8,
                                 &im);
        if (last_ != NAINA_OK) {
            return false;
        }
        last_ = naina_read(ctx_, im, out);
        naina_image_release(im);
        return last_ == NAINA_OK && *out != nullptr;
    }

    naina_ctx_t* ctx_ = nullptr;
    naina_status status_ = NAINA_E_INVALID_ARG;
    naina_status last_ = NAINA_OK;
};

std::string version() {
    return naina_version_string();
}

std::string status_text(int s) {
    return naina_status_str(static_cast<naina_status>(s));
}

// Escape the few characters that can appear in a URL or path and would break
// JSON. Model ids and release URLs are ASCII by construction, so this does not
// need to handle the full escape table.
void json_escape_into(const std::string& in, std::string* out) {
    for (const char c : in) {
        if (c == '"' || c == '\\') {
            *out += '\\';
        }
        *out += c;
    }
}

// The list of weight files a tier needs, as
// [{"path":"...","url":"...","bytes":N}, ...].
//
// Computed by the C++ core rather than by JS on purpose. The cache layout
// (<root>/<task>/<id>/<sha256[:16]>__<basename>) and the ${release_base}
// substitution live in model_loader; a JS reimplementation would be a second
// source of truth that drifts the first time either changes.
std::string staging_plan(int tier_int) {
    configure_paths_once();
    const char* reg_path = std::getenv("NAINA_REGISTRY");
    if (reg_path == nullptr) {
        return "[]";
    }

    naina::Tier want = naina::Tier::Small;
    if (tier_int == static_cast<int>(NAINA_TIER_TINY)) {
        want = naina::Tier::Tiny;
    } else if (tier_int == static_cast<int>(NAINA_TIER_MEDIUM)) {
        want = naina::Tier::Medium;
    }

    naina::ModelRegistry reg;
    try {
        reg = naina::ModelRegistry::load(reg_path);
    } catch (const std::exception&) {
        return "[]";
    }

    std::string out = "[";
    bool first = true;
    for (const naina::ModelEntry& m : reg.all()) {
        if (m.tier != want) {
            continue;
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
    return out;
}

}  // namespace

EMSCRIPTEN_BINDINGS(naina) {
    emscripten::class_<Reader>("Reader")
        .constructor<int, int>()
        .function("readMarkdown", &Reader::read_markdown)
        .function("readJson", &Reader::read_json)
        .function("status", &Reader::status)
        .function("ok", &Reader::ok)
        .function("lastError", &Reader::last_error);

    emscripten::function("version", &version);
    emscripten::function("statusText", &status_text);
    emscripten::function("stagingPlan", &staging_plan);
}
