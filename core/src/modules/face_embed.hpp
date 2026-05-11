// Face embedding — aligns the face to a canonical 112x112 crop, runs the
// embedding model, and returns an L2-normalised vector.
#ifndef NAINA_INTERNAL_FACE_EMBED_HPP
#define NAINA_INTERNAL_FACE_EMBED_HPP

#include "naina/backend.hpp"
#include "naina/naina.h"

#include "../image_ops.hpp"

#include <vector>

namespace naina::internal::face_embed {

struct Config {
    int input_size = 112;
    // Preprocessing per channel; defaults match ArcFace/SFace ([-1,1]).
    float mean[3] = {127.5F, 127.5F, 127.5F};
    float scale[3] = {127.5F, 127.5F, 127.5F};
    bool l2_normalize = true;
};

// Aligns + embeds. `out_embedding` will be sized to the model's output dim.
naina_status embed(backend::ISession* session,
                   const ImageView& src,
                   const naina_face& face,
                   const Config& cfg,
                   std::vector<float>* out_embedding);

}  // namespace naina::internal::face_embed

#endif  // NAINA_INTERNAL_FACE_EMBED_HPP
