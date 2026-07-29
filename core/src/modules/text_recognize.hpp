// Text recognition — PP-OCRv6 rec (CTC head) over rectified strips.
//
// Each detected quad is perspective-warped to a fixed-height strip, run through
// the recogniser, and CTC-decoded to UTF-8. One strip per session call: batching
// needs a uniform width across the batch, which is a later optimisation rather
// than a v0.2 requirement.
#ifndef NAINA_INTERNAL_TEXT_RECOGNIZE_HPP
#define NAINA_INTERNAL_TEXT_RECOGNIZE_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"
#include "charset.hpp"
#include "ctc_decode.hpp"

#include <string>
#include <vector>

namespace naina::internal::text_recognize {

struct Config {
    int32_t height = 48;      // rec input height is fixed by the graph
    int32_t max_width = 1200; // clamp so one bad quad cannot allocate unbounded
    float scale[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    float mean[3] = {0.5F, 0.5F, 0.5F};
    float std_[3] = {0.5F, 0.5F, 0.5F};
};

struct Line {
    naina_textbox box;
    std::string text;
    float confidence = 0.0F;
};

// Recognise every quad. Quads whose strip cannot be built, or whose decode
// fails, are still returned with empty text rather than dropped — the caller
// may want the geometry regardless.
naina_status recognize(backend::ISession* session,
                       const ImageView& src,
                       const std::vector<naina_textbox>& boxes,
                       const charset::Charset& cs,
                       const Config& cfg,
                       std::vector<Line>* out_lines);

}  // namespace naina::internal::text_recognize

#endif  // NAINA_INTERNAL_TEXT_RECOGNIZE_HPP
