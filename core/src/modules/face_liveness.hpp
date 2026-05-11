// Face liveness / anti-spoofing.
//
// Crops a square region around the detected face (with expansion margin so the
// background helps disambiguate replay-attack frames), runs a small binary or
// multi-class classifier, and returns the soft-probability of the "live"
// class. Threshold is the caller's concern.
//
// Model contract: input is [1, 3, S, S] float (S typically 80); output is
// either [1, 2] (live / spoof) or [1, K] with one designated "live" index.

#ifndef NAINA_INTERNAL_FACE_LIVENESS_HPP
#define NAINA_INTERNAL_FACE_LIVENESS_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"

namespace naina::internal::face_liveness {

struct Config {
    int input_size = 80;
    float crop_expand = 2.7F;
    float mean[3] = {0.0F, 0.0F, 0.0F};
    float scale[3] = {255.0F, 255.0F, 255.0F};
    int live_class_index = 1;
};

// Returns soft-probability of the "live" class in `out_score` (range [0, 1]).
naina_status liveness(backend::ISession* session,
                      const ImageView& src,
                      const naina_face& face,
                      const Config& cfg,
                      float* out_score);

}  // namespace naina::internal::face_liveness

#endif  // NAINA_INTERNAL_FACE_LIVENESS_HPP
