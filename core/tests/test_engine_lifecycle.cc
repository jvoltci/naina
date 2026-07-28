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

int main() {
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

    // If no backend is compiled in, init returns NAINA_E_BACKEND_UNAVAIL —
    // that's a valid environment for the "core-only" build matrix.
    if (init_rc == NAINA_E_BACKEND_UNAVAIL) {
        std::printf("engine_lifecycle: no backend available, skipping (OK)\n");
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

    // With a valid image but no weights on disk, detect must fail rather than
    // report a bogus empty success. NAINA_E_UNSUPPORTED is expected while the
    // module is stubbed; MODEL_NOT_FOUND / IO once it is wired but offline.
    naina_textbox* boxes = nullptr;
    int32_t nboxes = -1;
    const naina_status dt = naina_text_detect(ctx, img, &boxes, &nboxes);
    EXPECT(dt != NAINA_OK);
    EXPECT(nboxes == 0);
    EXPECT(boxes == nullptr);
    naina_free_textboxes(boxes, nboxes);

    // naina_read has the same contract.
    naina_page_t* page = nullptr;
    const naina_status rd = naina_read(ctx, img, &page);
    EXPECT(rd != NAINA_OK);
    EXPECT(page == nullptr);
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
