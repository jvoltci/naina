#include "face_embed.hpp"

#include <cmath>

namespace naina::internal::face_embed {

namespace {

void l2_normalize(std::vector<float>& v) {
    float sum = 0;
    for (const auto x : v) {
        sum += x * x;
    }
    const float n = std::sqrt(sum);
    if (n == 0) {
        return;
    }
    for (auto& x : v) {
        x /= n;
    }
}

}  // namespace

naina_status embed(backend::ISession* session,
                   const ImageView& src,
                   const naina_face& face,
                   const Config& cfg,
                   std::vector<float>* out_embedding) {
    if (session == nullptr || out_embedding == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    out_embedding->clear();

    // 1) Align: warp the face to a canonical 112x112 RGB planar tensor.
    const size_t s = static_cast<size_t>(cfg.input_size);
    std::vector<float> input(3 * s * s);
    warp_5pt_rgb_planar_f32(src, face.landmarks, cfg.input_size, cfg.mean, cfg.scale, input.data());

    Tensor in = Tensor::view(input.data(), {1, 3, cfg.input_size, cfg.input_size}, DType::F32);

    // 2) Allocate output. Embedding dim is whatever the model declares.
    auto out_descs = session->outputs();
    if (out_descs.size() != 1) {
        return NAINA_E_INFERENCE_FAILED;
    }
    size_t dim = 1;
    for (auto d : out_descs[0].shape) {
        if (d > 0) {
            dim *= static_cast<size_t>(d);
        }
    }
    std::vector<float> output(dim, 0.0F);
    std::vector<Tensor> outs;
    outs.push_back(Tensor::view(output.data(), out_descs[0].shape, DType::F32));

    // 3) Run.
    std::vector<Tensor> ins;
    ins.push_back(std::move(in));
    const naina_status rs = session->run(ins, outs);
    if (rs != NAINA_OK) {
        return rs;
    }

    if (cfg.l2_normalize) {
        l2_normalize(output);
    }
    *out_embedding = std::move(output);
    return NAINA_OK;
}

}  // namespace naina::internal::face_embed
