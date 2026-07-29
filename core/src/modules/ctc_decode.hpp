// CTC greedy decode: per-timestep argmax, collapse repeats, drop blanks.
//
// This is PaddleOCR's CTCLabelDecode without beam search. Pure function, no
// model and no session, so the whole decode path is testable from synthetic
// logits.
#ifndef NAINA_INTERNAL_CTC_DECODE_HPP
#define NAINA_INTERNAL_CTC_DECODE_HPP

#include "charset.hpp"

#include <cstdint>
#include <string>

namespace naina::internal::ctc_decode {

struct Decoded {
    std::string text;         // UTF-8
    float confidence = 0.0F;  // mean argmax probability over EMITTED steps
};

// `logits` is [num_steps, num_classes] row-major, as produced by the rec
// model. Values are already softmaxed by the graph, so they are treated as
// probabilities. `num_classes` must equal charset.num_classes().
//
// Returns false on a null pointer, a non-positive step count, a null out
// pointer, or a num_classes / charset mismatch.
bool greedy_decode(const float* logits,
                   int32_t num_steps,
                   int32_t num_classes,
                   const charset::Charset& cs,
                   Decoded* out);

}  // namespace naina::internal::ctc_decode

#endif  // NAINA_INTERNAL_CTC_DECODE_HPP
