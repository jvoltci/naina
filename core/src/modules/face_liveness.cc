#include "face_liveness.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace naina::internal::face_liveness {

namespace {

// In-place softmax over a contiguous probability vector.
void softmax(float* v, size_t n) {
    if (n == 0) {
        return;
    }
    float max = v[0];
    for (size_t i = 1; i < n; ++i) {
        if (v[i] > max) {
            max = v[i];
        }
    }
    float sum = 0;
    for (size_t i = 0; i < n; ++i) {
        v[i] = std::exp(v[i] - max);
        sum += v[i];
    }
    if (sum > 0) {
        for (size_t i = 0; i < n; ++i) {
            v[i] /= sum;
        }
    }
}

}  // namespace

naina_status liveness(backend::ISession* session,
                      const ImageView& src,
                      const naina_face& face,
                      const Config& cfg,
                      float* out_score) {
    if (session == nullptr || out_score == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_score = 0.0F;

    const size_t s = static_cast<size_t>(cfg.input_size);
    std::vector<float> input(3 * s * s);
    crop_face_rgb_planar_f32(
        src, face.bbox, cfg.crop_expand, cfg.input_size, cfg.mean, cfg.scale, input.data());

    Tensor in = Tensor::view(input.data(), {1, 3, cfg.input_size, cfg.input_size}, DType::F32);

    auto out_descs = session->outputs();
    if (out_descs.size() != 1) {
        return NAINA_E_INFERENCE_FAILED;
    }
    size_t classes = 1;
    for (auto d : out_descs[0].shape) {
        if (d > 0) {
            classes *= static_cast<size_t>(d);
        }
    }
    if (classes == 0) {
        return NAINA_E_INFERENCE_FAILED;
    }

    std::vector<float> raw(classes, 0.0F);
    std::vector<Tensor> outs;
    outs.push_back(Tensor::view(raw.data(), out_descs[0].shape, DType::F32));

    std::vector<Tensor> ins;
    ins.push_back(std::move(in));
    const naina_status rs = session->run(ins, outs);
    if (rs != NAINA_OK) {
        return rs;
    }

    // Models commonly emit raw logits; softmax to get probabilities. If the
    // model already outputs probabilities (rare; some emit a single sigmoid
    // score), softmax(x) where x is already in [0,1] still yields a valid
    // distribution — just less peaked.
    softmax(raw.data(), classes);

    const size_t idx = static_cast<size_t>(cfg.live_class_index);
    if (idx >= classes) {
        return NAINA_E_INVALID_ARG;
    }
    *out_score = raw[idx];
    return NAINA_OK;
}

}  // namespace naina::internal::face_liveness
