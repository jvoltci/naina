// Face detection — YuNet anchor-free decoder + NMS.
#ifndef NAINA_INTERNAL_FACE_DETECT_HPP
#define NAINA_INTERNAL_FACE_DETECT_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"

#include <vector>

namespace naina::internal::face_detect {

struct Config {
    int input_size = 640;  // YuNet 2023mar requires fixed 640x640
    float conf = 0.6F;
    float nms_iou = 0.3F;
};

// Run detection on `src` using `session`. Output bboxes / landmarks are in
// SOURCE image coordinates (already unletterboxed).
naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_face>* out_faces);

}  // namespace naina::internal::face_detect

#endif  // NAINA_INTERNAL_FACE_DETECT_HPP
