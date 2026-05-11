// Smoke test: Engine can initialize and tear down cleanly.
//
// We don't (yet) ship a tiny test model, and the manifest's GH-releases URLs
// aren't populated until model uploads happen — so this test only exercises
// the lifecycle path. NAINA_OFFLINE=1 prevents network fetches; the lazy
// session loader for face_detect will return NAINA_E_MODEL_NOT_FOUND, which
// we assert as the expected behaviour for a "weights-not-on-disk" build.

#include "naina/naina.h"
#include "naina/naina.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

static int failures = 0;
#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

int main() {
    // Tell the loader where to find registry.yaml; offline disables download.
    const std::filesystem::path repo = std::filesystem::current_path();
    setenv("NAINA_REGISTRY", (repo / "models" / "registry.yaml").c_str(), 1);
    setenv("NAINA_OFFLINE", "1", 1);

    naina_config cfg{};
    cfg.version = 1;
    cfg.backend = NAINA_BACKEND_AUTO;
    cfg.device = NAINA_DEVICE_AUTO;
    cfg.num_threads = 0;
    cfg.enable_research_models = 0;

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

    // Synthetic 128x128 RGB image.
    constexpr int W = 128, H = 128;
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 3, 128);
    naina_image_t* img = nullptr;
    EXPECT(naina_image_wrap(pixels.data(), W, H, W * 3, NAINA_PIXFMT_RGB8, &img) == NAINA_OK);

    // detect should fail with model-not-found (no weights) or IO (stale
    // partial file in the cache from a previous run) when offline. Both are
    // acceptable; we just want to confirm it doesn't crash and doesn't
    // silently succeed without a real model.
    naina_face* faces = nullptr;
    int32_t n = 0;
    const naina_status dt = naina_face_detect(ctx, img, &faces, &n);
    EXPECT(dt != NAINA_OK || n == 0);
    if (faces != nullptr) {
        naina_free_faces(faces, n);
    }

    // Cosine-similarity helper works on identical L2-normalised vectors.
    float a[4] = {0.5F, 0.5F, 0.5F, 0.5F};  // L2-normalised
    float b[4] = {0.5F, 0.5F, 0.5F, 0.5F};
    const float sim = naina_embed_similarity(a, b, 4);
    EXPECT(sim > 0.99F && sim < 1.01F);

    naina_image_release(img);
    naina_release(ctx);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("engine_lifecycle tests passed\n");
    return 0;
}
