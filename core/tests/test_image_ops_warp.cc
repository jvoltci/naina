// Pure image-op tests: detection resize geometry and quad rectification.
#include "image_ops.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using naina::internal::DetResize;
using naina::internal::ImageView;
using naina::internal::plan_det_resize;
using naina::internal::resize_det_bgr_planar_f32;

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

    // 100x50 is under the limit, so no downscale — but both dims round to
    // the nearest multiple of 32: 100 -> 96, 50 -> 64.
    const DetResize b = plan_det_resize(100, 50, 960, 32);
    EXPECT(b.out_w == 96);
    EXPECT(b.out_h == 64);
    // Scale is per-axis because rounding differs per axis.
    EXPECT(std::fabs(b.scale_x - 0.96F) < 1e-5F);
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

// Build a WxH BGR8 image where every pixel's B/G/R = a known function of
// (x, y), so we can assert what a resample must produce.
static std::vector<uint8_t> make_bgr(int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            px[i + 0] = 10;   // B constant
            px[i + 1] = 128;  // G constant
            px[i + 2] = 250;  // R constant
        }
    }
    return px;
}

static void test_resize_det_normalises_per_channel() {
    const int w = 64;
    const int h = 32;
    auto px = make_bgr(w, h);
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    const DetResize plan = plan_det_resize(w, h, 960, 32);
    EXPECT(plan.out_w == 64);
    EXPECT(plan.out_h == 32);

    const float scale[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    const float mean[3] = {0.485F, 0.456F, 0.406F};
    const float sd[3] = {0.229F, 0.224F, 0.225F};

    std::vector<float> dst(static_cast<size_t>(3) * static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h),
                            -999.0F);
    resize_det_bgr_planar_f32(src, plan, scale, mean, sd, dst.data());

    // Planar layout: channel 0 is the whole first plane.
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    const float want_c0 = (10.0F / 255.0F - mean[0]) / sd[0];
    const float want_c1 = (128.0F / 255.0F - mean[1]) / sd[1];
    const float want_c2 = (250.0F / 255.0F - mean[2]) / sd[2];

    // A constant source must produce a constant output in every plane.
    EXPECT(std::fabs(dst[0] - want_c0) < 1e-4F);
    EXPECT(std::fabs(dst[plane / 2] - want_c0) < 1e-4F);
    EXPECT(std::fabs(dst[plane + 0] - want_c1) < 1e-4F);
    EXPECT(std::fabs(dst[2 * plane + 0] - want_c2) < 1e-4F);

    // Nothing left uninitialised.
    for (float v : dst) {
        EXPECT(v > -900.0F);
    }
}

static void test_resize_det_downscales_dimensions() {
    const int w = 200;
    const int h = 100;
    auto px = make_bgr(w, h);
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    // 200x100 -> rounds to 192x96 (nearest multiples of 32).
    const DetResize plan = plan_det_resize(w, h, 960, 32);
    EXPECT(plan.out_w == 192);
    EXPECT(plan.out_h == 96);

    const float scale[3] = {1.0F, 1.0F, 1.0F};
    const float mean[3] = {0.0F, 0.0F, 0.0F};
    const float sd[3] = {1.0F, 1.0F, 1.0F};
    std::vector<float> dst(static_cast<size_t>(3) * static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h),
                            -999.0F);
    resize_det_bgr_planar_f32(src, plan, scale, mean, sd, dst.data());

    // With identity normalisation the raw channel values survive.
    EXPECT(std::fabs(dst[0] - 10.0F) < 0.5F);
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    EXPECT(std::fabs(dst[plane] - 128.0F) < 0.5F);
    EXPECT(std::fabs(dst[2 * plane] - 250.0F) < 0.5F);
}

int main() {
    test_det_resize_rounds_to_multiple_of_32();
    test_det_resize_is_idempotent_on_aligned_input();
    test_resize_det_normalises_per_channel();
    test_resize_det_downscales_dimensions();
    if (failures == 0) {
        std::printf("test_image_ops_warp: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
