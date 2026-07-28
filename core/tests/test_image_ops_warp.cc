// Pure image-op tests: detection resize geometry and quad rectification.
#include "image_ops.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::DetResize;
using naina::internal::ImageView;
using naina::internal::plan_det_resize;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_det_resize_rounds_to_multiple_of_32() {
    // A 1000x500 image: longest side 1000 > 960, so scale = 0.96.
    // 1000*0.96 = 960 (already /32), 500*0.96 = 480 (already /32).
    const DetResize a = plan_det_resize(1000, 500, 960, 32);
    EXPECT(a.out_w == 960);
    EXPECT(a.out_h == 480);
    EXPECT(std::fabs(a.scale_x - 0.96F) < 1e-5F);
    EXPECT(std::fabs(a.scale_y - 0.96F) < 1e-5F);

    // 100x50 is under the limit, so no downscale — but both dims round UP
    // to the next multiple of 32: 100 -> 128, 50 -> 64.
    const DetResize b = plan_det_resize(100, 50, 960, 32);
    EXPECT(b.out_w == 128);
    EXPECT(b.out_h == 64);
    // Scale is per-axis because rounding differs per axis.
    EXPECT(std::fabs(b.scale_x - 1.28F) < 1e-5F);
    EXPECT(std::fabs(b.scale_y - 1.28F) < 1e-5F);

    // Never collapse to zero.
    const DetResize c = plan_det_resize(3, 1, 960, 32);
    EXPECT(c.out_w == 32);
    EXPECT(c.out_h == 32);

    // A very tall image clamps on height.
    const DetResize d = plan_det_resize(200, 4000, 960, 32);
    EXPECT(d.out_h == 960);
    EXPECT(d.out_w % 32 == 0);
    EXPECT(d.out_w > 0);
}

static void test_det_resize_is_idempotent_on_aligned_input() {
    // 640x320 is already aligned and under the limit — nothing should move.
    const DetResize r = plan_det_resize(640, 320, 960, 32);
    EXPECT(r.out_w == 640);
    EXPECT(r.out_h == 320);
    EXPECT(std::fabs(r.scale_x - 1.0F) < 1e-6F);
    EXPECT(std::fabs(r.scale_y - 1.0F) < 1e-6F);
}

int main() {
    test_det_resize_rounds_to_multiple_of_32();
    test_det_resize_is_idempotent_on_aligned_input();
    if (failures == 0) {
        std::printf("test_image_ops_warp: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
