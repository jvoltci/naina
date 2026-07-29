// Text detection — PP-OCRv6 det (DBNet head) + DB post-processing.
//
// Pipeline: resize source to a multiple of 32 (longest side <= 960), normalise
// to planar BGR float32, run the session, then decode the single-channel
// probability map into quads and map those quads back to SOURCE coordinates.
#ifndef NAINA_INTERNAL_TEXT_DETECT_HPP
#define NAINA_INTERNAL_TEXT_DETECT_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"
#include "db_postprocess.hpp"

#include <vector>

namespace naina::internal::text_detect {

struct Config {
    // Resize, from the manifest's preprocess.resize block.
    int32_t limit_side = 960;
    int32_t multiple_of = 32;

    // NormalizeImage, from the manifest. Values are PP-OCRv6 det's own.
    float scale[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    float mean[3] = {0.485F, 0.456F, 0.406F};
    float std_[3] = {0.229F, 0.224F, 0.225F};

    // DBPostProcess, from the manifest's postprocess block.
    db_postprocess::Config db{};
};

// Run detection. Output quads are in SOURCE image coordinates, already scaled
// back from the resized inference resolution.
naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_textbox>* out_boxes);

}  // namespace naina::internal::text_detect

#endif  // NAINA_INTERNAL_TEXT_DETECT_HPP
