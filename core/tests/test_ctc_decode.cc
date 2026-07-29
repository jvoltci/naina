// CTC greedy decode: [T, C] logits -> string + confidence.
#include "modules/charset.hpp"
#include "modules/ctc_decode.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::charset::Charset;
using naina::internal::ctc_decode::Decoded;
using naina::internal::ctc_decode::greedy_decode;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Charset: blank, a, b, c, space  -> 5 classes.
static Charset abc_charset() {
    Charset cs;
    cs.assign({"a", "b", "c"});
    return cs;
}

// Build a [T, C] buffer where timestep t puts `hot` probability on class
// argmax[t] and spreads the rest evenly.
static std::vector<float> logits(const std::vector<int>& argmax, int C, float hot) {
    std::vector<float> v(argmax.size() * static_cast<size_t>(C), 0.0F);
    const float rest = (1.0F - hot) / static_cast<float>(C - 1);
    for (size_t t = 0; t < argmax.size(); ++t) {
        for (int c = 0; c < C; ++c) {
            v[t * static_cast<size_t>(C) + static_cast<size_t>(c)] = rest;
        }
        v[t * static_cast<size_t>(C) + static_cast<size_t>(argmax[t])] = hot;
    }
    return v;
}

static void test_collapses_repeats_and_drops_blanks() {
    const Charset cs = abc_charset();
    // a a blank a b  ->  "aab" collapses to "a", then blank resets, then "ab"
    // Expected CTC output: "a" + "a" + "b" = "aab"? No:
    //   t0=a t1=a  -> repeat, one 'a'
    //   t2=blank   -> resets the repeat guard
    //   t3=a       -> new 'a'
    //   t4=b       -> 'b'
    // Result: "aab"
    const auto v = logits({1, 1, 0, 1, 2}, 5, 0.9F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 5, 5, cs, &d));
    EXPECT(d.text == "aab");
}

static void test_repeat_without_blank_is_one_character() {
    const Charset cs = abc_charset();
    const auto v = logits({1, 1, 1, 1}, 5, 0.9F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 4, 5, cs, &d));
    EXPECT(d.text == "a");
}

static void test_all_blank_yields_empty_text() {
    const Charset cs = abc_charset();
    const auto v = logits({0, 0, 0}, 5, 0.9F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 3, 5, cs, &d));
    EXPECT(d.text.empty());
    // No emitted characters means no confidence to report.
    EXPECT(d.confidence == 0.0F);
}

static void test_confidence_is_mean_over_emitted_steps_only() {
    const Charset cs = abc_charset();
    // Two emitted characters at 0.8, plus a blank step at 0.99 that must NOT
    // inflate the average.
    std::vector<float> v(3 * 5, 0.0F);
    auto set = [&](int t, int c, float p) {
        v[static_cast<size_t>(t * 5 + c)] = p;
    };
    set(0, 1, 0.8F);
    set(1, 0, 0.99F);  // blank
    set(2, 2, 0.8F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 3, 5, cs, &d));
    EXPECT(d.text == "ab");
    EXPECT(std::fabs(d.confidence - 0.8F) < 1e-4F);
}

static void test_space_class_is_emitted() {
    const Charset cs = abc_charset();  // class 4 is the space
    const auto v = logits({1, 4, 2}, 5, 0.9F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 3, 5, cs, &d));
    EXPECT(d.text == "a b");
}

static void test_multibyte_characters_concatenate() {
    Charset cs;
    cs.assign({"\xe4\xb8\xad", "\xe6\x96\x87"});  // 中, 文 -> 4 classes
    const auto v = logits({1, 2}, 4, 0.9F);
    Decoded d;
    EXPECT(greedy_decode(v.data(), 2, 4, cs, &d));
    EXPECT(d.text == "\xe4\xb8\xad\xe6\x96\x87");
    EXPECT(d.text.size() == 6);
}

static void test_rejects_bad_arguments() {
    const Charset cs = abc_charset();
    Decoded d;
    EXPECT(!greedy_decode(nullptr, 3, 5, cs, &d));
    EXPECT(!greedy_decode(reinterpret_cast<const float*>(&d), 0, 5, cs, &d));
    const auto v = logits({1}, 5, 0.9F);
    EXPECT(!greedy_decode(v.data(), 1, 5, cs, nullptr));
    // A class count that disagrees with the charset is a configuration bug
    // and must fail loudly rather than silently mis-mapping characters.
    EXPECT(!greedy_decode(v.data(), 1, 9, cs, &d));
}

int main() {
    test_collapses_repeats_and_drops_blanks();
    test_repeat_without_blank_is_one_character();
    test_all_blank_yields_empty_text();
    test_confidence_is_mean_over_emitted_steps_only();
    test_space_class_is_emitted();
    test_multibyte_characters_concatenate();
    test_rejects_bad_arguments();
    if (failures == 0) {
        std::printf("test_ctc_decode: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
