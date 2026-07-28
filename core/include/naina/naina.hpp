// naina — C++ ergonomic wrapper over the C ABI.
// RAII, std::vector/std::array, exceptions on construction, status on hot paths.
#ifndef NAINA_HPP
#define NAINA_HPP

#include "naina.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace naina {

enum class Backend { Auto, ONNXRuntime, OpenVINO, NCNN, MNN, CoreML, TensorRT, ExecuTorch };
enum class Device { Auto, CPU, GPU, NPU };
enum class PixFmt { RGB8, BGR8, NV12, YUV420P, Gray8 };

struct BBox {
    float x, y, w, h, score;
};
struct Point {
    float x, y;
};

// Device tier — model size, not licence. See naina.h for the byte budgets.
enum class Tier { Auto, Tiny, Small, Medium };

struct Config {
    Backend backend = Backend::Auto;
    Device device = Device::Auto;
    std::filesystem::path models_root;
    int num_threads = 0;
    Tier tier = Tier::Auto;
};

class Error : public std::runtime_error {
public:
    Error(naina_status s, const char* where)
        : std::runtime_error(std::string(where) + ": " + naina_status_str(s)), code_(s) {}
    naina_status code() const noexcept { return code_; }

private:
    naina_status code_;
};

// Zero-copy image view. Pixels must outlive the Image.
class Image {
public:
    Image(const uint8_t* data, int w, int h, int stride, PixFmt fmt);
    ~Image();
    Image(Image&&) noexcept;
    Image& operator=(Image&&) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    const naina_image_t* handle() const noexcept { return h_; }

private:
    naina_image_t* h_ = nullptr;
};

class Engine {
public:
    explicit Engine(const Config& cfg = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // OCR surface. The C++ wrappers land alongside the modules that back
    // them; see docs/superpowers/plans/2026-07-29-naina-ocr-core.md.

    naina_ctx_t* raw() noexcept { return ctx_; }

private:
    naina_ctx_t* ctx_ = nullptr;
};

}  // namespace naina

#endif  // NAINA_HPP
