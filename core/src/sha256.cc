// Minimal SHA-256 implementation, RFC 6234 reference style.
//
// We embed our own instead of pulling in OpenSSL just for one hash:
// it keeps the dep graph clean and the implementation is small enough
// to review (see the NIST test vectors in tests/test_sha256.cc).

#include "sha256.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace naina::internal {

namespace {

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::array<uint32_t, 8> kInit = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

inline uint32_t rotr(uint32_t v, unsigned n) {
    return (v >> n) | (v << (32 - n));
}

inline uint32_t big_sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}
inline uint32_t big_sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}
inline uint32_t small_sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
inline uint32_t small_sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

}  // namespace

Sha256::Sha256() : h_(kInit) {}

void Sha256::compress(const uint8_t block[64]) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (size_t i = 16; i < 64; ++i) {
        w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t t1 = hh + big_sigma1(e) + ch(e, f, g) + kK[i] + w[i];
        const uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += hh;
}

void Sha256::update(const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    total_bits_ += static_cast<uint64_t>(bytes) * 8;
    while (bytes != 0) {
        const size_t take = std::min(bytes, size_t{64} - buf_len_);
        std::memcpy(buf_.data() + buf_len_, p, take);
        buf_len_ += take;
        p += take;
        bytes -= take;
        if (buf_len_ == 64) {
            compress(buf_.data());
            buf_len_ = 0;
        }
    }
}

std::array<uint8_t, 32> Sha256::finalize() {
    // Append 0x80, pad zeros, then 64-bit big-endian length.
    buf_[buf_len_++] = 0x80;
    if (buf_len_ > 56) {
        while (buf_len_ < 64) {
            buf_[buf_len_++] = 0;
        }
        compress(buf_.data());
        buf_len_ = 0;
    }
    while (buf_len_ < 56) {
        buf_[buf_len_++] = 0;
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        buf_[buf_len_++] = static_cast<uint8_t>(total_bits_ >> static_cast<unsigned>(shift));
    }
    compress(buf_.data());

    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>(h_[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
    }
    return out;
}

std::string to_hex(const std::array<uint8_t, 32>& digest) {
    static const char* kHex = "0123456789abcdef";
    std::string s(64, '\0');
    for (size_t i = 0; i < 32; ++i) {
        s[i * 2] = kHex[digest[i] >> 4];
        s[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return s;
}

std::string sha256_hex(std::string_view bytes) {
    Sha256 h;
    h.update(bytes.data(), bytes.size());
    return to_hex(h.finalize());
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("sha256_file_hex: cannot open " + path.string());
    }
    Sha256 h;
    std::array<char, 64 * 1024> chunk{};
    while (f.good()) {
        f.read(chunk.data(), chunk.size());
        const auto got = f.gcount();
        if (got > 0) {
            h.update(chunk.data(), static_cast<size_t>(got));
        }
    }
    return to_hex(h.finalize());
}

}  // namespace naina::internal
