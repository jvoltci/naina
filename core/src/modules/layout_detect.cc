// PP-DocLayout region detection.
//
// Preprocessing, from the model's own inference.yml:
//   Resize(target=[side, side], keep_ratio=false)  -- a STRETCH, not letterbox
//   NormalizeImage(is_scale=true, mean=.485/.456/.406, std=.229/.224/.225)
//   Permute (HWC -> CHW)
//
// Upstream resizes with bicubic interpolation (cv2 interp=2); naina uses
// bilinear, which shifts region edges by a fraction of a pixel. Region boxes
// feed reading order, not text extraction, so that is well inside tolerance.
//
// scale_factor is passed as the REAL resize ratio rather than [1, 1], because
// the graph divides its predictions by it — so the boxes come back already in
// source-image coordinates instead of resized space.

#include "layout_detect.hpp"

#include <algorithm>
#include <string>

namespace naina::internal::layout_detect {

namespace {

// PP-DocLayout's 23 classes, in label_list order. Read from the model config
// rather than guessed; a wrong index silently mislabels every region.
//
//   0 paragraph_title   8 table            16 seal
//   1 image             9 table_title      17 chart_title
//   2 text             10 reference        18 chart
//   3 number           11 doc_title        19 formula_number
//   4 abstract         12 footnote         20 header_image
//   5 content          13 header           21 footer_image
//   6 figure_title     14 algorithm        22 aside_text
//   7 formula          15 footer
constexpr int32_t kNumClasses = 23;

}  // namespace

naina_region_kind kind_from_class_id(int32_t class_id) {
    switch (class_id) {
        case 11:  // doc_title
        case 0:   // paragraph_title
            return NAINA_REGION_TITLE;

        case 2:   // text
        case 4:   // abstract
        case 5:   // content
        case 10:  // reference
        case 14:  // algorithm
        case 22:  // aside_text
        case 12:  // footnote — real prose, so TEXT rather than page furniture
            return NAINA_REGION_TEXT;

        case 8:  // table
            return NAINA_REGION_TABLE;

        case 1:   // image
        case 18:  // chart
        case 16:  // seal
        case 20:  // header_image
        case 21:  // footer_image
            return NAINA_REGION_FIGURE;

        case 6:   // figure_title
        case 9:   // table_title
        case 17:  // chart_title
            return NAINA_REGION_CAPTION;

        case 7:   // formula
        case 19:  // formula_number
            return NAINA_REGION_FORMULA;

        case 13:  // header
            return NAINA_REGION_HEADER;

        case 15:  // footer
            return NAINA_REGION_FOOTER;

        case 3:  // number — a page number
            return NAINA_REGION_PAGENUM;

        default:
            break;
    }
    return NAINA_REGION_UNKNOWN;
}

void dedupe_overlapping(float iou_thresh, std::vector<naina_region>* regions) {
    if (regions == nullptr || regions->size() < 2) {
        return;
    }

    // Visit in descending score so the label that survives for a box is the
    // best-scoring one. Suppress into a mask rather than reordering: the output
    // keeps input order, which keeps the pipeline's output reproducible.
    std::vector<size_t> by_score(regions->size());
    for (size_t i = 0; i < by_score.size(); ++i) {
        by_score[i] = i;
    }
    std::stable_sort(by_score.begin(), by_score.end(), [&](size_t a, size_t b) {
        return (*regions)[a].bbox.score > (*regions)[b].bbox.score;
    });

    // IoU, not containment. A small region fully inside a big one — a title in a
    // text block, a formula in a paragraph — has containment 1.0 but low IoU,
    // and both are real regions that must survive.
    const auto iou = [](const naina_bbox& a, const naina_bbox& b) -> float {
        const float x1 = std::max(a.x, b.x);
        const float y1 = std::max(a.y, b.y);
        const float x2 = std::min(a.x + a.w, b.x + b.w);
        const float y2 = std::min(a.y + a.h, b.y + b.h);
        const float iw = x2 - x1;
        const float ih = y2 - y1;
        if (iw <= 0.0F || ih <= 0.0F) {
            return 0.0F;
        }
        const float inter = iw * ih;
        const float uni = a.w * a.h + b.w * b.h - inter;
        return uni <= 0.0F ? 0.0F : inter / uni;
    };

    std::vector<bool> dropped(regions->size(), false);
    std::vector<size_t> kept;
    kept.reserve(regions->size());

    for (const size_t idx : by_score) {
        bool duplicate = false;
        for (const size_t k : kept) {
            if (iou((*regions)[idx].bbox, (*regions)[k].bbox) > iou_thresh) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            dropped[idx] = true;
        } else {
            kept.push_back(idx);
        }
    }

    std::vector<naina_region> out;
    out.reserve(kept.size());
    for (size_t i = 0; i < regions->size(); ++i) {
        if (!dropped[i]) {
            out.push_back((*regions)[i]);
        }
    }
    *regions = std::move(out);
}

naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_region>* out_regions) {
    if (session == nullptr || out_regions == nullptr || src.data == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    out_regions->clear();
    if (src.width <= 0 || src.height <= 0 || cfg.input_side <= 0) {
        return NAINA_E_INVALID_ARG;
    }

    const int32_t side = cfg.input_side;
    const size_t plane = static_cast<size_t>(side) * static_cast<size_t>(side);

    // A stretch to side x side: scale each axis independently, no letterbox.
    DetResize plan{};
    plan.out_w = side;
    plan.out_h = side;
    plan.scale_x = static_cast<float>(side) / static_cast<float>(src.width);
    plan.scale_y = static_cast<float>(side) / static_cast<float>(src.height);

    std::vector<float> chw(3U * plane);
    resize_det_bgr_planar_f32(src, plan, cfg.scale, cfg.mean, cfg.std_, chw.data());

    // The det path produces planar BGR; layout wants RGB, so swap planes 0 and 2
    // in place rather than adding a second resize implementation.
    for (size_t i = 0; i < plane; ++i) {
        std::swap(chw[i], chw[2U * plane + i]);
    }

    // PaddleDetection's convention: scale_factor is [scale_y, scale_x], and the
    // graph divides predictions by it. Passing the real ratios yields boxes
    // already in source coordinates.
    std::vector<float> scale_factor = {plan.scale_y, plan.scale_x};
    std::vector<float> im_shape = {static_cast<float>(side), static_cast<float>(side)};

    // Feed BY NAME. Two of the four variants take an extra im_shape input, and
    // a positional feed would silently hand them the wrong tensors.
    const auto in_descs = session->inputs();
    std::vector<Tensor> ins;
    ins.reserve(in_descs.size());
    for (const auto& d : in_descs) {
        if (d.name == "image") {
            ins.push_back(Tensor::view(
                chw.data(),
                {1, 3, static_cast<int64_t>(side), static_cast<int64_t>(side)},
                DType::F32));
        } else if (d.name == "scale_factor") {
            ins.push_back(Tensor::view(scale_factor.data(), {1, 2}, DType::F32));
        } else if (d.name == "im_shape") {
            ins.push_back(Tensor::view(im_shape.data(), {1, 2}, DType::F32));
        } else {
            // An unrecognised input means this is a layout architecture naina
            // has not been taught. Guessing would produce plausible garbage.
            return NAINA_E_UNSUPPORTED;
        }
    }

    // Outputs: [N, 6] detections plus a count. The backend copies into
    // caller-allocated buffers and discards shapes, so a buffer that is too
    // small is rejected outright — allocate for the largest top-k any variant
    // emits (300) with headroom.
    //
    // Exactly two outputs is a hard requirement, not an assumption. A third
    // output means this is a different architecture (PP-DocLayoutV3 emits an
    // [N, 7] box row plus 300 200x200 masks), and decoding it as [N, 6] would
    // silently shift every column.
    constexpr int64_t kMaxDet = 512;
    const auto out_descs = session->outputs();
    if (out_descs.size() != 2) {
        return NAINA_E_UNSUPPORTED;
    }
    std::vector<float> boxes(static_cast<size_t>(kMaxDet) * 6U, 0.0F);
    std::vector<int32_t> count(static_cast<size_t>(kMaxDet), 0);

    std::vector<Tensor> outs;
    outs.reserve(2);
    outs.push_back(Tensor::view(boxes.data(), {kMaxDet, 6}, DType::F32));
    outs.push_back(Tensor::view(count.data(), {kMaxDet}, DType::I32));

    const naina_status rs = session->run(ins, outs);
    if (rs != NAINA_OK) {
        return rs;
    }

    const int32_t n = std::clamp(count[0], 0, static_cast<int32_t>(kMaxDet));
    const float max_x = static_cast<float>(src.width - 1);
    const float max_y = static_cast<float>(src.height - 1);

    out_regions->reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        const float* row = boxes.data() + static_cast<size_t>(i) * 6U;
        const int32_t class_id = static_cast<int32_t>(row[0]);
        const float score = row[1];

        // A negative class id is PaddleDetection's padding marker for "no
        // detection", not a real region.
        if (class_id < 0 || class_id >= kNumClasses) {
            continue;
        }
        if (score < cfg.score_thresh) {
            continue;
        }

        const float x1 = std::clamp(row[2], 0.0F, max_x);
        const float y1 = std::clamp(row[3], 0.0F, max_y);
        const float x2 = std::clamp(row[4], 0.0F, max_x);
        const float y2 = std::clamp(row[5], 0.0F, max_y);
        const float w = x2 - x1;
        const float h = y2 - y1;
        if (w < cfg.min_side || h < cfg.min_side) {
            continue;
        }

        naina_region r{};
        r.bbox.x = x1;
        r.bbox.y = y1;
        r.bbox.w = w;
        r.bbox.h = h;
        r.bbox.score = score;
        r.kind = kind_from_class_id(class_id);
        r.order = -1;  // doc_assemble assigns reading order
        out_regions->push_back(r);
    }

    // PaddleDetection's NMS is per class, so the same physical box arrives once
    // per plausible label. Drop the extras before doc_assemble sees them —
    // otherwise one box competes with itself for line assignment.
    dedupe_overlapping(cfg.dedupe_iou, out_regions);

    return NAINA_OK;
}

}  // namespace naina::internal::layout_detect
