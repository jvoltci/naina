// Internal SHA-256 utility — used by the model loader to verify downloaded
// weights against the manifest. Not part of the public API.
#ifndef NAINA_INTERNAL_SHA256_HPP
#define NAINA_INTERNAL_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace naina::internal {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t bytes);
    std::array<uint8_t, 32> finalize();

private:
    void compress(const uint8_t block[64]);

    std::array<uint32_t, 8> h_;
    std::array<uint8_t, 64> buf_;
    size_t buf_len_ = 0;
    uint64_t total_bits_ = 0;
};

// Convenience helpers.
std::string to_hex(const std::array<uint8_t, 32>& digest);
std::string sha256_hex(std::string_view bytes);
std::string sha256_file_hex(const std::filesystem::path& path);

}  // namespace naina::internal

#endif  // NAINA_INTERNAL_SHA256_HPP
