// Layout analysis — PP-DocLayout region detection.
//
// Emits labelled regions (title, paragraph, table, figure, ...) which
// doc_assemble uses to establish reading order and structure.
//
// The four PP-DocLayout variants do NOT share an input signature:
//
//   S   image[1,3,480,480], scale_factor[1,2]
//   M   image[1,3,640,640], scale_factor[1,2]
//   L   im_shape[1,2], image[1,3,640,640], scale_factor[1,2]
//   V3  im_shape, image[1,3,800,800], scale_factor   (+ an unused mask output)
//
// They DO all emit the same [N, 6] rows of [class_id, score, x1, y1, x2, y2],
// so post-processing is uniform. Inputs are therefore fed BY NAME from
// session->inputs(), never positionally — a positional feed silently
// mis-assigns tensors on two of the four models.
#ifndef NAINA_INTERNAL_LAYOUT_DETECT_HPP
#define NAINA_INTERNAL_LAYOUT_DETECT_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"

#include <vector>

namespace naina::internal::layout_detect {

struct Config {
    // Square input side. Read from the manifest; differs per variant
    // (480 for S, 640 for M/L, 800 for V3).
    int32_t input_side = 800;

    float scale[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    float mean[3] = {0.485F, 0.456F, 0.406F};
    float std_[3] = {0.229F, 0.224F, 0.225F};

    float score_thresh = 0.5F;

    // Regions thinner than this in either dimension are discarded — they are
    // never real document structure and only confuse reading order.
    float min_side = 4.0F;

    // Cross-class duplicate suppression. PaddleDetection runs NMS PER CLASS, so
    // one physical box comes back once per plausible label and nothing upstream
    // removes the extras. Measured on a real page: a running head returned
    // `text` at 0.677 and `header` at 0.481 for byte-identical coordinates.
    //
    // Deliberately high, because overlap between DIFFERENT regions is normal and
    // meaningful: a caption sits against its figure, a title against its table.
    // At 0.5 this would delete real structure. The duplicates it must catch have
    // IoU at or near 1.0, so a strict threshold removes them and nothing else.
    float dedupe_iou = 0.85F;
};

// Map a PP-DocLayout class id to naina's region kind. Exposed for testing
// because the mapping is data, not logic, and getting it wrong silently
// mislabels every region.
naina_region_kind kind_from_class_id(int32_t class_id);

// Drop regions that duplicate a higher-scoring region's box, regardless of
// class. Keeps the highest-scoring label per physical box and preserves input
// order among survivors, so reading order is unaffected.
//
// Exposed for testing: the threshold trades a real failure (one box emitted
// twice under two labels) against a worse one (deleting a caption because it
// touches its figure), and that balance needs to be pinned by tests rather than
// argued about.
void dedupe_overlapping(float iou_thresh, std::vector<naina_region>* regions);

// Run layout analysis. Region boxes are in SOURCE image coordinates.
// `order` is left at -1; doc_assemble assigns reading order.
naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_region>* out_regions);

}  // namespace naina::internal::layout_detect

#endif  // NAINA_INTERNAL_LAYOUT_DETECT_HPP
