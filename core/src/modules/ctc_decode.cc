#include "ctc_decode.hpp"

namespace naina::internal::ctc_decode {

bool greedy_decode(const float* logits,
                   int32_t num_steps,
                   int32_t num_classes,
                   const charset::Charset& cs,
                   Decoded* out) {
    if (logits == nullptr || out == nullptr || num_steps <= 0 || num_classes <= 1) {
        return false;
    }
    // A mismatch here would silently map class indices to the wrong
    // characters, which reads as a broken model rather than a config bug.
    if (num_classes != cs.num_classes()) {
        return false;
    }

    out->text.clear();
    out->confidence = 0.0F;

    double conf_sum = 0.0;
    int64_t emitted = 0;
    int32_t prev = -1;  // previous argmax class, for repeat collapsing

    for (int32_t t = 0; t < num_steps; ++t) {
        const float* row = logits + static_cast<size_t>(t) * static_cast<size_t>(num_classes);
        int32_t best = 0;
        float best_p = row[0];
        for (int32_t c = 1; c < num_classes; ++c) {
            if (row[c] > best_p) {
                best_p = row[c];
                best = c;
            }
        }

        // Blank (class 0) emits nothing but resets the repeat guard, which is
        // what lets a genuine double letter through as two characters.
        if (best == 0) {
            prev = -1;
            continue;
        }
        if (best == prev) {
            continue;
        }
        prev = best;

        const std::string& ch = cs.at(best);
        if (!ch.empty()) {
            out->text += ch;
        }
        conf_sum += static_cast<double>(best_p);
        ++emitted;
    }

    // Confidence averages only the steps that produced output. Including
    // blanks would let a mostly-blank strip report high confidence.
    if (emitted > 0) {
        out->confidence = static_cast<float>(conf_sum / static_cast<double>(emitted));
    }
    return true;
}

}  // namespace naina::internal::ctc_decode
