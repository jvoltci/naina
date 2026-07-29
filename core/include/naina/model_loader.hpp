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

// Device tier, not a licence tier. Every OCR model naina ships is
// Apache-2.0; what differs is size and the hardware it suits.
//   Tiny   ~11 MB total  — browser, phone, Pi Zero
//   Small  ~54 MB total  — laptop, Pi 5, mobile app
//   Medium ~269 MB total — server, desktop
enum class Tier { Tiny, Small, Medium };

// Parse a manifest tier string. Unknown values map to Small so a registry
// written for a newer naina still loads on an older one.
Tier tier_from_string(const std::string& s);

// Inverse of tier_from_string. Returns a static string, never null.
const char* tier_to_string(Tier t);

struct FileEntry {
    std::string url;
    std::string sha256;  // hex, lowercase. The literal "TBD..." means unverified.
    int64_t bytes = 0;

    // Provenance only — NEVER fetched. Records the upstream artifact these
    // mirrored bytes came from, so the chain of custody stays auditable.
    // naina serves weights from its own release so an upstream re-tag or
    // deletion cannot break installs; see NOTICE.
    std::string source_url;
};

struct ModelEntry {
    std::string id;    // e.g. "text_recognize.tiny"
    std::string task;  // "text_detect" | "text_recognize" | "layout_detect"
    Tier tier = Tier::Small;

    // Recognition alphabet, e.g. "devanagari". Empty means the default
    // (Latin + CJK). Only recognition models carry this: detection and layout
    // are script-agnostic, so they are shared across languages rather than
    // duplicated per script.
    std::string lang;
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
    // Resolve by task, tier and language.
    //
    // `lang` empty selects the entry with no `lang` (the default alphabet). A
    // non-empty `lang` matches only an entry declaring exactly that language —
    // there is deliberately NO fallback to the default, because silently
    // recognising Devanagari with a Latin alphabet is the bug this exists to
    // fix. Callers get nullopt and should surface NAINA_E_UNSUPPORTED.
    std::optional<ModelEntry> resolve(const std::string& task,
                                      Tier tier,
                                      const std::string& lang = "") const;

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

    // Override where weights are cached, ignoring the manifest's default.
    //
    // Needed because `models_root` in naina_config is what callers actually mean
    // by "put the models here", while the manifest's cache_root defaults to
    // ~/.cache — which on Android expands to a literal "~" on a read-only
    // filesystem, since there is no HOME.
    void set_cache_root(std::filesystem::path root) { cache_root_ = std::move(root); }
    const std::vector<ModelEntry>& all() const { return models_; }

private:
    std::filesystem::path cache_root_;
    std::string release_base_;
    std::vector<ModelEntry> models_;
};

}  // namespace naina

#endif  // NAINA_MODEL_LOADER_HPP
