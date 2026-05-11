// SHA-256 test vectors against the NIST FIPS-180-2 examples and a couple
// of well-known short strings, including the empty string.
#include "../src/sha256.hpp"

#include <cstdio>
#include <string>

using naina::internal::Sha256;
using naina::internal::sha256_hex;
using naina::internal::to_hex;

static int failures = 0;

#define EXPECT_EQ(a, b)                                                            \
    do {                                                                           \
        const auto _a = (a);                                                       \
        const auto _b = (b);                                                       \
        if (_a != _b) {                                                            \
            std::fprintf(stderr,                                                   \
                         "FAIL %s:%d  %s != %s\n  got:      %s\n  expected: %s\n", \
                         __FILE__,                                                 \
                         __LINE__,                                                 \
                         #a,                                                       \
                         #b,                                                       \
                         _a.c_str(),                                               \
                         _b.c_str());                                              \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

int main() {
    // Empty string.
    EXPECT_EQ(sha256_hex(""),
              std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    // "abc"
    EXPECT_EQ(sha256_hex("abc"),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // 56-byte input — exercises padding boundary.
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    // Streamed update produces the same digest as a single update.
    {
        Sha256 h;
        h.update("hello ", 6);
        h.update("world", 5);
        EXPECT_EQ(to_hex(h.finalize()),
                  std::string("b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9"));
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("sha256 tests passed\n");
    return 0;
}
