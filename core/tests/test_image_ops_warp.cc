// Pure image-op tests: detection resize geometry and quad rectification.
#include "image_ops.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using naina::internal::DetResize;
using naina::internal::ImageView;
using naina::internal::plan_det_resize;
using naina::internal::plan_quad_strip;
using naina::internal::QuadStrip;
using naina::internal::resize_det_bgr_planar_f32;
using naina::internal::shrink_filter_from_string;
using naina::internal::ShrinkFilter;

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
            const size_t i =
                (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
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

    std::vector<float> dst(
        static_cast<size_t>(3) * static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h),
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
    std::vector<float> dst(
        static_cast<size_t>(3) * static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h),
        -999.0F);
    resize_det_bgr_planar_f32(src, plan, scale, mean, sd, dst.data());

    // With identity normalisation the raw channel values survive.
    EXPECT(std::fabs(dst[0] - 10.0F) < 0.5F);
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    EXPECT(std::fabs(dst[plane] - 128.0F) < 0.5F);
    EXPECT(std::fabs(dst[2 * plane] - 250.0F) < 0.5F);
}

static void test_quad_strip_sizing_from_edge_lengths() {
    // An axis-aligned 80x20 quad: 4x wider than tall.
    naina_point q[4];
    q[0] = {10.0F, 10.0F};
    q[1] = {90.0F, 10.0F};
    q[2] = {90.0F, 30.0F};
    q[3] = {10.0F, 30.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.height == 48);
    // aspect 80/20 = 4 -> width = 48*4 = 192
    EXPECT(s.width == 192);
    EXPECT(!s.rotate90);
}

static void test_quad_strip_rotates_tall_quads() {
    // A 20x100 quad: 5x taller than wide, so it is vertical text.
    naina_point q[4];
    q[0] = {0.0F, 0.0F};
    q[1] = {20.0F, 0.0F};
    q[2] = {20.0F, 100.0F};
    q[3] = {0.0F, 100.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.rotate90);
    EXPECT(s.height == 48);
    // After rotation the long side (100) becomes the width: 48*(100/20)=240
    EXPECT(s.width == 240);
}

static void test_quad_strip_clamps_max_width() {
    // An absurdly wide quad must not produce an unbounded tensor.
    naina_point q[4];
    q[0] = {0.0F, 0.0F};
    q[1] = {10000.0F, 0.0F};
    q[2] = {10000.0F, 10.0F};
    q[3] = {0.0F, 10.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.width == 1200);
    EXPECT(s.height == 48);
}

static void test_quad_strip_degenerate_quad_is_safe() {
    naina_point q[4];
    for (auto& p : q) {
        p = {5.0F, 5.0F};
    }
    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.width >= 1);
    EXPECT(s.height == 48);
}

static void test_warp_quad_extracts_the_right_pixels() {
    // 100x40 image, left half B=0, right half B=200. Warp the right half
    // and confirm we sampled the bright side.
    const int w = 100;
    const int h = 40;
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 3, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i =
                (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            const uint8_t v = (x >= w / 2) ? 200 : 0;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
        }
    }
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    naina_point q[4];
    q[0] = {50.0F, 5.0F};
    q[1] = {99.0F, 5.0F};
    q[2] = {99.0F, 35.0F};
    q[3] = {50.0F, 35.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    const float scale[3] = {1.0F, 1.0F, 1.0F};
    const float mean[3] = {0.0F, 0.0F, 0.0F};
    const float sd[3] = {1.0F, 1.0F, 1.0F};
    std::vector<float> dst(
        static_cast<size_t>(3) * static_cast<size_t>(s.width) * static_cast<size_t>(s.height),
        -999.0F);
    naina::internal::warp_quad_bgr_planar_f32(src, q, s, scale, mean, sd, dst.data());

    // Every sample came from the bright half.
    const size_t plane = static_cast<size_t>(s.width) * static_cast<size_t>(s.height);
    for (size_t i = 0; i < plane; ++i) {
        EXPECT(dst[i] > 150.0F);
    }
}

// A 4x shrink must average the area it covers, not read every fourth pixel.
// Columns repeat 0, 0, 0, 255: the area mean is 63.75; plain bilinear at the
// mapped centre (source x = 1.5) read the two zero columns and returned 0,
// which is how the medium detector lost whole newspaper pages (2026-09-23).
static void test_resize_det_shrink_is_an_area_average() {
    const int32_t W = 16, H = 8;
    std::vector<uint8_t> px(static_cast<size_t>(W * H * 3));
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            const uint8_t v = (x % 4 == 3) ? 255 : 0;
            for (int32_t c = 0; c < 3; ++c) {
                px[static_cast<size_t>((y * W + x) * 3 + c)] = v;
            }
        }
    }
    const ImageView src{px.data(), W, H, W * 3, NAINA_PIXFMT_BGR8};
    DetResize plan{};
    plan.out_w = 4;
    plan.out_h = 2;
    plan.scale_x = 0.25F;
    plan.scale_y = 0.25F;
    const float one[3] = {1.0F, 1.0F, 1.0F};
    const float zero[3] = {0.0F, 0.0F, 0.0F};
    std::vector<float> out(3U * 4U * 2U, -1.0F);
    resize_det_bgr_planar_f32(src, plan, one, zero, one, out.data());
    for (float v : out) {
        EXPECT(std::fabs(v - 63.75F) < 1e-3F);
    }
}

// Shrinking by a non-integer factor splits boundary pixels by exact overlap:
// 3 -> 2 in x covers [0, 1.5) and [1.5, 3), so with columns 0, 90, 180 the
// outputs are (0 + 0.5*90) / 1.5 = 30 and (0.5*90 + 180) / 1.5 = 150.
static void test_resize_det_fractional_shrink_splits_boundary_pixels() {
    const uint8_t px[9] = {0, 0, 0, 90, 90, 90, 180, 180, 180};
    const ImageView src{px, 3, 1, 9, NAINA_PIXFMT_BGR8};
    DetResize plan{};
    plan.out_w = 2;
    plan.out_h = 1;
    plan.scale_x = 2.0F / 3.0F;
    plan.scale_y = 1.0F;
    const float one[3] = {1.0F, 1.0F, 1.0F};
    const float zero[3] = {0.0F, 0.0F, 0.0F};
    float out[6] = {0};
    resize_det_bgr_planar_f32(src, plan, one, zero, one, out);
    for (int32_t c = 0; c < 3; ++c) {
        EXPECT(std::fabs(out[c * 2 + 0] - 30.0F) < 1e-3F);
        EXPECT(std::fabs(out[c * 2 + 1] - 150.0F) < 1e-3F);
    }
}

// Enlarging keeps the old bilinear-at-centre behaviour, edge-clamped: a
// 2-pixel row 0, 100 stretched to 4 gives 0, 25, 75, 100. GRAY8 feeds its one
// channel to all three planes, and mixing axes (x up, y down) works.
static void test_resize_det_enlarge_is_bilinear_and_gray_replicates() {
    const uint8_t px[4] = {0, 100, 0, 100};  // 2 wide, 2 tall, identical rows
    const ImageView src{px, 2, 2, 2, NAINA_PIXFMT_GRAY8};
    DetResize plan{};
    plan.out_w = 4;
    plan.out_h = 1;
    plan.scale_x = 2.0F;
    plan.scale_y = 0.5F;
    const float one[3] = {1.0F, 1.0F, 1.0F};
    const float zero[3] = {0.0F, 0.0F, 0.0F};
    float out[12] = {0};
    resize_det_bgr_planar_f32(src, plan, one, zero, one, out);
    const float want[4] = {0.0F, 25.0F, 75.0F, 100.0F};
    for (int32_t c = 0; c < 3; ++c) {
        for (int32_t x = 0; x < 4; ++x) {
            EXPECT(std::fabs(out[c * 4 + x] - want[x]) < 1e-3F);
        }
    }
}

// The old behaviour is still reachable by name, so a tier that measures better
// on it can ask for it: under Bilinear the 0, 0, 0, 255 stripe shrunk 4x reads
// the two zero columns at source x = 1.5 and returns 0, the aliasing that
// emptied the newspaper pages.
static void test_resize_det_bilinear_filter_reproduces_the_old_aliasing() {
    const int32_t W = 16, H = 8;
    std::vector<uint8_t> px(static_cast<size_t>(W * H * 3));
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            const uint8_t v = (x % 4 == 3) ? 255 : 0;
            for (int32_t c = 0; c < 3; ++c) {
                px[static_cast<size_t>((y * W + x) * 3 + c)] = v;
            }
        }
    }
    const ImageView src{px.data(), W, H, W * 3, NAINA_PIXFMT_BGR8};
    DetResize plan{};
    plan.out_w = 4;
    plan.out_h = 2;
    plan.scale_x = 0.25F;
    plan.scale_y = 0.25F;
    const float one[3] = {1.0F, 1.0F, 1.0F};
    const float zero[3] = {0.0F, 0.0F, 0.0F};
    std::vector<float> out(3U * 4U * 2U, -1.0F);
    resize_det_bgr_planar_f32(src, plan, one, zero, one, out.data(), ShrinkFilter::Bilinear);
    for (float v : out) {
        EXPECT(std::fabs(v) < 1e-3F);
    }
    // Lanczos3 keeps the mean of a periodic pattern close to the area mean
    // (63.75) but not equal: it is a different filter, and the difference is
    // the point of measuring them against each other.
    resize_det_bgr_planar_f32(src, plan, one, zero, one, out.data(), ShrinkFilter::Lanczos3);
    EXPECT(std::fabs(out[5] - 63.75F) < 20.0F);
    EXPECT(std::fabs(out[5]) > 1e-3F);

    EXPECT(shrink_filter_from_string("area", ShrinkFilter::Bilinear) == ShrinkFilter::Area);
    EXPECT(shrink_filter_from_string("bilinear", ShrinkFilter::Area) == ShrinkFilter::Bilinear);
    EXPECT(shrink_filter_from_string("lanczos3", ShrinkFilter::Area) == ShrinkFilter::Lanczos3);
    EXPECT(shrink_filter_from_string("triangle", ShrinkFilter::Area) == ShrinkFilter::Triangle);
    EXPECT(shrink_filter_from_string("", ShrinkFilter::Area) == ShrinkFilter::Area);
    EXPECT(shrink_filter_from_string(nullptr, ShrinkFilter::Lanczos3) == ShrinkFilter::Lanczos3);
    EXPECT(shrink_filter_from_string("nearest", ShrinkFilter::Area) == ShrinkFilter::Area);
}

int main() {
    test_det_resize_rounds_to_multiple_of_32();
    test_det_resize_is_idempotent_on_aligned_input();
    test_resize_det_normalises_per_channel();
    test_resize_det_downscales_dimensions();
    test_quad_strip_sizing_from_edge_lengths();
    test_quad_strip_rotates_tall_quads();
    test_quad_strip_clamps_max_width();
    test_quad_strip_degenerate_quad_is_safe();
    test_warp_quad_extracts_the_right_pixels();
    test_resize_det_shrink_is_an_area_average();
    test_resize_det_fractional_shrink_splits_boundary_pixels();
    test_resize_det_enlarge_is_bilinear_and_gray_replicates();
    test_resize_det_bilinear_filter_reproduces_the_old_aliasing();
    if (failures == 0) {
        std::printf("test_image_ops_warp: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
