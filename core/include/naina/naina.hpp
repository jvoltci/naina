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

// One recognised line of text. Values are copied out of the page, so a Line
// outlives the Page it came from — unlike the borrowed pointers in the C ABI.
struct Line {
    std::string text;
    float confidence = 0.0F;
    float score = 0.0F;  // detection confidence for the quad
    std::array<Point, 4> quad{};
};

// A read page. RAII over naina_page_t.
class Page {
public:
    ~Page();
    Page(Page&&) noexcept;
    Page& operator=(Page&&) noexcept;
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    // Recognised lines in detection order.
    std::vector<Line> lines() const;

    // Markdown and JSON serialisations. Copies, so they are safe to keep.
    std::string markdown() const;
    std::string json() const;

private:
    friend class Engine;
    explicit Page(naina_page_t* p) : h_(p) {}
    naina_page_t* h_ = nullptr;
};

class Engine {
public:
    explicit Engine(const Config& cfg = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Read a document: detect text, recognise it, return the page.
    Page read(const Image& img);

    // Detection only, for callers that want geometry without recognition.
    std::vector<std::array<Point, 4>> detect_text(const Image& img);

    naina_ctx_t* raw() noexcept { return ctx_; }

private:
    naina_ctx_t* ctx_ = nullptr;
};

}  // namespace naina

#endif  // NAINA_HPP
