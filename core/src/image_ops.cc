#include "image_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace naina::internal {

namespace {

// Read one RGB sample at (x, y) from the source view, clamping to bounds.
// Returns the 3 channels as (r, g, b) in [0,255] regardless of source format.
struct RGB {
    float r;
    float g;
    float b;
};

inline RGB sample_rgb_nearest(const ImageView& src, int x, int y) {
    x = std::clamp(x, 0, src.width - 1);
    y = std::clamp(y, 0, src.height - 1);
    const uint8_t* row = src.data + static_cast<size_t>(y) * static_cast<size_t>(src.stride);
    switch (src.fmt) {
        case NAINA_PIXFMT_RGB8: {
            const uint8_t* p = row + x * 3;
            return {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
        }
        case NAINA_PIXFMT_BGR8: {
            const uint8_t* p = row + x * 3;
            return {static_cast<float>(p[2]), static_cast<float>(p[1]), static_cast<float>(p[0])};
        }
        case NAINA_PIXFMT_GRAY8: {
            const uint8_t* p = row + x;
            const float v = static_cast<float>(*p);
            return {v, v, v};
        }
        case NAINA_PIXFMT_NV12:
        case NAINA_PIXFMT_YUV420P:
            // Not supported in v1; caller should convert first.
            return {0, 0, 0};
    }
    return {0, 0, 0};
}

inline RGB sample_rgb_bilinear(const ImageView& src, float fx, float fy) {
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float dx = fx - static_cast<float>(x0);
    const float dy = fy - static_cast<float>(y0);
    const RGB p00 = sample_rgb_nearest(src, x0, y0);
    const RGB p10 = sample_rgb_nearest(src, x0 + 1, y0);
    const RGB p01 = sample_rgb_nearest(src, x0, y0 + 1);
    const RGB p11 = sample_rgb_nearest(src, x0 + 1, y0 + 1);
    const float w00 = (1 - dx) * (1 - dy);
    const float w10 = dx * (1 - dy);
    const float w01 = (1 - dx) * dy;
    const float w11 = dx * dy;
    return {p00.r * w00 + p10.r * w10 + p01.r * w01 + p11.r * w11,
            p00.g * w00 + p10.g * w10 + p01.g * w01 + p11.g * w11,
            p00.b * w00 + p10.b * w10 + p01.b * w01 + p11.b * w11};
}

}  // namespace

Letterbox letterbox_to_bgr_planar_f32(const ImageView& src, int size, float pad_value, float* dst) {
    Letterbox lb{};
    lb.src_w = src.width;
    lb.src_h = src.height;
    const int max_side = std::max(src.width, src.height);
    lb.scale = static_cast<float>(size) / static_cast<float>(max_side);
    const int new_w = static_cast<int>(std::round(static_cast<float>(src.width) * lb.scale));
    const int new_h = static_cast<int>(std::round(static_cast<float>(src.height) * lb.scale));
    lb.pad_x = (size - new_w) / 2;
    lb.pad_y = (size - new_h) / 2;

    const size_t plane = static_cast<size_t>(size) * static_cast<size_t>(size);
    float* plane_b = dst + 0 * plane;
    float* plane_g = dst + 1 * plane;
    float* plane_r = dst + 2 * plane;

    // Pad region
    for (size_t i = 0; i < plane; ++i) {
        plane_b[i] = pad_value;
        plane_g[i] = pad_value;
        plane_r[i] = pad_value;
    }

    // Forward-resample the source into the padded box.
    const float inv_scale = 1.0F / lb.scale;
    for (int dy = 0; dy < new_h; ++dy) {
        const float sy = (static_cast<float>(dy) + 0.5F) * inv_scale - 0.5F;
        const size_t row_off = static_cast<size_t>(lb.pad_y + dy) * static_cast<size_t>(size);
        for (int dx = 0; dx < new_w; ++dx) {
            const float sx = (static_cast<float>(dx) + 0.5F) * inv_scale - 0.5F;
            const RGB c = sample_rgb_bilinear(src, sx, sy);
            const size_t idx = row_off + static_cast<size_t>(lb.pad_x + dx);
            plane_b[idx] = c.b;
            plane_g[idx] = c.g;
            plane_r[idx] = c.r;
        }
    }

    return lb;
}

// ── 5pt similarity warp ──────────────────────────────────────────────
// Canonical reference points for 112x112 ArcFace/SFace/EdgeFace alignment.
static constexpr std::array<float, 10> kRefKps112 = {38.2946F,
                                                     51.6963F,
                                                     73.5318F,
                                                     51.5014F,
                                                     56.0252F,
                                                     71.7366F,
                                                     41.5493F,
                                                     92.3655F,
                                                     70.7299F,
                                                     92.2041F};

namespace {

// Solve similarity transform: src_landmarks → ref_landmarks. Returns
// (a, b, e, f) where the inverse (ref → src) sample mapping is:
//   sx = a*ux - b*uy + e
//   sy = b*ux + a*uy + f
struct Similarity {
    float a, b, e, f;
};

Similarity solve_similarity(const naina_point src[5], const float ref[10]) {
    float mxs = 0, mys = 0, mxd = 0, myd = 0;
    for (int i = 0; i < 5; ++i) {
        mxs += src[i].x;
        mys += src[i].y;
        mxd += ref[i * 2];
        myd += ref[i * 2 + 1];
    }
    mxs /= 5;
    mys /= 5;
    mxd /= 5;
    myd /= 5;

    // We want a transform T mapping src → ref (i.e. dst = T(src)).
    // Then to draw the warped face we sample the source at T^{-1}(u, v) for each
    // output pixel. Inverse of a similarity (a + bi) is its conjugate / |.|^2.
    float num_re = 0, num_im = 0, den = 0;
    for (int i = 0; i < 5; ++i) {
        const float px = src[i].x - mxs;
        const float py = src[i].y - mys;
        const float qx = ref[i * 2] - mxd;
        const float qy = ref[i * 2 + 1] - myd;
        num_re += px * qx + py * qy;
        num_im += px * qy - py * qx;
        den += px * px + py * py;
    }
    Similarity fwd{};
    if (den == 0) {
        fwd = {1, 0, 0, 0};
    } else {
        fwd.a = num_re / den;
        fwd.b = num_im / den;
        fwd.e = mxd - (fwd.a * mxs - fwd.b * mys);
        fwd.f = myd - (fwd.b * mxs + fwd.a * mys);
    }
    // Invert: (a + bi)^-1 = (a - bi)/(a^2+b^2); then translate by -fwd.e/f.
    const float nrm = fwd.a * fwd.a + fwd.b * fwd.b;
    Similarity inv{};
    inv.a = fwd.a / nrm;
    inv.b = -fwd.b / nrm;
    inv.e = -(inv.a * fwd.e - inv.b * fwd.f);
    inv.f = -(inv.b * fwd.e + inv.a * fwd.f);
    return inv;
}

}  // namespace

void warp_5pt_rgb_planar_f32(const ImageView& src,
                             const naina_point landmarks[5],
                             int out_size,
                             const float mean[3],
                             const float scale[3],
                             float* dst) {
    // Scale reference points if caller wants something other than 112.
    std::array<float, 10> ref = kRefKps112;
    if (out_size != 112) {
        const float k = static_cast<float>(out_size) / 112.0F;
        for (auto& v : ref) {
            v *= k;
        }
    }

    const Similarity inv = solve_similarity(landmarks, ref.data());

    const size_t plane = static_cast<size_t>(out_size) * static_cast<size_t>(out_size);
    float* pr = dst + 0 * plane;
    float* pg = dst + 1 * plane;
    float* pb = dst + 2 * plane;

    for (int uy = 0; uy < out_size; ++uy) {
        for (int ux = 0; ux < out_size; ++ux) {
            const float sx =
                inv.a * static_cast<float>(ux) - inv.b * static_cast<float>(uy) + inv.e;
            const float sy =
                inv.b * static_cast<float>(ux) + inv.a * static_cast<float>(uy) + inv.f;
            const RGB c = sample_rgb_bilinear(src, sx, sy);
            const size_t idx =
                static_cast<size_t>(uy) * static_cast<size_t>(out_size) + static_cast<size_t>(ux);
            pr[idx] = (c.r - mean[0]) / scale[0];
            pg[idx] = (c.g - mean[1]) / scale[1];
            pb[idx] = (c.b - mean[2]) / scale[2];
        }
    }
}

}  // namespace naina::internal
