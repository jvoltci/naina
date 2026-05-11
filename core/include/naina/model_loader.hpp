// naina — model registry & loader.
//
// Parses models/registry.yaml, resolves an entry by (task, tier), and
// returns a verified local path to a weights file. Downloads from the
// manifest URL are NOT YET IMPLEMENTED — for now the loader either uses
// a file already in the cache (and verifies its sha256) or returns
// NAINA_E_MODEL_NOT_FOUND with the expected cache path so callers can
// pre-place the file.

#ifndef NAINA_MODEL_LOADER_HPP
#define NAINA_MODEL_LOADER_HPP

#include "naina/naina.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace naina {

enum class Tier { Default, Research };

struct FileEntry {
    std::string url;
    std::string sha256;  // hex, lowercase. The literal "TBD..." means unverified.
    int64_t bytes = 0;
};

struct ModelEntry {
    std::string id;    // e.g. "face_detect.default"
    std::string task;  // "face_detect" | "face_embed" | ...
    Tier tier = Tier::Default;
    std::string arch;
    std::string license;
    std::unordered_map<std::string, FileEntry> files;  // key: "onnx", "ncnn_param", ...
};

class ModelRegistry {
public:
    // Loads the registry from a YAML file. Resolves ${release_base} and
    // ${cache_root} placeholders. Throws on parse error.
    static ModelRegistry load(const std::filesystem::path& yaml_path);

    // Find a model by (task, tier). Returns nullopt if not present.
    std::optional<ModelEntry> resolve(const std::string& task, Tier tier) const;

    // Compute the local cache path for a given model's file kind ("onnx",
    // "ncnn_param", "ncnn_bin", etc). The path is deterministic from the
    // sha256 (or, while sha256 is "TBD…", from the URL basename) — same
    // file is cached at the same path across runs.
    std::filesystem::path cache_path_for(const ModelEntry& m, const std::string& file_kind) const;

    // Ensure the file is on disk and verified.
    //   - If the file exists and sha256 matches the manifest → NAINA_OK
    //   - If the file is missing → NAINA_E_MODEL_NOT_FOUND (downloads TBD)
    //   - If sha256 mismatches → NAINA_E_IO
    //   - If file_kind not in manifest → NAINA_E_INVALID_ARG
    naina_status ensure_local(const ModelEntry& m,
                              const std::string& file_kind,
                              std::filesystem::path* out_path) const;

    const std::filesystem::path& cache_root() const { return cache_root_; }
    const std::vector<ModelEntry>& all() const { return models_; }

private:
    std::filesystem::path cache_root_;
    std::string release_base_;
    std::vector<ModelEntry> models_;
};

}  // namespace naina

#endif  // NAINA_MODEL_LOADER_HPP
