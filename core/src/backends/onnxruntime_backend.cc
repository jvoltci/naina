// ONNX Runtime backend.
//
// Compiled when -DNAINA_WITH_ONNXRUNTIME=ON. Wraps Ort::Session, exposes it
// through the naina::backend::IBackend interface, and self-registers via
// NAINA_REGISTER_BACKEND at static-init time.
//
// Execution provider selection: we ask ORT to enable the GPU EPs that the
// build supports; ORT silently falls back to CPU when an EP isn't available
// at runtime, which keeps single-binary distributions sane.

#include "naina/backend.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace naina::backend {
namespace {

DType ort_to_dtype(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:    return DType::F32;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:  return DType::F16;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16: return DType::BF16;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:    return DType::I64;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:    return DType::I32;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:    return DType::I16;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:     return DType::I8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:    return DType::U8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:     return DType::Bool;
        default:                                     return DType::F32;
    }
}

ONNXTensorElementDataType dtype_to_ort(DType d) {
    switch (d) {
        case DType::F32:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        case DType::F16:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
        case DType::BF16: return ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16;
        case DType::I64:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
        case DType::I32:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;
        case DType::I16:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16;
        case DType::I8:   return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8;
        case DType::U8:   return ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
        case DType::Bool: return ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
    }
    return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
}

class OnnxRuntimeSession final : public ISession {
public:
    OnnxRuntimeSession(Ort::Session sess, Ort::AllocatorWithDefaultOptions allocator)
        : sess_(std::move(sess)) {
        const size_t in_count = sess_.GetInputCount();
        inputs_.reserve(in_count);
        input_name_holders_.reserve(in_count);
        input_names_.reserve(in_count);
        for (size_t i = 0; i < in_count; ++i) {
            auto held = sess_.GetInputNameAllocated(i, allocator);
            input_name_holders_.push_back(std::string(held.get()));
            input_names_.push_back(input_name_holders_.back().c_str());
            inputs_.push_back(describe(input_name_holders_.back(), sess_.GetInputTypeInfo(i)));
        }
        const size_t out_count = sess_.GetOutputCount();
        outputs_.reserve(out_count);
        output_name_holders_.reserve(out_count);
        output_names_.reserve(out_count);
        for (size_t i = 0; i < out_count; ++i) {
            auto held = sess_.GetOutputNameAllocated(i, allocator);
            output_name_holders_.push_back(std::string(held.get()));
            output_names_.push_back(output_name_holders_.back().c_str());
            outputs_.push_back(describe(output_name_holders_.back(), sess_.GetOutputTypeInfo(i)));
        }
    }

    std::vector<TensorDesc> inputs() const override { return inputs_; }
    std::vector<TensorDesc> outputs() const override { return outputs_; }

    naina_status run(Span<const Tensor> ins, Span<Tensor> outs) override {
        if (ins.size() != inputs_.size() || outs.size() != outputs_.size()) {
            return NAINA_E_INVALID_ARG;
        }
        try {
            Ort::MemoryInfo mem_info =
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<Ort::Value> in_values;
            in_values.reserve(ins.size());
            for (size_t i = 0; i < ins.size(); ++i) {
                const auto& t = ins[i];
                in_values.push_back(Ort::Value::CreateTensor(
                    mem_info,
                    const_cast<void*>(t.data()),       // ORT takes non-const but doesn't mutate
                    t.nbytes(),
                    t.shape().data(),
                    t.shape().size(),
                    dtype_to_ort(t.dtype())));
            }

            auto out_values = sess_.Run(Ort::RunOptions{nullptr},
                                        input_names_.data(),
                                        in_values.data(),
                                        in_values.size(),
                                        output_names_.data(),
                                        output_names_.size());

            // Copy outputs into caller-allocated tensors. Shape/dtype must match.
            for (size_t i = 0; i < out_values.size(); ++i) {
                auto info = out_values[i].GetTensorTypeAndShapeInfo();
                if (ort_to_dtype(info.GetElementType()) != outs[i].dtype()) {
                    return NAINA_E_INVALID_ARG;
                }
                const size_t bytes =
                    info.GetElementCount() * dtype_size(outs[i].dtype());
                if (bytes > outs[i].nbytes()) {
                    return NAINA_E_INVALID_ARG;
                }
                std::memcpy(outs[i].data(),
                            out_values[i].GetTensorRawData(),
                            bytes);
            }
            return NAINA_OK;
        } catch (const Ort::Exception&) {
            return NAINA_E_INFERENCE_FAILED;
        }
    }

private:
    static TensorDesc describe(const std::string& name, Ort::TypeInfo info) {
        TensorDesc d{};
        d.name = name;
        auto t = info.GetTensorTypeAndShapeInfo();
        d.shape = t.GetShape();
        d.dtype = ort_to_dtype(t.GetElementType());
        return d;
    }

    Ort::Session sess_;
    std::vector<TensorDesc> inputs_;
    std::vector<TensorDesc> outputs_;
    std::vector<std::string> input_name_holders_;
    std::vector<std::string> output_name_holders_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
};

class OnnxRuntimeBackend final : public IBackend {
public:
    std::string_view name() const override { return "onnxruntime"; }
    naina_backend id() const override { return NAINA_BACKEND_ONNXRUNTIME; }
    bool available() const override { return true; }  // compiled in ⇒ available

    std::unique_ptr<ISession> load(const std::filesystem::path& model_path,
                                   const SessionOptions& opts,
                                   naina_status* out_status) override {
        try {
            Ort::SessionOptions ort_opts;
            if (opts.num_threads > 0) {
                ort_opts.SetIntraOpNumThreads(opts.num_threads);
            }
            ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            // ORT silently ignores EP appends if the EP isn't compiled in,
            // so it's safe to ask for the lot.
            try { ort_opts.AppendExecutionProvider("CoreML", {}); } catch (...) {}
            try { ort_opts.AppendExecutionProvider("CUDA",   {}); } catch (...) {}
            try { ort_opts.AppendExecutionProvider("ROCm",   {}); } catch (...) {}

#if defined(_WIN32)
            std::wstring wpath = model_path.wstring();
            Ort::Session sess(env(), wpath.c_str(), ort_opts);
#else
            Ort::Session sess(env(), model_path.c_str(), ort_opts);
#endif
            Ort::AllocatorWithDefaultOptions alloc;
            if (out_status != nullptr) {
                *out_status = NAINA_OK;
            }
            return std::make_unique<OnnxRuntimeSession>(std::move(sess), alloc);
        } catch (const Ort::Exception&) {
            if (out_status != nullptr) {
                *out_status = NAINA_E_INFERENCE_FAILED;
            }
            return nullptr;
        } catch (const std::exception&) {
            if (out_status != nullptr) {
                *out_status = NAINA_E_IO;
            }
            return nullptr;
        }
    }

private:
    static Ort::Env& env() {
        static Ort::Env e(ORT_LOGGING_LEVEL_WARNING, "naina");
        return e;
    }
};

}  // namespace

NAINA_REGISTER_BACKEND(OnnxRuntimeBackend)

}  // namespace naina::backend
