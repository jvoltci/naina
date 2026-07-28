// DBNet post-processing: bitmap -> blob borders -> quads.
#include "modules/db_postprocess.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using naina::internal::db_postprocess::Bitmap;
using naina::internal::db_postprocess::box_score;
using naina::internal::db_postprocess::Config;
using naina::internal::db_postprocess::decode;
using naina::internal::db_postprocess::find_blob_borders;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Build a bitmap from an ASCII picture: '#' is foreground.
static Bitmap from_ascii(const std::vector<const char*>& rows) {
    Bitmap bm;
    bm.height = static_cast<int32_t>(rows.size());
    bm.width = 0;
    for (const char* r : rows) {
        int32_t len = 0;
        while (r[len] != '\0') {
            ++len;
        }
        if (len > bm.width) {
            bm.width = len;
        }
    }
    bm.px.assign(static_cast<size_t>(bm.width) * static_cast<size_t>(bm.height), 0);
    for (int32_t y = 0; y < bm.height; ++y) {
        const char* r = rows[static_cast<size_t>(y)];
        for (int32_t x = 0; r[x] != '\0'; ++x) {
            if (r[x] == '#') {
                bm.px[static_cast<size_t>(y) * static_cast<size_t>(bm.width) + static_cast<size_t>(x)] = 1;
            }
        }
    }
    return bm;
}

static void test_single_rectangle_blob() {
    const Bitmap bm = from_ascii({
        "........",
        ".####...",
        ".####...",
        "........",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
    if (blobs.empty()) {
        return;
    }
    // Every border pixel must be inside the 4x2 block at x in [1,4], y in [1,2].
    for (const auto& p : blobs[0]) {
        EXPECT(p.x >= 1.0F && p.x <= 4.0F);
        EXPECT(p.y >= 1.0F && p.y <= 2.0F);
    }
    // A 4x2 block is all border: 8 pixels (verified by simulation — with
    // only 2 rows, every pixel has a vertical neighbour outside the block).
    EXPECT(blobs[0].size() >= 6);
}

static void test_two_separate_blobs() {
    const Bitmap bm = from_ascii({
        "##...##",
        "##...##",
        ".......",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 2);
}

static void test_diagonally_touching_blobs_are_one() {
    // 8-connectivity: these two squares touch at a corner and count as one.
    const Bitmap bm = from_ascii({
        "##...",
        "##...",
        "..##.",
        "..##.",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
}

static void test_single_pixel_blob() {
    const Bitmap bm = from_ascii({
        ".....",
        "..#..",
        ".....",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
    if (!blobs.empty()) {
        EXPECT(blobs[0].size() == 1);
    }
}

static void test_empty_bitmap_yields_nothing() {
    const Bitmap bm = from_ascii({"....", "....."});
    EXPECT(find_blob_borders(bm, 3000).empty());
}

static void test_max_candidates_caps_output() {
    // A checkerboard of isolated pixels: many blobs, capped at 3. Verified by
    // simulation that even (x,y) pixels are never 8-adjacent to each other
    // (min offset is 2 in at least one axis), so this really yields 100
    // separate single-pixel blobs, not fewer.
    Bitmap bm;
    bm.width = 20;
    bm.height = 20;
    bm.px.assign(static_cast<size_t>(bm.width) * static_cast<size_t>(bm.height), 0);
    for (int32_t y = 0; y < bm.height; y += 2) {
        for (int32_t x = 0; x < bm.width; x += 2) {
            bm.px[static_cast<size_t>(y) * static_cast<size_t>(bm.width) + static_cast<size_t>(x)] = 1;
        }
    }
    const auto blobs = find_blob_borders(bm, 3);
    EXPECT(blobs.size() == 3);
}

// A probability map with a single high-confidence rectangle of text.
static std::vector<float> prob_with_rect(
    int32_t w, int32_t h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, float v) {
    std::vector<float> p(static_cast<size_t>(w) * static_cast<size_t>(h), 0.02F);
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            p[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = v;
        }
    }
    return p;
}

static void test_box_score_averages_probability_inside() {
    const int32_t w = 20;
    const int32_t h = 20;
    const auto p = prob_with_rect(w, h, 5, 5, 14, 10, 0.9F);
    naina_point quad[4] = {{5, 5}, {14, 5}, {14, 10}, {5, 10}};
    const float s = box_score(p.data(), w, h, quad);
    EXPECT(s > 0.85F && s <= 1.0F);

    // A quad over background must score near zero.
    naina_point empty[4] = {{16, 16}, {19, 16}, {19, 19}, {16, 19}};
    EXPECT(box_score(p.data(), w, h, empty) < 0.1F);
}

static void test_decode_finds_one_box_and_unclips_it() {
    const int32_t w = 60;
    const int32_t h = 40;
    // Text block from (10,10) to (39,19): 30 wide, 10 tall.
    const auto p = prob_with_rect(w, h, 10, 10, 39, 19, 0.95F);

    Config cfg;
    cfg.thresh = 0.2F;
    cfg.box_thresh = 0.4F;
    cfg.unclip_ratio = 1.4F;
    cfg.min_box_side = 3.0F;

    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.size() == 1);
    if (boxes.empty()) {
        return;
    }
    EXPECT(boxes[0].score > 0.8F);

    // Unclip must have grown the box beyond the raw 30x10 extent.
    float minx = 1e9F, maxx = -1e9F, miny = 1e9F, maxy = -1e9F;
    for (const auto& c : boxes[0].corners) {
        minx = c.x < minx ? c.x : minx;
        maxx = c.x > maxx ? c.x : maxx;
        miny = c.y < miny ? c.y : miny;
        maxy = c.y > maxy ? c.y : maxy;
    }
    EXPECT(minx < 10.0F);
    EXPECT(maxx > 39.0F);
    EXPECT(miny < 10.0F);
    EXPECT(maxy > 19.0F);
    // Not absurdly, either. The min-area quad's corner-to-corner span is
    // 29x9 (inclusive pixel coords 10..39 and 10..19 span 29 and 9, not the
    // pixel counts 30 and 10), so area=261, perimeter=76, and
    // distance = 261*1.4/76 = ~4.81 px per side — verified by simulating
    // the exact hull/min-area-quad/unclip pipeline in Python. That number,
    // not the naively-counted 5.25, is what bounds these two checks, but
    // both are well inside [0, 59] so either estimate would pass.
    EXPECT(minx > 0.0F);
    EXPECT(maxx < 59.0F);
}

static void test_decode_rejects_low_confidence_blobs() {
    const int32_t w = 40;
    const int32_t h = 30;
    // Above the binarisation threshold (0.2) but below box_thresh (0.4).
    const auto p = prob_with_rect(w, h, 5, 5, 24, 14, 0.3F);
    Config cfg;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.empty());
}

static void test_decode_rejects_slivers() {
    const int32_t w = 40;
    const int32_t h = 30;
    // A 20x1 sliver: below min_box_side of 3. (In practice this is a
    // 1-pixel-tall row of border points, which are all collinear, so
    // min_area_quad's internal convex_hull collapses to 2 points and it
    // returns false before the min_box_side check even runs — either path
    // yields the same observable result: no box.)
    const auto p = prob_with_rect(w, h, 5, 10, 24, 10, 0.95F);
    Config cfg;
    cfg.min_box_side = 3.0F;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.empty());
}

static void test_decode_finds_two_blocks() {
    const int32_t w = 80;
    const int32_t h = 40;
    auto p = prob_with_rect(w, h, 5, 5, 30, 16, 0.95F);
    const auto q = prob_with_rect(w, h, 45, 5, 70, 16, 0.95F);
    for (size_t i = 0; i < p.size(); ++i) {
        p[i] = p[i] > q[i] ? p[i] : q[i];
    }
    Config cfg;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.size() == 2);
}

static void test_decode_empty_map_is_empty() {
    const int32_t w = 20;
    const int32_t h = 20;
    const std::vector<float> p(static_cast<size_t>(w) * static_cast<size_t>(h), 0.01F);
    Config cfg;
    EXPECT(decode(p.data(), w, h, cfg).empty());
    EXPECT(decode(nullptr, w, h, cfg).empty());
}

int main() {
    test_single_rectangle_blob();
    test_two_separate_blobs();
    test_diagonally_touching_blobs_are_one();
    test_single_pixel_blob();
    test_empty_bitmap_yields_nothing();
    test_max_candidates_caps_output();
    test_box_score_averages_probability_inside();
    test_decode_finds_one_box_and_unclips_it();
    test_decode_rejects_low_confidence_blobs();
    test_decode_rejects_slivers();
    test_decode_finds_two_blocks();
    test_decode_empty_map_is_empty();
    if (failures == 0) {
        std::printf("test_db_postprocess: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
