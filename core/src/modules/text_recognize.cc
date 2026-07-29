// PP-OCRv6 recognition. Input is Nx3x48xW planar BGR float32 normalised to
// [-1, 1] (mean 0.5, std 0.5 after a 1/255 scale). Output is NxTxC softmaxed
// probabilities with T == W/8 exactly, both verified by running the model.

#include "text_recognize.hpp"

namespace naina::internal::text_recognize {

namespace {

// The graph's time dimension is W/8. Rounding the strip width up to a multiple
// of 8 keeps that exact, so the output buffer size is predictable without
// reimplementing the graph's floor-division chain. The extra columns are blank
// padding, which CTC emits nothing for.
constexpr int32_t kWidthMultiple = 8;

int32_t round_up_to(int32_t v, int32_t m) {
    if (m <= 1 || v <= 0) {
        return v > 0 ? v : m;
    }
    const int32_t r = v % m;
    return r == 0 ? v : v + (m - r);
}

}  // namespace

naina_status recognize(backend::ISession* session,
                       const ImageView& src,
                       const std::vector<naina_textbox>& boxes,
                       const charset::Charset& cs,
                       const Config& cfg,
                       std::vector<Line>* out_lines) {
    if (session == nullptr || out_lines == nullptr || src.data == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    out_lines->clear();
    if (cs.num_classes() <= 1) {
        // No charset means nothing can be mapped to characters. Failing here
        // rather than returning empty text makes a config error obvious.
        return NAINA_E_MODEL_NOT_FOUND;
    }
    out_lines->reserve(boxes.size());

    const int32_t num_classes = cs.num_classes();

    for (const naina_textbox& box : boxes) {
        Line line{};
        line.box = box;

        QuadStrip strip = plan_quad_strip(box.corners, cfg.height, cfg.max_width);
        strip.width = round_up_to(strip.width, kWidthMultiple);
        if (strip.width > cfg.max_width) {
            strip.width = (cfg.max_width / kWidthMultiple) * kWidthMultiple;
        }
        if (strip.width < kWidthMultiple) {
            strip.width = kWidthMultiple;
        }

        const size_t plane = static_cast<size_t>(strip.width) * static_cast<size_t>(strip.height);
        std::vector<float> input(3U * plane);
        warp_quad_bgr_planar_f32(
            src, box.corners, strip, cfg.scale, cfg.mean, cfg.std_, input.data());

        const int32_t num_steps = strip.width / kWidthMultiple;
        std::vector<float> logits(static_cast<size_t>(num_steps) * static_cast<size_t>(num_classes),
                                  0.0F);

        Tensor in = Tensor::view(
            input.data(),
            {1, 3, static_cast<int64_t>(strip.height), static_cast<int64_t>(strip.width)},
            DType::F32);
        Tensor out =
            Tensor::view(logits.data(),
                         {1, static_cast<int64_t>(num_steps), static_cast<int64_t>(num_classes)},
                         DType::F32);

        std::vector<Tensor> ins;
        ins.push_back(std::move(in));
        std::vector<Tensor> outs;
        outs.push_back(std::move(out));

        const naina_status rs = session->run(ins, outs);
        if (rs != NAINA_OK) {
            // Keep the geometry; the caller may still want the box. A single
            // bad strip should not abort the whole page.
            out_lines->push_back(std::move(line));
            continue;
        }

        ctc_decode::Decoded d;
        if (ctc_decode::greedy_decode(logits.data(), num_steps, num_classes, cs, &d)) {
            line.text = std::move(d.text);
            line.confidence = d.confidence;
        }
        out_lines->push_back(std::move(line));
    }

    return NAINA_OK;
}

}  // namespace naina::internal::text_recognize
