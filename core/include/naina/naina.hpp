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

struct Face {
    BBox bbox;
    std::array<Point, 5> landmarks;
    float quality;
    int32_t track_id;
};

struct Person {
    BBox bbox;
    int32_t class_id;
    int32_t track_id;
};

struct Config {
    Backend backend = Backend::Auto;
    Device device = Device::Auto;
    std::filesystem::path models_root;
    int num_threads = 0;
    bool enable_research_models = false;
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

class Tracker {
public:
    std::vector<Person> update(const std::vector<Person>& detections);
    ~Tracker();
    Tracker(Tracker&&) noexcept;
    Tracker& operator=(Tracker&&) noexcept;

private:
    friend class Engine;
    explicit Tracker(naina_tracker_t* t) : h_(t) {}
    naina_tracker_t* h_ = nullptr;
};

class Engine {
public:
    explicit Engine(const Config& cfg = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Face stack
    std::vector<Face> detect_faces(const Image& img);
    std::vector<float> embed_face(const Image& img, const Face& f);
    float face_liveness(const Image& img, const Face& f);
    int face_embed_dim() const noexcept;

    // Person stack
    std::vector<Person> detect_persons(const Image& img);
    std::vector<float> embed_person(const Image& img, const Person& p);
    int reid_embed_dim() const noexcept;

    // Tracking
    Tracker make_tracker();

    // Pure
    static float similarity(const float* a, const float* b, int dim) noexcept {
        return naina_embed_similarity(a, b, dim);
    }

    naina_ctx_t* raw() noexcept { return ctx_; }

private:
    naina_ctx_t* ctx_ = nullptr;
};

}  // namespace naina

#endif  // NAINA_HPP
