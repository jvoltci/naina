// Browser backend: naina's core, ONNX Runtime Web's execution.
//
// Compiled when -DNAINA_WITH_WASMJS=ON, which only makes sense under Emscripten.
//
// This exists because onnxruntime-web is a JavaScript library and cannot be
// static-linked into a WASM module. The alternative would have been to port
// naina's preprocessing, DB decode, rectification, CTC decode, layout mapping
// and document assembly to TypeScript — roughly 1,950 lines of arithmetic whose
// every knob (rounding mode, corner ordering, blank handling, score-before-
// unclip) fails *silently* when it diverges. Two implementations of that would
// make naina's one-core guarantee unenforceable.
//
// So the split is: naina keeps all of its own computation in the shared C++,
// and delegates only "execute this graph on these tensors" to JS. That is
// exactly the seam IBackend/ISession already draws for ONNX Runtime and NCNN.
//
// id() reports NAINA_BACKEND_ONNXRUNTIME because the enum names engine families,
// and onnxruntime-web is ONNX Runtime. It does NOT mean the numerics match a
// native ONNX Runtime build, and an earlier version of this comment wrongly
// claimed it did.
//
// Measured on an A4 page at the tiny tier: native macOS arm64 produced 35 text
// lines, this backend 33, with 33 character-identical. The two extra native
// lines were fragments where DBNet split a line at a blob that sits within
// float noise of the 0.3 binarize threshold — arm64 NEON kernels and WASM SIMD
// kernels land on opposite sides of it. Line segmentation can therefore differ,
// and because a fragment gets its own reading-order slot, word order can differ
// with it.
//
// So naina's byte-identical guarantee covers bindings over ONE backend build.
// Browser against server is close, not bit-exact. docs/ROADMAP.md states this.
//
// Async. ort-web's session.run() returns a Promise, while ISession::run() is
// synchronous. Emscripten's ASYNCIFY unwinds and rewinds the WASM stack across
// that await, which lets the C++ core stay completely unaware. It costs binary
// size; restructuring the core to be async would cost the guarantee.

#include "naina/backend.hpp"

#include <emscripten.h>

#include <cstdint>
#include <string>
#include <vector>

namespace naina::backend {
namespace {

// ─── JS bridge ─────────────────────────────────────────────────────────
//
// The JS side lives in bindings/wasm/src/runtime.mjs and installs itself as
// Module.__naina_ort before any naina call. Every function here is a thin
// marshaller; none of them make decisions.
//
// The whole region is fenced from clang-format. These macro bodies are
// JavaScript, and clang-format parses them as C++ and rewrites `===` into
// `== =` and `!==` into `!= =`. That still compiles cleanly; the damage only
// appears much later as an acorn SyntaxError inside Emscripten's JS optimiser,
// reported against generated code with no hint of the real cause. It cost a
// build to find. Do not remove the fence.
//
// clang-format off

// Create a session from a model already staged in the virtual filesystem.
// Returns a positive handle, or 0 on failure.
EM_ASYNC_JS(int, js_session_create, (const char* path, int device), {
    const rt = Module.__naina_ort;
    if (!rt) {
        console.error('naina: Module.__naina_ort is not installed');
        return 0;
    }
    try {
        return await rt.createSession(UTF8ToString(path), device);
    } catch (e) {
        console.error('naina: session create failed:', e);
        return 0;
    }
});

EM_JS(void, js_session_release, (int handle), { Module.__naina_ort ?.releaseSession(handle); });

// Whether the host page has installed the bridge.
//
// EM_JS rather than emscripten_run_script_int: the latter goes through eval(),
// which a page served with a strict Content-Security-Policy will refuse. EM_JS
// compiles to a direct import with no dynamic execution.
EM_JS(int, js_bridge_ready, (), {
    return (typeof Module.__naina_ort === 'object' && Module.__naina_ort !== null) ? 1
                                                                                             : 0;
});

// Tensor metadata is returned as JSON rather than through many small calls,
// because each EM_JS crossing is comparatively expensive and this happens once
// per session. Caller owns the returned buffer and frees it with free().
EM_JS(char*, js_session_io_json, (int handle, int want_outputs), {
    const rt = Module.__naina_ort;
    const json = rt ? rt.describeIo(handle, want_outputs !== 0) : '[]';
    const len = lengthBytesUTF8(json) + 1;
    const buf = _malloc(len);
    stringToUTF8(json, buf, len);
    return buf;
});

// Run the graph. Inputs and outputs are described by flat arrays that JS reads
// straight out of the WASM heap:
//
//   in_ptrs[i]   byte offset of input i's data
//   in_bytes[i]  its length in bytes
//   in_dtypes[i] naina DType as int
//   in_shapes    all input shapes concatenated, with in_ranks[i] dims each
//
// Outputs are pre-allocated by the caller — the same contract every other
// naina backend honours — so JS copies results into existing buffers and never
// allocates on naina's behalf.
EM_ASYNC_JS(int,
            js_session_run,
            (int handle,
             int n_in,
             const int32_t* in_ptrs,
             const int32_t* in_bytes,
             const int32_t* in_dtypes,
             const int32_t* in_ranks,
             const int64_t* in_shapes,
             int n_out,
             const int32_t* out_ptrs,
             const int32_t* out_bytes,
             const int32_t* out_dtypes),
            {
                const rt = Module.__naina_ort;
                if (!rt) {
                    return 1;
                }
                try {
                    return await rt.run(handle,
                                        n_in,
                                        in_ptrs,
                                        in_bytes,
                                        in_dtypes,
                                        in_ranks,
                                        in_shapes,
                                        n_out,
                                        out_ptrs,
                                        out_bytes,
                                        out_dtypes);
                } catch (e) {
                    console.error('naina: run failed:', e);
                    return 1;
                }
            });

// clang-format on

// ─── Minimal JSON reader ───────────────────────────────────────────────
//
// The IO description is machine-generated by our own JS on the line above, so
// it is a known shape:
//
//   [{"name":"image","dtype":1,"shape":[1,3,-1,-1]}, ...]
//
// A full JSON parser would be a dependency for one call site. This walks the
// three keys it needs and gives up loudly on anything else.

class TinyJson {
public:
    explicit TinyJson(const char* s) : s_(s == nullptr ? "" : s) {}

    // Advance to the next occurrence of `key` and return its raw value start,
    // or npos when exhausted.
    size_t find_key(const std::string& key, size_t from) const {
        const std::string pat = "\"" + key + "\":";
        const size_t at = s_.find(pat, from);
        return at == std::string::npos ? std::string::npos : at + pat.size();
    }

    std::string string_at(size_t pos) const {
        if (pos >= s_.size() || s_[pos] != '"') {
            return {};
        }
        const size_t end = s_.find('"', pos + 1);
        if (end == std::string::npos) {
            return {};
        }
        return s_.substr(pos + 1, end - pos - 1);
    }

    int64_t int_at(size_t pos) const {
        size_t i = pos;
        bool neg = false;
        if (i < s_.size() && s_[i] == '-') {
            neg = true;
            ++i;
        }
        int64_t v = 0;
        while (i < s_.size() && s_[i] >= '0' && s_[i] <= '9') {
            v = v * 10 + (s_[i] - '0');
            ++i;
        }
        return neg ? -v : v;
    }

    // Parse "[a,b,c]" starting at pos.
    std::vector<int64_t> int_array_at(size_t pos) const {
        std::vector<int64_t> out;
        if (pos >= s_.size() || s_[pos] != '[') {
            return out;
        }
        size_t i = pos + 1;
        while (i < s_.size() && s_[i] != ']') {
            if (s_[i] == '-' || (s_[i] >= '0' && s_[i] <= '9')) {
                out.push_back(int_at(i));
                if (s_[i] == '-') {
                    ++i;
                }
                while (i < s_.size() && s_[i] >= '0' && s_[i] <= '9') {
                    ++i;
                }
            } else {
                ++i;
            }
        }
        return out;
    }

private:
    std::string s_;
};

std::vector<TensorDesc> parse_io_json(const char* json) {
    std::vector<TensorDesc> out;
    if (json == nullptr) {
        return out;
    }
    TinyJson j(json);
    size_t cursor = 0;
    for (;;) {
        const size_t name_at = j.find_key("name", cursor);
        if (name_at == std::string::npos) {
            break;
        }
        TensorDesc d{};
        d.name = j.string_at(name_at);

        const size_t dtype_at = j.find_key("dtype", name_at);
        d.dtype = dtype_at == std::string::npos
                      ? DType::F32
                      : static_cast<DType>(static_cast<int>(j.int_at(dtype_at)));

        const size_t shape_at = j.find_key("shape", name_at);
        if (shape_at != std::string::npos) {
            d.shape = j.int_array_at(shape_at);
        }
        out.push_back(std::move(d));
        cursor = shape_at == std::string::npos ? name_at + 1 : shape_at + 1;
    }
    return out;
}

// ─── ISession ──────────────────────────────────────────────────────────

class WasmJsSession final : public ISession {
public:
    explicit WasmJsSession(int handle) : handle_(handle) {
        char* in_json = js_session_io_json(handle_, 0);
        inputs_ = parse_io_json(in_json);
        std::free(in_json);

        char* out_json = js_session_io_json(handle_, 1);
        outputs_ = parse_io_json(out_json);
        std::free(out_json);
    }

    ~WasmJsSession() override {
        if (handle_ != 0) {
            js_session_release(handle_);
        }
    }

    WasmJsSession(const WasmJsSession&) = delete;
    WasmJsSession& operator=(const WasmJsSession&) = delete;
    WasmJsSession(WasmJsSession&&) = delete;
    WasmJsSession& operator=(WasmJsSession&&) = delete;

    std::vector<TensorDesc> inputs() const override { return inputs_; }
    std::vector<TensorDesc> outputs() const override { return outputs_; }

    naina_status run(Span<const Tensor> ins, Span<Tensor> outs) override {
        if (ins.size() != inputs_.size() || outs.size() != outputs_.size()) {
            return NAINA_E_INVALID_ARG;
        }

        // Flatten both sides into plain arrays JS can read from the heap.
        const size_t ni = ins.size();
        const size_t no = outs.size();
        std::vector<int32_t> in_ptrs(ni);
        std::vector<int32_t> in_bytes(ni);
        std::vector<int32_t> in_dtypes(ni);
        std::vector<int32_t> in_ranks(ni);
        std::vector<int64_t> in_shapes;

        for (size_t i = 0; i < ni; ++i) {
            const Tensor& t = ins[i];
            if (t.data() == nullptr) {
                return NAINA_E_INVALID_ARG;
            }
            size_t elems = 1;
            for (const int64_t d : t.shape()) {
                if (d < 0) {
                    return NAINA_E_INVALID_ARG;
                }
                elems *= static_cast<size_t>(d);
            }
            in_ptrs[i] = static_cast<int32_t>(reinterpret_cast<uintptr_t>(t.data()));
            in_bytes[i] = static_cast<int32_t>(elems * dtype_size(t.dtype()));
            in_dtypes[i] = static_cast<int32_t>(t.dtype());
            in_ranks[i] = static_cast<int32_t>(t.shape().size());
            for (const int64_t d : t.shape()) {
                in_shapes.push_back(d);
            }
        }

        std::vector<int32_t> out_ptrs(no);
        std::vector<int32_t> out_bytes(no);
        std::vector<int32_t> out_dtypes(no);
        for (size_t i = 0; i < no; ++i) {
            const Tensor& t = outs[i];
            if (t.data() == nullptr) {
                return NAINA_E_INVALID_ARG;
            }
            size_t elems = 1;
            for (const int64_t d : t.shape()) {
                if (d < 0) {
                    return NAINA_E_INVALID_ARG;
                }
                elems *= static_cast<size_t>(d);
            }
            out_ptrs[i] = static_cast<int32_t>(reinterpret_cast<uintptr_t>(t.data()));
            out_bytes[i] = static_cast<int32_t>(elems * dtype_size(t.dtype()));
            out_dtypes[i] = static_cast<int32_t>(t.dtype());
        }

        // in_shapes may legitimately be empty only if every input is rank 0,
        // which no naina model has; guard the data() call regardless.
        static const int64_t kNoShapes = 0;
        const int rc = js_session_run(handle_,
                                      static_cast<int>(ni),
                                      in_ptrs.data(),
                                      in_bytes.data(),
                                      in_dtypes.data(),
                                      in_ranks.data(),
                                      in_shapes.empty() ? &kNoShapes : in_shapes.data(),
                                      static_cast<int>(no),
                                      out_ptrs.data(),
                                      out_bytes.data(),
                                      out_dtypes.data());
        return rc == 0 ? NAINA_OK : NAINA_E_INFERENCE_FAILED;
    }

private:
    int handle_ = 0;
    std::vector<TensorDesc> inputs_;
    std::vector<TensorDesc> outputs_;
};

// ─── IBackend ──────────────────────────────────────────────────────────

class WasmJsBackend final : public IBackend {
public:
    std::string_view name() const override { return "onnxruntime-web"; }

    // Not a new backend id: onnxruntime-web is ONNX Runtime. Reporting the same
    // id is what lets "same backend, same tier, same output" mean what it says
    // when one caller is a browser and the other is a server.
    naina_backend id() const override { return NAINA_BACKEND_ONNXRUNTIME; }

    // Available exactly when the host page has installed the bridge. A missing
    // bridge is a setup error worth reporting as unavailable rather than
    // failing later inside load().
    bool available() const override { return js_bridge_ready() != 0; }

    std::unique_ptr<ISession> load(const std::filesystem::path& model_path,
                                   const SessionOptions& opts,
                                   naina_status* out_status) override {
        const int handle =
            js_session_create(model_path.string().c_str(), static_cast<int>(opts.device));
        if (handle == 0) {
            if (out_status != nullptr) {
                *out_status = NAINA_E_BACKEND_UNAVAIL;
            }
            return nullptr;
        }
        if (out_status != nullptr) {
            *out_status = NAINA_OK;
        }
        return std::make_unique<WasmJsSession>(handle);
    }
};

}  // namespace

NAINA_REGISTER_BACKEND(WasmJsBackend)

}  // namespace naina::backend
