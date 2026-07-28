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

}  // namespace naina::internal::db_postprocess
