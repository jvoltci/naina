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

void crop_face_rgb_planar_f32(const ImageView& src,
                              const naina_bbox& bbox,
                              float expand,
                              int out_size,
                              const float mean[3],
                              const float scale[3],
                              float* dst) {
    // Square crop centred on bbox, side = max(w, h) * expand.
    const float cx = bbox.x + bbox.w * 0.5F;
    const float cy = bbox.y + bbox.h * 0.5F;
    const float side = std::max(bbox.w, bbox.h) * expand;
    const float half = side * 0.5F;
    const float x0 = cx - half;
    const float y0 = cy - half;

    const size_t plane = static_cast<size_t>(out_size) * static_cast<size_t>(out_size);
    float* pr = dst + 0 * plane;
    float* pg = dst + 1 * plane;
    float* pb = dst + 2 * plane;

    const float step = side / static_cast<float>(out_size);
    for (int uy = 0; uy < out_size; ++uy) {
        const float sy = y0 + (static_cast<float>(uy) + 0.5F) * step;
        for (int ux = 0; ux < out_size; ++ux) {
            const float sx = x0 + (static_cast<float>(ux) + 0.5F) * step;
            const RGB c = sample_rgb_bilinear(src, sx, sy);
            const size_t idx =
                static_cast<size_t>(uy) * static_cast<size_t>(out_size) + static_cast<size_t>(ux);
            pr[idx] = (c.r - mean[0]) / scale[0];
            pg[idx] = (c.g - mean[1]) / scale[1];
            pb[idx] = (c.b - mean[2]) / scale[2];
        }
    }
}

namespace {

// Round `v` to the nearest multiple of `m`, never below `m`.
int32_t round_to_multiple(float v, int32_t m) {
    if (m <= 1) {
        return static_cast<int32_t>(v) > 0 ? static_cast<int32_t>(v) : 1;
    }
    const int32_t n = static_cast<int32_t>(std::lround(v / static_cast<float>(m)));
    return (n < 1 ? 1 : n) * m;
}

}  // namespace

DetResize plan_det_resize(int32_t src_w, int32_t src_h, int32_t limit, int32_t multiple_of) {
    DetResize r{};
    if (src_w <= 0 || src_h <= 0) {
        r.out_w = multiple_of > 0 ? multiple_of : 1;
        r.out_h = r.out_w;
        r.scale_x = 1.0F;
        r.scale_y = 1.0F;
        return r;
    }
    const int32_t longest = src_w > src_h ? src_w : src_h;
    float ratio = 1.0F;
    if (limit > 0 && longest > limit) {
        ratio = static_cast<float>(limit) / static_cast<float>(longest);
    }
    r.out_w = round_to_multiple(static_cast<float>(src_w) * ratio, multiple_of);
    r.out_h = round_to_multiple(static_cast<float>(src_h) * ratio, multiple_of);
    r.scale_x = static_cast<float>(r.out_w) / static_cast<float>(src_w);
    r.scale_y = static_cast<float>(r.out_h) / static_cast<float>(src_h);
    return r;
}

namespace {

// Fetch one channel of one pixel, clamping to the image edge. `ch` is an
// index into the source's native channel order (BGR8 → 0=B, 1=G, 2=R).
float sample_u8(const ImageView& src, int32_t x, int32_t y, int32_t ch) {
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= src.width) {
        x = src.width - 1;
    }
    if (y >= src.height) {
        y = src.height - 1;
    }
    const int32_t nch = (src.fmt == NAINA_PIXFMT_GRAY8) ? 1 : 3;
    const size_t off = static_cast<size_t>(y) * static_cast<size_t>(src.stride) +
                       static_cast<size_t>(x) * static_cast<size_t>(nch);
    // GRAY8 replicates its single channel across all three requested planes.
    const int32_t c = (nch == 1) ? 0 : ch;
    return static_cast<float>(src.data[off + static_cast<size_t>(c)]);
}

// Bilinear sample at continuous (fx, fy).
float bilinear_u8(const ImageView& src, float fx, float fy, int32_t ch) {
    const int32_t x0 = static_cast<int32_t>(std::floor(fx));
    const int32_t y0 = static_cast<int32_t>(std::floor(fy));
    const float ax = fx - static_cast<float>(x0);
    const float ay = fy - static_cast<float>(y0);
    const float p00 = sample_u8(src, x0, y0, ch);
    const float p10 = sample_u8(src, x0 + 1, y0, ch);
    const float p01 = sample_u8(src, x0, y0 + 1, ch);
    const float p11 = sample_u8(src, x0 + 1, y0 + 1, ch);
    const float top = p00 + (p10 - p00) * ax;
    const float bot = p01 + (p11 - p01) * ax;
    return top + (bot - top) * ay;
}

}  // namespace

void resize_det_bgr_planar_f32(const ImageView& src,
                               const DetResize& plan,
                               const float scale[3],
                               const float mean[3],
                               const float std_[3],
                               float* dst) {
    if (src.data == nullptr || dst == nullptr || plan.out_w <= 0 || plan.out_h <= 0) {
        return;
    }
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    // Map destination pixel centres back into source space.
    const float inv_x = 1.0F / (plan.scale_x != 0.0F ? plan.scale_x : 1.0F);
    const float inv_y = 1.0F / (plan.scale_y != 0.0F ? plan.scale_y : 1.0F);

    for (int32_t ch = 0; ch < 3; ++ch) {
        const float s = scale[ch];
        const float m = mean[ch];
        const float d = (std_[ch] != 0.0F) ? std_[ch] : 1.0F;
        float* out = dst + static_cast<size_t>(ch) * plane;
        for (int32_t y = 0; y < plan.out_h; ++y) {
            const float fy = (static_cast<float>(y) + 0.5F) * inv_y - 0.5F;
            for (int32_t x = 0; x < plan.out_w; ++x) {
                const float fx = (static_cast<float>(x) + 0.5F) * inv_x - 0.5F;
                const float raw = bilinear_u8(src, fx, fy, ch);
                out[static_cast<size_t>(y) * static_cast<size_t>(plan.out_w) + static_cast<size_t>(x)] =
                    (raw * s - m) / d;
            }
        }
    }
}

namespace {

float dist(const naina_point& a, const naina_point& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

QuadStrip plan_quad_strip(const naina_point corners[4], int32_t height, int32_t max_width) {
    QuadStrip s{};
    s.height = height > 0 ? height : 48;

    // PaddleOCR's get_rotate_crop_image: take the longer of each opposing
    // edge pair, so a slightly skewed quad is not undersized.
    const float top = dist(corners[0], corners[1]);
    const float bottom = dist(corners[3], corners[2]);
    const float left = dist(corners[0], corners[3]);
    const float right = dist(corners[1], corners[2]);
    float quad_w = top > bottom ? top : bottom;
    float quad_h = left > right ? left : right;

    // A quad markedly taller than wide is vertical text: rotate so the
    // reading direction runs along the strip's width.
    s.rotate90 = (quad_w > 0.0F) && (quad_h / quad_w >= 1.5F);
    if (s.rotate90) {
        const float t = quad_w;
        quad_w = quad_h;
        quad_h = t;
    }

    if (quad_h <= 0.0F || quad_w <= 0.0F) {
        s.width = 1;
        return s;
    }
    const float aspect = quad_w / quad_h;
    int32_t w = static_cast<int32_t>(std::lround(static_cast<float>(s.height) * aspect));
    if (w < 1) {
        w = 1;
    }
    if (max_width > 0 && w > max_width) {
        w = max_width;
    }
    s.width = w;
    return s;
}

void warp_quad_bgr_planar_f32(const ImageView& src,
                              const naina_point corners[4],
                              const QuadStrip& plan,
                              const float scale[3],
                              const float mean[3],
                              const float std_[3],
                              float* dst) {
    if (src.data == nullptr || dst == nullptr || plan.width <= 0 || plan.height <= 0) {
        return;
    }
    // Order the quad so corner 0 maps to the strip's top-left. When the quad
    // is vertical we start from corner 3, which rotates the sampling frame
    // by 90 degrees without a second pass over the pixels.
    naina_point c[4];
    for (int32_t i = 0; i < 4; ++i) {
        c[i] = corners[plan.rotate90 ? ((i + 3) % 4) : i];
    }

    const size_t plane = static_cast<size_t>(plan.width) * static_cast<size_t>(plan.height);
    const float fw = static_cast<float>(plan.width);
    const float fh = static_cast<float>(plan.height);

    for (int32_t y = 0; y < plan.height; ++y) {
        // v, u in [0,1] across the strip; bilinear blend of the four corners
        // is the exact inverse map for a planar quad.
        const float v = (static_cast<float>(y) + 0.5F) / fh;
        for (int32_t x = 0; x < plan.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / fw;
            const float top_x = c[0].x + (c[1].x - c[0].x) * u;
            const float top_y = c[0].y + (c[1].y - c[0].y) * u;
            const float bot_x = c[3].x + (c[2].x - c[3].x) * u;
            const float bot_y = c[3].y + (c[2].y - c[3].y) * u;
            const float sx = top_x + (bot_x - top_x) * v;
            const float sy = top_y + (bot_y - top_y) * v;

            for (int32_t ch = 0; ch < 3; ++ch) {
                const float d = (std_[ch] != 0.0F) ? std_[ch] : 1.0F;
                const float raw = bilinear_u8(src, sx, sy, ch);
                dst[static_cast<size_t>(ch) * plane +
                    static_cast<size_t>(y) * static_cast<size_t>(plan.width) + static_cast<size_t>(x)] =
                    (raw * scale[ch] - mean[ch]) / d;
            }
        }
    }
}

}  // namespace naina::internal
