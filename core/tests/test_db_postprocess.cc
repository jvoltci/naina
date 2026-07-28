// DBNet post-processing: bitmap -> blob borders -> quads.
#include "modules/db_postprocess.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::db_postprocess::Bitmap;
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

int main() {
    test_single_rectangle_blob();
    test_two_separate_blobs();
    test_diagonally_touching_blobs_are_one();
    test_single_pixel_blob();
    test_empty_bitmap_yields_nothing();
    test_max_candidates_caps_output();
    if (failures == 0) {
        std::printf("test_db_postprocess: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
