// Model registry & loader (parse + cache + verify; downloads stubbed).

#include "naina/model_loader.hpp"

#include "sha256.hpp"

#ifndef NAINA_NO_CURL
#include <curl/curl.h>
#endif
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
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

std::string substitute_placeholder(std::string s, std::string_view key, const std::string& value) {
    for (;;) {
        const size_t pos = s.find(key);
        if (pos == std::string::npos) {
            break;
        }
        s.replace(pos, key.size(), value);
    }
    return s;
}

std::string substitute_release_base(const std::string& s, const std::string& release_base) {
    return substitute_placeholder(s, "${release_base}", release_base);
}

std::string substitute_hf(const std::string& s, const std::string& hf) {
    return substitute_placeholder(s, "${hf}", hf);
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

#ifdef NAINA_NO_CURL

// Builds with no network layer in the core: Emscripten (no sockets) and Android
// (the NDK ships no libcurl). On both, the host owns fetching (JS through the
// Cache API, or Dart through http) and writes the bytes into the path
// cache_path_for() produces. ensure_local then finds the file present and runs
// the SAME sha256 verification as every other platform, so these targets get the
// same integrity guarantee rather than a weaker one.
//
// Reaching here means the host did not stage the file, which is a caller error
// rather than a network condition, so report it as a missing model.
naina_status download_atomic(const std::string&, const fs::path&) {
    return NAINA_E_MODEL_NOT_FOUND;
}

#else

// One-time libcurl global init. Thread-safe via std::call_once.
void curl_global_init_once() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t curl_write_to_file(void* buf, size_t size, size_t nmemb, void* user) {
    auto* fp = static_cast<std::FILE*>(user);
    return std::fwrite(buf, size, nmemb, fp);
}

// Progress to stderr, one line per 4 MiB and one at the end.
//
// Why this exists: on 2026-09-22 a medium-tier read looked hung for twenty
// minutes. It was not hung. The host was serving at 16 KB/s, which is above
// curl's 1 KB/s low-speed floor, so a 129 MB model was a two-hour download
// with no output at all. A silent two-hour wait is indistinguishable from a
// crash, which is this repository's oldest failure mode.
//
// Set NAINA_QUIET=1 to suppress. Nothing is printed for files under 4 MiB, so
// charsets and tiny weights stay silent.
struct ProgressState {
    std::string name;
    curl_off_t last_reported = 0;
    bool announced = false;
};

int curl_progress(void* user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* st = static_cast<ProgressState*>(user);
    constexpr curl_off_t kStep = 4LL * 1024 * 1024;
    if (dltotal > 0 && !st->announced && dltotal >= kStep) {
        std::fprintf(stderr,
                     "[naina] downloading %s, %.1f MB\n",
                     st->name.c_str(),
                     static_cast<double>(dltotal) / (1024.0 * 1024.0));
        st->announced = true;
    }
    if (st->announced && dlnow - st->last_reported >= kStep) {
        st->last_reported = dlnow;
        const double pct =
            dltotal > 0 ? (100.0 * static_cast<double>(dlnow) / static_cast<double>(dltotal)) : 0.0;
        std::fprintf(stderr,
                     "[naina]   %s %.0f%% (%.1f of %.1f MB)\n",
                     st->name.c_str(),
                     pct,
                     static_cast<double>(dlnow) / (1024.0 * 1024.0),
                     static_cast<double>(dltotal) / (1024.0 * 1024.0));
    }
    return 0;  // never abort from here; the low-speed limit does that
}

// Download `url` → `dest` atomically (writes to dest+".part", renames on
// success). Returns NAINA_OK or an error code.
naina_status download_atomic(const std::string& url, const fs::path& dest) {
    curl_global_init_once();

    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);

    const fs::path tmp = dest.string() + ".part";
    std::FILE* fp = std::fopen(tmp.string().c_str(), "wb");
    if (fp == nullptr) {
        return NAINA_E_IO;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        std::fclose(fp);
        std::remove(tmp.string().c_str());
        return NAINA_E_IO;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "naina/0.1 (+https://github.com/jvoltci/naina)");

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    ProgressState progress{dest.filename().string(), 0, false};
    const bool quiet = std::getenv("NAINA_QUIET") != nullptr;
    if (!quiet) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    }

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    std::fclose(fp);

    if (rc != CURLE_OK) {
        // Say what failed and where. The previous version returned the same
        // code from both branches of a ternary and threw the URL, the HTTP
        // status and curl's own message away, which left the caller with
        // "an IO error happened somewhere".
        std::fprintf(stderr,
                     "[naina] download failed: %s\n[naina]   url: %s\n"
                     "[naina]   curl: %s%s%s\n",
                     dest.filename().string().c_str(),
                     url.c_str(),
                     curl_easy_strerror(rc),
                     errbuf[0] != 0 ? ": " : "",
                     errbuf);
        if (http_code != 0) {
            std::fprintf(stderr, "[naina]   http: %ld\n", http_code);
        }
        std::remove(tmp.string().c_str());
        // Both cases are NAINA_E_IO because the C ABI has no timeout code and
        // adding one is an ABI change. The distinction now lives in the
        // message above, where a person can read it.
        return NAINA_E_IO;
    }
    if (!quiet && progress.announced) {
        std::fprintf(stderr, "[naina] downloaded %s\n", dest.filename().string().c_str());
    }

    // Atomic move into final location.
    fs::rename(tmp, dest, ec);
    if (ec) {
        std::remove(tmp.string().c_str());
        return NAINA_E_IO;
    }
    return NAINA_OK;
}

#endif  // NAINA_NO_CURL

}  // namespace

Tier tier_from_string(const std::string& s) {
    if (s == "tiny") {
        return Tier::Tiny;
    }
    if (s == "medium") {
        return Tier::Medium;
    }
    return Tier::Small;
}

const char* tier_to_string(Tier t) {
    switch (t) {
        case Tier::Tiny:
            return "tiny";
        case Tier::Medium:
            return "medium";
        case Tier::Small:
            break;
    }
    return "small";
}

ModelRegistry ModelRegistry::load(const fs::path& yaml_path) {
    YAML::Node root = YAML::LoadFile(yaml_path.string());

    if (!root["schema_version"] || root["schema_version"].as<int>() != 1) {
        throw std::runtime_error("model registry: unsupported or missing schema_version");
    }

    ModelRegistry reg;
    std::string cache_root_raw = "${NAINA_CACHE:-~/.cache/naina/models}";
    reg.release_base_ = "";
    std::string hf;

    if (root["defaults"]) {
        const auto& d = root["defaults"];
        if (d["cache_root"]) {
            cache_root_raw = d["cache_root"].as<std::string>();
        }
        if (d["release_base"]) {
            reg.release_base_ = d["release_base"].as<std::string>();
        }
        if (d["hf"]) {
            hf = d["hf"].as<std::string>();
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
        entry.tier = tier_from_string(m["tier"] ? m["tier"].as<std::string>() : "small");
        entry.lang = m["lang"] ? m["lang"].as<std::string>() : "";
        entry.arch = m["arch"].as<std::string>("");
        entry.license = m["license"].as<std::string>("");

        if (m["files"]) {
            for (const auto& kv : m["files"]) {
                const auto kind = kv.first.as<std::string>();
                FileEntry fe;
                fe.url = substitute_hf(substitute_release_base(kv.second["url"].as<std::string>(""),
                                                               reg.release_base_),
                                       hf);
                // Provenance is stored but never fetched; see FileEntry.
                fe.source_url = substitute_hf(kv.second["source_url"].as<std::string>(""), hf);
                fe.sha256 = lower(kv.second["sha256"].as<std::string>(""));
                fe.bytes = kv.second["bytes"].as<int64_t>(0);
                entry.files.emplace(kind, std::move(fe));
            }
        }
        // Detection parameters. Absent keys keep the struct's defaults, which
        // equal the values every text_detect entry has carried since v0.1.
        if (entry.task == "text_detect") {
            const YAML::Node pre = m["input"] ? m["input"]["preprocess"] : YAML::Node();
            if (pre) {
                if (const YAML::Node rs = pre["resize"]; rs) {
                    entry.det.limit = rs["limit"].as<int32_t>(entry.det.limit);
                    entry.det.multiple_of = rs["multiple_of"].as<int32_t>(entry.det.multiple_of);
                    entry.det.filter = rs["filter"].as<std::string>("");
                }
                const auto triple = [](const YAML::Node& n, float out[3]) {
                    if (n && n.IsSequence() && n.size() == 3) {
                        for (std::size_t i = 0; i < 3; ++i) {
                            out[i] = n[i].as<float>();
                        }
                    }
                };
                triple(pre["scale"], entry.det.scale);
                triple(pre["mean"], entry.det.mean);
                triple(pre["std"], entry.det.std_);
            }
            const YAML::Node post = m["output"] ? m["output"]["postprocess"] : YAML::Node();
            if (post) {
                entry.det.thresh = post["thresh"].as<float>(entry.det.thresh);
                entry.det.box_thresh = post["box_thresh"].as<float>(entry.det.box_thresh);
                entry.det.unclip_ratio = post["unclip_ratio"].as<float>(entry.det.unclip_ratio);
                entry.det.max_candidates =
                    post["max_candidates"].as<int32_t>(entry.det.max_candidates);
            }
        }
        if (entry.task == "layout_detect") {
            const YAML::Node pre = m["input"] ? m["input"]["preprocess"] : YAML::Node();
            if (pre && pre["resize"]) {
                entry.det.filter = pre["resize"]["filter"].as<std::string>("");
            }
        }
        reg.models_.push_back(std::move(entry));
    }

    return reg;
}

std::optional<ModelEntry> ModelRegistry::resolve(const std::string& task,
                                                 Tier tier,
                                                 const std::string& lang) const {
    for (const auto& m : models_) {
        if (m.task == task && m.tier == tier && m.lang == lang) {
            return m;
        }
    }

    // A language-specific model may exist at only one tier: upstream ships
    // Devanagari in a "mobile" size only. Rather than fail, fall back across
    // TIERS for the same language, which changes model size but never the
    // alphabet. Falling back across LANGUAGES is what we must never do.
    if (!lang.empty()) {
        for (const auto& m : models_) {
            if (m.task == task && m.lang == lang) {
                return m;
            }
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
    const bool exists = fs::exists(path, ec) && !fs::is_directory(path, ec);

    auto verify_or_remove = [&]() -> naina_status {
        if (sha256_unverified(f.sha256)) {
            return NAINA_OK;
        }
        try {
            if (internal::sha256_file_hex(path) == f.sha256) {
                return NAINA_OK;
            }
        } catch (const std::exception&) {
            // fall through and try to recover
        }
        // Hash mismatch or unreadable: remove and signal IO error.
        std::error_code rmec;
        fs::remove(path, rmec);
        return NAINA_E_IO;
    };

    if (exists) {
        const naina_status s = verify_or_remove();
        if (s == NAINA_OK) {
            return NAINA_OK;
        }
        // Fall through to re-download if hash mismatched.
    }

    if (f.url.empty()) {
        return NAINA_E_MODEL_NOT_FOUND;
    }

    // NAINA_OFFLINE=1 disables network, useful for tests and air-gapped runs.
    if (const char* off = std::getenv("NAINA_OFFLINE"); off != nullptr && off[0] != '0') {
        return NAINA_E_MODEL_NOT_FOUND;
    }

    const naina_status dl = download_atomic(f.url, path);
    if (dl != NAINA_OK) {
        return dl;
    }
    return verify_or_remove();
}

}  // namespace naina
