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
    Reader(int tier, int backend, const std::string& language)
        : language_(language) {
        configure_paths_once();
        naina_config cfg{};
        cfg.version = 3;
        cfg.backend = static_cast<naina_backend>(backend);
        cfg.device = NAINA_DEVICE_AUTO;
        cfg.tier = static_cast<naina_tier>(tier);
        // Held in language_ so the pointer stays valid across naina_init.
        cfg.language = language_.empty() ? nullptr : language_.c_str();
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

    std::string language_;
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

// The list of weight files a configuration needs, as
// [{"path":"...","url":"...","bytes":N}, ...].
//
// Delegates to naina_staging_plan in the C ABI rather than reimplementing it.
// This function used to have its own copy of the filtering, and the copies
// diverged the moment "auto" was added: the C version returned all 22 files while
// this one returned 2, so the browser staged no recognition model and every read
// failed with "model not found". One implementation, in the core.
std::string staging_plan(int tier_int, const std::string& lang) {
    configure_paths_once();
    const char* reg_path = std::getenv("NAINA_REGISTRY");
    const char* root = std::getenv("NAINA_CACHE");
    if (reg_path == nullptr) {
        return "[]";
    }

    char* json = nullptr;
    const naina_status rc = naina_staging_plan(reg_path,
                                              root,
                                              static_cast<naina_tier>(tier_int),
                                              lang.empty() ? nullptr : lang.c_str(),
                                              &json);
    if (rc != NAINA_OK || json == nullptr) {
        return "[]";
    }
    std::string out(json);
    naina_free_string(json);
    return out;
}

}  // namespace

EMSCRIPTEN_BINDINGS(naina) {
    emscripten::class_<Reader>("Reader")
        .constructor<int, int, std::string>()
        .function("readMarkdown", &Reader::read_markdown)
        .function("readJson", &Reader::read_json)
        .function("status", &Reader::status)
        .function("ok", &Reader::ok)
        .function("lastError", &Reader::last_error);

    emscripten::function("version", &version);
    emscripten::function("statusText", &status_text);
    emscripten::function("stagingPlan", &staging_plan);
}
