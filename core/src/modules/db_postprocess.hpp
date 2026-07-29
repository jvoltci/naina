// DBNet post-processing: probability map -> text quads.
//
// Pure functions, no session and no model. The detection module calls this
// after inference; keeping it separate means the whole decode path is
// testable from an ASCII picture.
#ifndef NAINA_INTERNAL_DB_POSTPROCESS_HPP
#define NAINA_INTERNAL_DB_POSTPROCESS_HPP

#include "naina/naina.h"

#include <cstdint>
#include <vector>

namespace naina::internal::db_postprocess {

// A binary foreground mask. `px` is width*height, values 0 or 1.
struct Bitmap {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> px;
};

struct Config {
    float thresh = 0.2F;        // probability -> foreground
    float box_thresh = 0.4F;    // mean probability inside a box to keep it
    float unclip_ratio = 1.4F;  // DBNet shrink compensation
    int32_t max_candidates = 3000;
    float min_box_side = 3.0F;  // discard boxes thinner than this
};

// Threshold a probability map into a Bitmap. `prob` is height*width row-major.
Bitmap binarize(const float* prob, int32_t width, int32_t height, float thresh);

// Border pixels of each 8-connected foreground blob, at most
// `max_candidates` blobs. Blob order follows a raster scan of seed pixels,
// which makes the output deterministic.
std::vector<std::vector<naina_point>> find_blob_borders(const Bitmap& bm, int32_t max_candidates);

// Mean probability inside a quad, sampled over its axis-aligned extent.
// This is PaddleOCR's box_score_fast.
float box_score(const float* prob, int32_t width, int32_t height, const naina_point quad[4]);

// Full decode: probability map -> quads in probability-map coordinates.
// Callers scale the result back to source coordinates.
std::vector<naina_textbox> decode(const float* prob,
                                  int32_t width,
                                  int32_t height,
                                  const Config& cfg);

}  // namespace naina::internal::db_postprocess

#endif  // NAINA_INTERNAL_DB_POSTPROCESS_HPP
