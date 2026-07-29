// End-to-end text spotting against real PP-OCRv6 weights.
//
// Skips cleanly (exit 0) when either the fixture or the weights are absent, so
// the suite stays runnable offline. Point it at a fixture with:
//
//   NAINA_E2E_FIXTURE=/path/to/fixture.rgb ctest -R test_ocr_e2e
//
// Fixture format: int32 LE width, int32 LE height, then width*height*3 RGB8
// bytes. naina ships no image decoder on purpose, so the test consumes raw
// pixels rather than a PNG.

#include "naina/naina.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

namespace {

struct RawImage {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgb;
};

bool load_raw(const std::string& path, RawImage* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    int32_t w = 0;
    int32_t h = 0;
    f.read(reinterpret_cast<char*>(&w), sizeof(w));
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || w <= 0 || h <= 0) {
        return false;
    }
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 3U;
    out->rgb.resize(n);
    f.read(reinterpret_cast<char*>(out->rgb.data()), static_cast<std::streamsize>(n));
    if (!f) {
        return false;
    }
    out->width = w;
    out->height = h;
    return true;
}

// Case-insensitive substring search, so the assertion does not hinge on the
// model's casing decisions.
bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

}  // namespace

int main() {
    const char* fixture = std::getenv("NAINA_E2E_FIXTURE");
    if (fixture == nullptr) {
        std::printf("test_ocr_e2e: NAINA_E2E_FIXTURE unset, skipping (OK)\n");
        return 0;
    }
    RawImage img;
    if (!load_raw(fixture, &img)) {
        std::printf("test_ocr_e2e: cannot read fixture '%s', skipping (OK)\n", fixture);
        return 0;
    }

    naina_config cfg{};
    cfg.version = 2;
    cfg.backend = NAINA_BACKEND_AUTO;
    cfg.device = NAINA_DEVICE_AUTO;
    cfg.tier = NAINA_TIER_TINY;

    naina_ctx_t* ctx = nullptr;
    const naina_status init_rc = naina_init(&cfg, &ctx);
    if (init_rc != NAINA_OK || ctx == nullptr) {
        std::printf("test_ocr_e2e: init returned %s, skipping (OK)\n", naina_status_str(init_rc));
        return 0;
    }

    naina_image_t* wrapped = nullptr;
    EXPECT(naina_image_wrap(
               img.rgb.data(), img.width, img.height, img.width * 3, NAINA_PIXFMT_RGB8, &wrapped) ==
           NAINA_OK);

    naina_page_t* page = nullptr;
    const naina_status rc = naina_read(ctx, wrapped, &page);
    if (rc == NAINA_E_MODEL_NOT_FOUND) {
        std::printf("test_ocr_e2e: weights not in cache, skipping (OK)\n");
        naina_image_release(wrapped);
        naina_release(ctx);
        return 0;
    }

    EXPECT(rc == NAINA_OK);
    EXPECT(page != nullptr);
    if (rc != NAINA_OK || page == nullptr) {
        std::fprintf(stderr, "naina_read failed: %s\n", naina_status_str(rc));
        naina_image_release(wrapped);
        naina_release(ctx);
        return 1;
    }

    const naina_textline* lines = nullptr;
    int32_t n = 0;
    EXPECT(naina_page_lines(page, &lines, &n) == NAINA_OK);
    std::printf("test_ocr_e2e: %d line(s) detected\n", n);
    for (int32_t i = 0; i < n; ++i) {
        std::printf("  [%d] conf=%.3f box_score=%.3f  %s\n",
                    i,
                    static_cast<double>(lines[i].confidence),
                    static_cast<double>(lines[i].box.score),
                    lines[i].text);
    }

    const std::string md = naina_page_markdown(page);
    std::printf("--- markdown ---\n%s\n----------------\n", md.c_str());

    // The fixture reads "HELLO WORLD" / "naina 2026".
    EXPECT(n >= 2);
    EXPECT(contains_ci(md, "HELLO"));
    EXPECT(contains_ci(md, "WORLD"));
    EXPECT(contains_ci(md, "naina"));
    EXPECT(contains_ci(md, "2026"));

    // Every emitted line should carry a plausible confidence.
    for (int32_t i = 0; i < n; ++i) {
        if (lines[i].text != nullptr && lines[i].text[0] != '\0') {
            EXPECT(lines[i].confidence > 0.3F);
            EXPECT(lines[i].box.score > 0.3F);
        }
    }

    // JSON must be non-trivial and mention the recognised text.
    const std::string js = naina_page_json(page);
    EXPECT(js.size() > 50);
    EXPECT(js.front() == '{' && js.back() == '}');

    naina_page_release(page);
    naina_image_release(wrapped);
    naina_release(ctx);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_ocr_e2e: all passed\n");
    return 0;
}
