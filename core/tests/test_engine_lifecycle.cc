// Smoke test: Engine initialises and tears down cleanly, and the OCR C ABI
// honours its argument contracts.
//
// No model weights are on disk in a fresh build, and NAINA_OFFLINE=1 prevents
// fetching them — so the lazy session loader returns NAINA_E_MODEL_NOT_FOUND.
// That is the expected behaviour for a weights-absent build, and asserting it
// keeps a silent "succeeded without a model" regression from slipping through.

#include "naina/naina.h"
#include "naina/naina.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

static int failures = 0;
#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Null-argument contracts must hold before any model is loaded, so bindings
// can be written and tested against them today.
static void check_arg_contracts(naina_ctx_t* ctx) {
    naina_page_t* page = nullptr;
    EXPECT(naina_read(nullptr, nullptr, &page) == NAINA_E_INVALID_ARG);
    EXPECT(naina_read(ctx, nullptr, &page) == NAINA_E_INVALID_ARG);
    EXPECT(naina_read(ctx, nullptr, nullptr) == NAINA_E_INVALID_ARG);

    naina_textbox* boxes = nullptr;
    int32_t nboxes = -1;
    EXPECT(naina_text_detect(ctx, nullptr, &boxes, &nboxes) == NAINA_E_INVALID_ARG);
    EXPECT(naina_text_detect(nullptr, nullptr, &boxes, &nboxes) == NAINA_E_INVALID_ARG);

    naina_region* regions = nullptr;
    int32_t nregions = -1;
    EXPECT(naina_layout_detect(ctx, nullptr, &regions, &nregions) == NAINA_E_INVALID_ARG);

    const naina_textline* lines = nullptr;
    int32_t nlines = -1;
    EXPECT(naina_page_lines(nullptr, &lines, &nlines) == NAINA_E_INVALID_ARG);

    // Releasing null is a no-op, never a crash.
    naina_page_release(nullptr);
    naina_free_textboxes(nullptr, 0);
    naina_free_regions(nullptr, 0);
}

static void check_region_kind_names() {
    EXPECT(std::string(naina_region_kind_str(NAINA_REGION_TITLE)) == "title");
    EXPECT(std::string(naina_region_kind_str(NAINA_REGION_TABLE)) == "table");
    EXPECT(std::string(naina_region_kind_str(NAINA_REGION_UNKNOWN)) == "unknown");
    // Every enumerator must map to a non-empty name.
    for (int k = NAINA_REGION_UNKNOWN; k <= NAINA_REGION_PAGENUM; ++k) {
        const char* s = naina_region_kind_str(static_cast<naina_region_kind>(k));
        EXPECT(s != nullptr);
        EXPECT(s != nullptr && s[0] != '\0');
    }
}

// ── language selection ────────────────────────────────────────────────

static void test_auto_language_is_accepted_at_init() {
    // "auto" cannot be validated at init the way a named alphabet is, because it
    // depends on the image. It must not be rejected as unknown.
    naina_config cfg{};
    cfg.version = 3;
    cfg.tier = NAINA_TIER_TINY;
    cfg.language = "auto";
    naina_ctx_t* ctx = nullptr;
    const naina_status rc = naina_init(&cfg, &ctx);
    EXPECT(rc != NAINA_E_UNSUPPORTED);
    if (rc == NAINA_OK) {
        naina_release(ctx);
    }
}

static void test_unknown_language_is_still_refused() {
    // The whole point of the field. A silent fall back to Latin is what returned
    // "3rarearanlus Tarafaaa:" at 0.758 confidence on a Hindi page.
    naina_config cfg{};
    cfg.version = 3;
    cfg.tier = NAINA_TIER_TINY;
    cfg.language = "klingon";
    naina_ctx_t* ctx = nullptr;
    const naina_status rc = naina_init(&cfg, &ctx);
    EXPECT(rc == NAINA_E_UNSUPPORTED || rc == NAINA_E_MODEL_NOT_FOUND);
    if (rc == NAINA_OK) {
        naina_release(ctx);
    }
}

static void test_version_2_config_ignores_language() {
    // Additive-only ABI: a v2 config predates `language`, so whatever happens to
    // sit in that slot must not be read.
    naina_config cfg{};
    cfg.version = 2;
    cfg.tier = NAINA_TIER_TINY;
    cfg.language = "klingon";  // must be ignored at version 2
    naina_ctx_t* ctx = nullptr;
    const naina_status rc = naina_init(&cfg, &ctx);
    EXPECT(rc != NAINA_E_UNSUPPORTED);
    if (rc == NAINA_OK) {
        naina_release(ctx);
    }
}

int main() {
    test_auto_language_is_accepted_at_init();
    test_unknown_language_is_still_refused();
    test_version_2_config_ignores_language();
    // Tell the loader where to find registry.yaml; offline disables download.
    const std::filesystem::path repo = std::filesystem::current_path();
    setenv("NAINA_REGISTRY", (repo / "models" / "registry.yaml").c_str(), 1);
    setenv("NAINA_OFFLINE", "1", 1);

    naina_config cfg{};
    cfg.version = 2;
    cfg.backend = NAINA_BACKEND_AUTO;
    cfg.device = NAINA_DEVICE_AUTO;
    cfg.num_threads = 0;
    cfg.tier = NAINA_TIER_TINY;

    naina_ctx_t* ctx = nullptr;
    const naina_status init_rc = naina_init(&cfg, &ctx);

    // A build with no backend compiled in is legitimate (the "core-only"
    // matrix entry), but it cannot run inference — so skipping here means this
    // test validates nothing. Set NAINA_REQUIRE_BACKEND=1 to turn that into a
    // hard failure. CI and the dev presets set it, because a silent skip once
    // hid the fact that a whole clean rebuild had dropped ONNX Runtime and
    // every test still reported green.
    if (init_rc == NAINA_E_BACKEND_UNAVAIL) {
        const char* require = std::getenv("NAINA_REQUIRE_BACKEND");
        if (require != nullptr && require[0] != '0') {
            std::fprintf(stderr,
                         "FAIL no inference backend compiled in, but "
                         "NAINA_REQUIRE_BACKEND=1.\n"
                         "     Configure with -DNAINA_WITH_ONNXRUNTIME=ON.\n");
            return 1;
        }
        std::fprintf(stderr,
                     "SKIP engine_lifecycle: no inference backend compiled in, so the\n"
                     "     ABI contract checks below did NOT run. This is a vacuous pass.\n"
                     "     Configure with -DNAINA_WITH_ONNXRUNTIME=ON to exercise them.\n");
        return 0;
    }
    EXPECT(init_rc == NAINA_OK);
    EXPECT(ctx != nullptr);
    if (ctx == nullptr) {
        return 1;
    }

    check_arg_contracts(ctx);
    check_region_kind_names();

    // Synthetic 128x128 RGB image.
    constexpr int W = 128;
    constexpr int H = 128;
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 3, 128);
    naina_image_t* img = nullptr;
    EXPECT(naina_image_wrap(pixels.data(), W, H, W * 3, NAINA_PIXFMT_RGB8, &img) == NAINA_OK);

    // The image is flat mid-grey, so it contains no text. Two outcomes are
    // legitimate and which one occurs depends only on whether weights happen
    // to be in the cache:
    //   - weights absent (offline)  -> MODEL_NOT_FOUND, nothing produced
    //   - weights present           -> OK with zero detections
    // The invariant that actually matters, and holds either way, is that a
    // blank image never yields a detection.
    naina_textbox* boxes = nullptr;
    int32_t nboxes = -1;
    const naina_status dt = naina_text_detect(ctx, img, &boxes, &nboxes);
    EXPECT(dt == NAINA_OK || dt == NAINA_E_MODEL_NOT_FOUND || dt == NAINA_E_IO);
    EXPECT(nboxes == 0);
    EXPECT(boxes == nullptr);
    naina_free_textboxes(boxes, nboxes);

    naina_page_t* page = nullptr;
    const naina_status rd = naina_read(ctx, img, &page);
    EXPECT(rd == NAINA_OK || rd == NAINA_E_MODEL_NOT_FOUND || rd == NAINA_E_IO);
    if (rd == NAINA_OK) {
        EXPECT(page != nullptr);
        if (page != nullptr) {
            const naina_textline* lines = nullptr;
            int32_t nlines = -1;
            EXPECT(naina_page_lines(page, &lines, &nlines) == NAINA_OK);
            EXPECT(nlines == 0);
            // Markdown of a blank page is the empty string, never null.
            EXPECT(naina_page_markdown(page) != nullptr);
            EXPECT(std::string(naina_page_markdown(page)).empty());
        }
    } else {
        EXPECT(page == nullptr);
    }
    naina_page_release(page);

    // A config declaring version 1 is still accepted — the header's ABI rule
    // is that fields may only be added, never required.
    naina_config old_cfg{};
    old_cfg.version = 1;
    old_cfg.backend = NAINA_BACKEND_AUTO;
    old_cfg.device = NAINA_DEVICE_AUTO;
    naina_ctx_t* old_ctx = nullptr;
    EXPECT(naina_init(&old_cfg, &old_ctx) == NAINA_OK);
    naina_release(old_ctx);

    naina_image_release(img);
    naina_release(ctx);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("engine_lifecycle tests passed\n");
    return 0;
}
