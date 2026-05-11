// naina — pluggable inference backend interface.
//
// Each backend (ONNX Runtime, NCNN, OpenVINO, CoreML, TensorRT, ...) implements
// IBackend and ISession. Backends self-register at static-init time via the
// NAINA_REGISTER_BACKEND macro; the registry selects the best available one
// at runtime based on the Config hint and capability probe.
#ifndef NAINA_BACKEND_HPP
#define NAINA_BACKEND_HPP

#include "naina.h"
#include "tensor.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace naina::backend {

struct TensorDesc {
    std::string name;
    std::vector<int64_t> shape;  // -1 for dynamic dims
    DType dtype;
};

struct SessionOptions {
    Device device = Device::Auto;
    int num_threads = 0;                                     // 0 = backend default
    bool enable_fp16 = true;                                 // backends that support it
    bool enable_int8 = false;                                // requires quantized weights
    std::vector<std::pair<std::string, std::string>> extra;  // backend-specific
};

// Compiled model + execution state. One session per (model × device).
class ISession {
public:
    virtual ~ISession() = default;

    virtual std::vector<TensorDesc> inputs() const = 0;
    virtual std::vector<TensorDesc> outputs() const = 0;

    // Synchronous run. Tensors must match inputs()/outputs() in count and dtype;
    // shapes may resolve dynamic dims. Backend owns no tensor memory.
    virtual naina_status run(Span<const Tensor> inputs, Span<Tensor> outputs) = 0;
};

// Backend = factory for ISessions, plus a capability probe.
class IBackend {
public:
    virtual ~IBackend() = default;

    virtual std::string_view name() const = 0;  // "onnxruntime", "ncnn", ...
    virtual naina_backend id() const = 0;       // matches the C enum
    virtual bool available() const = 0;         // runtime probe

    // Model path is what the manifest points at. Backend interprets it
    // (.onnx, .param/.bin pair, .mlmodelc, .engine, ...).
    virtual std::unique_ptr<ISession> load(const std::filesystem::path& model_path,
                                           const SessionOptions& opts,
                                           naina_status* out_status) = 0;
};

// ─── Registry ──────────────────────────────────────────────────────────

using BackendFactory = std::function<std::unique_ptr<IBackend>()>;

// Called by NAINA_REGISTER_BACKEND at static-init time. Safe to call before
// main(); thread-safe to call concurrently.
void register_backend(BackendFactory factory);

// Pick the best available backend given a hint. Returns nullptr if none match.
// Selection rules:
//   1. If `hint != Auto` and that backend is available, return it.
//   2. Otherwise consult the per-platform priority list (see backend_registry.cc).
//   3. Fall back to ONNX Runtime CPU if compiled in.
IBackend* select_best(naina_backend hint, Device device_hint);

// Enumerate available backends (for diagnostics / CLI).
std::vector<IBackend*> available_backends();

}  // namespace naina::backend

// ─── Self-registration macro ───────────────────────────────────────────
//
// Use once per backend translation unit:
//
//     NAINA_REGISTER_BACKEND(OnnxRuntimeBackend);
//
// where OnnxRuntimeBackend is a class deriving from IBackend with a default
// constructor.
#define NAINA_REGISTER_BACKEND(ClassName)                                                     \
    namespace {                                                                               \
    struct ClassName##_Registrar {                                                            \
        ClassName##_Registrar() {                                                             \
            ::naina::backend::register_backend(                                               \
                [] { return std::unique_ptr<::naina::backend::IBackend>(new ClassName()); }); \
        }                                                                                     \
    };                                                                                        \
    static ClassName##_Registrar g_##ClassName##_registrar;                                   \
    }

#endif  // NAINA_BACKEND_HPP
