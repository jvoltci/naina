#include "db_postprocess.hpp"

#include "../geometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace naina::internal::db_postprocess {

Bitmap binarize(const float* prob, int32_t width, int32_t height, float thresh) {
    Bitmap bm;
    bm.width = width;
    bm.height = height;
    if (prob == nullptr || width <= 0 || height <= 0) {
        bm.width = 0;
        bm.height = 0;
        return bm;
    }
    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    bm.px.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        bm.px[i] = prob[i] > thresh ? 1 : 0;
    }
    return bm;
}

std::vector<std::vector<naina_point>> find_blob_borders(const Bitmap& bm,
                                                        int32_t max_candidates) {
    std::vector<std::vector<naina_point>> blobs;
    if (bm.width <= 0 || bm.height <= 0 || bm.px.empty()) {
        return blobs;
    }
    const int32_t w = bm.width;
    const int32_t h = bm.height;
    std::vector<uint8_t> seen(bm.px.size(), 0);

    auto fg = [&](int32_t x, int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               bm.px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] != 0;
    };

    // 8-connected neighbour offsets.
    static const int32_t dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int32_t dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    std::vector<int32_t> stack;
    for (int32_t y = 0; y < h && static_cast<int32_t>(blobs.size()) < max_candidates; ++y) {
        for (int32_t x = 0; x < w && static_cast<int32_t>(blobs.size()) < max_candidates; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            if (bm.px[idx] == 0 || seen[idx] != 0) {
                continue;
            }
            // Flood-fill this blob, collecting only its border pixels. A
            // foreground pixel is a border pixel when any 8-neighbour is
            // background or outside the image.
            std::vector<naina_point> border;
            stack.clear();
            stack.push_back(static_cast<int32_t>(idx));
            seen[idx] = 1;
            while (!stack.empty()) {
                const int32_t cur = stack.back();
                stack.pop_back();
                const int32_t cy = cur / w;
                const int32_t cx = cur % w;
                bool is_border = false;
                for (int32_t k = 0; k < 8; ++k) {
                    if (!fg(cx + dx8[k], cy + dy8[k])) {
                        is_border = true;
                        break;
                    }
                }
                if (is_border) {
                    border.push_back(naina_point{static_cast<float>(cx), static_cast<float>(cy)});
                }
                for (int32_t k = 0; k < 8; ++k) {
                    const int32_t nx = cx + dx8[k];
                    const int32_t ny = cy + dy8[k];
                    if (!fg(nx, ny)) {
                        continue;
                    }
                    const size_t nidx =
                        static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
                    if (seen[nidx] == 0) {
                        seen[nidx] = 1;
                        stack.push_back(static_cast<int32_t>(nidx));
                    }
                }
            }
            if (!border.empty()) {
                blobs.push_back(std::move(border));
            }
        }
    }
    return blobs;
}

namespace {

// Is `p` inside the convex quad? Consistent sign of the cross product
// against all four edges means inside.
bool point_in_quad(const naina_point quad[4], float px, float py) {
    int32_t pos = 0;
    int32_t neg = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const naina_point& a = quad[i];
        const naina_point& b = quad[(i + 1) % 4];
        const float d = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        if (d > 0.0F) {
            ++pos;
        } else if (d < 0.0F) {
            ++neg;
        }
    }
    return pos == 0 || neg == 0;
}

}  // namespace

float box_score(const float* prob, int32_t width, int32_t height, const naina_point quad[4]) {
    if (prob == nullptr || width <= 0 || height <= 0 || quad == nullptr) {
        return 0.0F;
    }
    // Walk the quad's axis-aligned extent, averaging only pixels inside it.
    // This is PaddleOCR's box_score_fast.
    float minx = quad[0].x;
    float maxx = quad[0].x;
    float miny = quad[0].y;
    float maxy = quad[0].y;
    for (int32_t i = 1; i < 4; ++i) {
        minx = std::min(minx, quad[i].x);
        maxx = std::max(maxx, quad[i].x);
        miny = std::min(miny, quad[i].y);
        maxy = std::max(maxy, quad[i].y);
    }
    int32_t x0 = static_cast<int32_t>(std::floor(minx));
    int32_t x1 = static_cast<int32_t>(std::ceil(maxx));
    int32_t y0 = static_cast<int32_t>(std::floor(miny));
    int32_t y1 = static_cast<int32_t>(std::ceil(maxy));
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(width - 1, x1);
    y1 = std::min(height - 1, y1);
    if (x1 < x0 || y1 < y0) {
        return 0.0F;
    }

    double acc = 0.0;
    int64_t n = 0;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            if (!point_in_quad(quad, static_cast<float>(x), static_cast<float>(y))) {
                continue;
            }
            acc += static_cast<double>(
                prob[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]);
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(acc / static_cast<double>(n)) : 0.0F;
}

std::vector<naina_textbox> decode(const float* prob,
                                  int32_t width,
                                  int32_t height,
                                  const Config& cfg) {
    std::vector<naina_textbox> out;
    if (prob == nullptr || width <= 0 || height <= 0) {
        return out;
    }
    const Bitmap bm = binarize(prob, width, height, cfg.thresh);
    const auto blobs = find_blob_borders(bm, cfg.max_candidates);
    out.reserve(blobs.size());

    for (const auto& border : blobs) {
        if (border.size() < 3) {
            continue;  // a single pixel or a pair cannot form a box
        }
        naina_point quad[4];
        if (!min_area_quad(border, quad)) {
            continue;
        }
        // min_area_quad works in its own rotating-calipers frame, so corner[0]
        // is not reliably top-left. Canonicalise before anything downstream
        // warps the quad: a mis-ordered quad reads the text strip mirrored or
        // upside down and produces plausible garbage rather than an error.
        order_quad_clockwise(quad);

        // Reject slivers before the expensive scoring pass.
        const float side_a = std::hypot(quad[1].x - quad[0].x, quad[1].y - quad[0].y);
        const float side_b = std::hypot(quad[3].x - quad[0].x, quad[3].y - quad[0].y);
        if (std::min(side_a, side_b) < cfg.min_box_side) {
            continue;
        }

        // Score on the RAW box, before unclip — unclip deliberately spills
        // into background, so scoring after it would depress every score.
        const float score = box_score(prob, width, height, quad);
        if (score < cfg.box_thresh) {
            continue;
        }

        // Unclip: DBNet trained on shrunk polygons.
        std::vector<naina_point> poly(quad, quad + 4);
        const float area = polygon_area(poly);
        const float per = polygon_perimeter(poly);
        if (per <= 0.0F) {
            continue;
        }
        const float distance = area * cfg.unclip_ratio / per;
        const auto grown = offset_convex_polygon(poly, distance);
        if (grown.size() != 4) {
            continue;
        }

        naina_textbox tb{};
        for (int32_t i = 0; i < 4; ++i) {
            // Clamp to the map so downstream warping never samples outside.
            tb.corners[i].x = std::clamp(grown[static_cast<size_t>(i)].x, 0.0F,
                                         static_cast<float>(width - 1));
            tb.corners[i].y = std::clamp(grown[static_cast<size_t>(i)].y, 0.0F,
                                         static_cast<float>(height - 1));
        }
        tb.score = score;
        out.push_back(tb);
    }
    return out;
}

}  // namespace naina::internal::db_postprocess
