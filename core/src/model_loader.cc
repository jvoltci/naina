// Model registry & loader (parse + cache + verify; downloads stubbed).

#include "naina/model_loader.hpp"

#include "sha256.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace naina {

namespace fs = std::filesystem;

namespace {

// Resolve "${NAME:-fallback}" or "${NAME}" with environment variable lookup.
std::string substitute_env(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == '$' && s[i + 1] == '{') {
            const size_t end = s.find('}', i + 2);
            if (end == std::string::npos) {
                out.push_back(s[i]);
                ++i;
                continue;
            }
            const std::string spec = s.substr(i + 2, end - (i + 2));
            std::string name = spec;
            std::string fallback;
            const size_t colon = spec.find(":-");
            if (colon != std::string::npos) {
                name = spec.substr(0, colon);
                fallback = spec.substr(colon + 2);
            }
            const char* env = std::getenv(name.c_str());
            out.append(env != nullptr ? std::string(env) : fallback);
            i = end + 1;
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

fs::path expand_home(const std::string& s) {
    if (!s.empty() && s[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            return fs::path(home) / s.substr(s.size() > 1 && s[1] == '/' ? 2 : 1);
        }
    }
    return fs::path(s);
}

std::string substitute_release_base(const std::string& s, const std::string& release_base) {
    std::string out = s;
    const std::string key = "${release_base}";
    for (;;) {
        const size_t pos = out.find(key);
        if (pos == std::string::npos) {
            break;
        }
        out.replace(pos, key.size(), release_base);
    }
    return out;
}

Tier parse_tier(const std::string& s) {
    if (s == "research") {
        return Tier::Research;
    }
    return Tier::Default;
}

std::string basename_from_url(const std::string& url) {
    const size_t pos = url.find_last_of('/');
    return pos == std::string::npos ? url : url.substr(pos + 1);
}

bool sha256_unverified(const std::string& s) {
    return s.size() >= 3 && (s[0] == 'T' || s[0] == 't') && (s[1] == 'B' || s[1] == 'b') &&
           (s[2] == 'D' || s[2] == 'd');
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace

ModelRegistry ModelRegistry::load(const fs::path& yaml_path) {
    YAML::Node root = YAML::LoadFile(yaml_path.string());

    if (!root["schema_version"] || root["schema_version"].as<int>() != 1) {
        throw std::runtime_error("model registry: unsupported or missing schema_version");
    }

    ModelRegistry reg;
    std::string cache_root_raw = "${NAINA_CACHE:-~/.cache/naina/models}";
    reg.release_base_ = "";

    if (root["defaults"]) {
        const auto& d = root["defaults"];
        if (d["cache_root"]) {
            cache_root_raw = d["cache_root"].as<std::string>();
        }
        if (d["release_base"]) {
            reg.release_base_ = d["release_base"].as<std::string>();
        }
    }
    reg.cache_root_ = expand_home(substitute_env(cache_root_raw));

    if (!root["models"] || !root["models"].IsSequence()) {
        throw std::runtime_error("model registry: 'models' missing or not a list");
    }

    for (const auto& m : root["models"]) {
        ModelEntry entry;
        entry.id = m["id"].as<std::string>();
        entry.task = m["task"].as<std::string>();
        entry.tier = parse_tier(m["tier"].as<std::string>());
        entry.arch = m["arch"].as<std::string>("");
        entry.license = m["license"].as<std::string>("");

        if (m["files"]) {
            for (const auto& kv : m["files"]) {
                const auto kind = kv.first.as<std::string>();
                FileEntry fe;
                fe.url = substitute_release_base(kv.second["url"].as<std::string>(""),
                                                 reg.release_base_);
                fe.sha256 = lower(kv.second["sha256"].as<std::string>(""));
                fe.bytes = kv.second["bytes"].as<int64_t>(0);
                entry.files.emplace(kind, std::move(fe));
            }
        }
        reg.models_.push_back(std::move(entry));
    }

    return reg;
}

std::optional<ModelEntry> ModelRegistry::resolve(const std::string& task, Tier tier) const {
    for (const auto& m : models_) {
        if (m.task == task && m.tier == tier) {
            return m;
        }
    }
    return std::nullopt;
}

fs::path ModelRegistry::cache_path_for(const ModelEntry& m, const std::string& file_kind) const {
    const auto it = m.files.find(file_kind);
    if (it == m.files.end()) {
        return {};
    }
    const FileEntry& f = it->second;

    // Cache layout:  <cache_root>/<task>/<model_id>/<file_kind>__<digest-or-name>
    // The digest-or-name suffix means switching the manifest's sha256 forces
    // a re-fetch (because the path changes), without leaving stale bytes in
    // the cache for previous-id files.
    std::string suffix;
    if (sha256_unverified(f.sha256)) {
        suffix = basename_from_url(f.url);
    } else {
        suffix = f.sha256.substr(0, 16) + "__" + basename_from_url(f.url);
    }
    return cache_root_ / m.task / m.id / suffix;
}

naina_status ModelRegistry::ensure_local(const ModelEntry& m,
                                         const std::string& file_kind,
                                         fs::path* out_path) const {
    const auto it = m.files.find(file_kind);
    if (it == m.files.end()) {
        return NAINA_E_INVALID_ARG;
    }
    const FileEntry& f = it->second;
    const fs::path path = cache_path_for(m, file_kind);
    if (out_path != nullptr) {
        *out_path = path;
    }

    std::error_code ec;
    if (!fs::exists(path, ec) || fs::is_directory(path, ec)) {
        // Download is not implemented yet; the caller is expected to
        // pre-place the file at the returned path.
        return NAINA_E_MODEL_NOT_FOUND;
    }

    if (sha256_unverified(f.sha256)) {
        // Manifest hash is a placeholder; trust the on-disk file.
        return NAINA_OK;
    }

    try {
        const std::string actual = internal::sha256_file_hex(path);
        if (actual != f.sha256) {
            return NAINA_E_IO;
        }
    } catch (const std::exception&) {
        return NAINA_E_IO;
    }
    return NAINA_OK;
}

}  // namespace naina
