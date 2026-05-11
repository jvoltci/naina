// NCNN backend (Tencent's edge inference engine).
//
// Compiled when -DNAINA_WITH_NCNN=ON. NCNN models are a (.param, .bin) pair;
// our load() takes the .param path and looks for a sibling .bin with the same
// basename. NCNN is float32-first (CHW layout); for v1 we only support F32
// I/O and return NAINA_E_UNSUPPORTED otherwise.

#include "naina/backend.hpp"

#include <net.h>  // NCNN: include dir is already .../include/ncnn

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace naina::backend {
namespace {

bool shape_is_nchw_or_smaller(const std::vector<int64_t>& s) {
    return s.size() >= 1 && s.size() <= 4;
}

// Map an NCHW (or smaller-rank) shape to NCNN (w, h, c) dimensions.
// NCNN is implicit-batch (1) and supports {1D, 2D, 3D, 4D} via Mat constructors;
// for simplicity we always pass through the 3D form when rank ≥ 3.
struct NcnnDims {
    int w;
    int h;
    int c;
    int rank;  // original tensor rank (informational)
};

NcnnDims to_ncnn_dims(const std::vector<int64_t>& s) {
    NcnnDims d{1, 1, 1, static_cast<int>(s.size())};
    // Treat leading dim as batch if rank==4 (assume N=1).
    size_t off = (s.size() == 4) ? 1 : 0;
    if (s.size() - off >= 1) {
        d.c = static_cast<int>(s[off]);
    }
    if (s.size() - off >= 2) {
        d.h = static_cast<int>(s[off + 1]);
    }
    if (s.size() - off >= 3) {
        d.w = static_cast<int>(s[off + 2]);
    }
    return d;
}

class NcnnSession final : public ISession {
public:
    NcnnSession(std::unique_ptr<ncnn::Net> net,
                std::vector<std::string> in_names,
                std::vector<std::string> out_names,
                std::vector<TensorDesc> in_descs,
                std::vector<TensorDesc> out_descs)
        : net_(std::move(net))
        , input_names_(std::move(in_names))
        , output_names_(std::move(out_names))
        , input_descs_(std::move(in_descs))
        , output_descs_(std::move(out_descs)) {}

    std::vector<TensorDesc> inputs() const override { return input_descs_; }
    std::vector<TensorDesc> outputs() const override { return output_descs_; }

    naina_status run(Span<const Tensor> ins, Span<Tensor> outs) override {
        if (ins.size() != input_names_.size() || outs.size() != output_names_.size()) {
            return NAINA_E_INVALID_ARG;
        }
        for (const auto& t : ins) {
            if (t.dtype() != DType::F32 || !shape_is_nchw_or_smaller(t.shape())) {
                return NAINA_E_UNSUPPORTED;
            }
        }

        ncnn::Extractor ex = net_->create_extractor();
        ex.set_light_mode(true);

        for (size_t i = 0; i < ins.size(); ++i) {
            const auto& t = ins[i];
            const NcnnDims d = to_ncnn_dims(t.shape());
            // ncnn::Mat wraps external memory; layout is CHW with elemsize=4 for F32.
            ncnn::Mat in_mat(d.w, d.h, d.c, const_cast<void*>(t.data()), sizeof(float));
            if (ex.input(input_names_[i].c_str(), in_mat) != 0) {
                return NAINA_E_INFERENCE_FAILED;
            }
        }

        for (size_t i = 0; i < outs.size(); ++i) {
            ncnn::Mat out_mat;
            if (ex.extract(output_names_[i].c_str(), out_mat) != 0) {
                return NAINA_E_INFERENCE_FAILED;
            }
            if (outs[i].dtype() != DType::F32) {
                return NAINA_E_INVALID_ARG;
            }
            const size_t bytes = static_cast<size_t>(out_mat.total()) * sizeof(float);
            if (bytes > outs[i].nbytes()) {
                return NAINA_E_INVALID_ARG;
            }
            std::memcpy(outs[i].data(), out_mat.data, bytes);
        }
        return NAINA_OK;
    }

private:
    std::unique_ptr<ncnn::Net> net_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<TensorDesc> input_descs_;
    std::vector<TensorDesc> output_descs_;
};

class NcnnBackend final : public IBackend {
public:
    std::string_view name() const override { return "ncnn"; }
    naina_backend id() const override { return NAINA_BACKEND_NCNN; }
    bool available() const override { return true; }

    std::unique_ptr<ISession> load(const std::filesystem::path& param_path,
                                   const SessionOptions& opts,
                                   naina_status* out_status) override {
        auto set = [&](naina_status s) {
            if (out_status != nullptr) {
                *out_status = s;
            }
        };

        std::filesystem::path bin_path = param_path;
        bin_path.replace_extension(".bin");

        auto net = std::make_unique<ncnn::Net>();
        net->opt.num_threads = opts.num_threads > 0 ? opts.num_threads : 0;
        net->opt.use_fp16_packed = opts.enable_fp16;
        net->opt.use_fp16_storage = opts.enable_fp16;
        net->opt.use_fp16_arithmetic = opts.enable_fp16;
        net->opt.use_int8_inference = opts.enable_int8;

        if (net->load_param(param_path.string().c_str()) != 0) {
            set(NAINA_E_IO);
            return nullptr;
        }
        if (net->load_model(bin_path.string().c_str()) != 0) {
            set(NAINA_E_IO);
            return nullptr;
        }

        // Capture I/O names. NCNN returns vectors of const char* owned by the
        // Net; we copy into std::string so they outlive any net mutations.
        std::vector<std::string> in_names;
        for (const char* n : net->input_names()) {
            in_names.emplace_back(n);
        }
        std::vector<std::string> out_names;
        for (const char* n : net->output_names()) {
            out_names.emplace_back(n);
        }

        // NCNN doesn't expose shapes statically — defer until the first run
        // populates concrete dims. For TensorDesc we record dtype=F32 and an
        // empty shape (meaning "dynamic").
        auto desc_for = [](const std::string& name) {
            TensorDesc d{};
            d.name = name;
            d.dtype = DType::F32;
            return d;
        };
        std::vector<TensorDesc> in_descs;
        in_descs.reserve(in_names.size());
        for (const auto& n : in_names) {
            in_descs.push_back(desc_for(n));
        }
        std::vector<TensorDesc> out_descs;
        out_descs.reserve(out_names.size());
        for (const auto& n : out_names) {
            out_descs.push_back(desc_for(n));
        }

        set(NAINA_OK);
        return std::make_unique<NcnnSession>(std::move(net),
                                             std::move(in_names),
                                             std::move(out_names),
                                             std::move(in_descs),
                                             std::move(out_descs));
    }
};

}  // namespace

NAINA_REGISTER_BACKEND(NcnnBackend)

}  // namespace naina::backend
