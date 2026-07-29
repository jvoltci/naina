// PP-OCRv6 detection. Input is 1x3xHxW planar BGR float32, normalised with
// ImageNet mean/std after a 1/255 scale. The single output is a 1x1xHxW
// probability map at the SAME resolution as the input, verified by running the
// model at four input sizes.

#include "text_detect.hpp"

#include <algorithm>

namespace naina::internal::text_detect {

naina_status detect(backend::ISession* session,
                    const ImageView& src,
                    const Config& cfg,
                    std::vector<naina_textbox>* out_boxes) {
    if (session == nullptr || out_boxes == nullptr || src.data == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    out_boxes->clear();
    if (src.width <= 0 || src.height <= 0) {
        return NAINA_E_INVALID_ARG;
    }

    // 1) Plan the resize, then build the normalised planar BGR input.
    const DetResize plan = plan_det_resize(src.width, src.height, cfg.limit_side, cfg.multiple_of);
    if (plan.out_w <= 0 || plan.out_h <= 0) {
        return NAINA_E_INVALID_ARG;
    }
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    std::vector<float> input(3U * plane);
    resize_det_bgr_planar_f32(src, plan, cfg.scale, cfg.mean, cfg.std_, input.data());

    Tensor in =
        Tensor::view(input.data(),
                     {1, 3, static_cast<int64_t>(plan.out_h), static_cast<int64_t>(plan.out_w)},
                     DType::F32);

    // 2) The backend copies results into a caller-allocated buffer and drops
    //    the real shape, so the size must be exact. DBNet's output resolution
    //    equals its input resolution.
    std::vector<float> prob(plane, 0.0F);
    Tensor out =
        Tensor::view(prob.data(),
                     {1, 1, static_cast<int64_t>(plan.out_h), static_cast<int64_t>(plan.out_w)},
                     DType::F32);

    std::vector<Tensor> ins;
    ins.push_back(std::move(in));
    std::vector<Tensor> outs;
    outs.push_back(std::move(out));

    const naina_status rs = session->run(ins, outs);
    if (rs != NAINA_OK) {
        return rs;
    }

    // 3) Decode in probability-map space.
    std::vector<naina_textbox> boxes =
        db_postprocess::decode(prob.data(), plan.out_w, plan.out_h, cfg.db);

    // 4) Map back to source coordinates. Scale is per-axis because the resize
    //    rounds each dimension independently — using one scale skews every box
    //    on non-square input.
    const float inv_x = plan.scale_x != 0.0F ? 1.0F / plan.scale_x : 1.0F;
    const float inv_y = plan.scale_y != 0.0F ? 1.0F / plan.scale_y : 1.0F;
    const float max_x = static_cast<float>(src.width - 1);
    const float max_y = static_cast<float>(src.height - 1);
    for (auto& b : boxes) {
        for (auto& c : b.corners) {
            c.x = std::clamp(c.x * inv_x, 0.0F, max_x);
            c.y = std::clamp(c.y * inv_y, 0.0F, max_y);
        }
    }

    *out_boxes = std::move(boxes);
    return NAINA_OK;
}

}  // namespace naina::internal::text_detect
