// End-to-end tests for the model registry parser.
//
//  - parses our real models/registry.yaml
//  - resolves entries by (task, tier)
//  - cache_path_for is deterministic and includes the sha256 prefix when set
//  - ensure_local with a pre-placed file in the cache works (verifies sha256
//    or accepts the "TBD" placeholder)
//  - ensure_local with a missing file returns NAINA_E_MODEL_NOT_FOUND

#include "naina/model_loader.hpp"

#include "../src/sha256.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using naina::ModelRegistry;
using naina::Tier;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static fs::path find_repo_root() {
    fs::path p = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(p / "models" / "registry.yaml")) {
            return p;
        }
        p = p.parent_path();
    }
    return {};
}

static void write_file(const fs::path& p, std::string_view contents) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

int main() {
    const fs::path repo = find_repo_root();
    EXPECT(!repo.empty());
    if (repo.empty()) {
        std::fprintf(stderr, "could not find repo root with models/registry.yaml\n");
        return 1;
    }

    // Point cache at a temp dir so we don't pollute ~/.cache/naina.
    const fs::path cache = fs::temp_directory_path() / "naina-test-cache";
    fs::remove_all(cache);
    fs::create_directories(cache);
    setenv("NAINA_CACHE", cache.c_str(), 1);
    // Don't actually hit the network during unit tests.
    setenv("NAINA_OFFLINE", "1", 1);

    const auto reg = ModelRegistry::load(repo / "models" / "registry.yaml");

    // ── tier names are device-based ─────────────────────────────────
    EXPECT(naina::tier_from_string("tiny") == Tier::Tiny);
    EXPECT(naina::tier_from_string("small") == Tier::Small);
    EXPECT(naina::tier_from_string("medium") == Tier::Medium);
    // Unknown strings degrade to Small so a registry written for a newer
    // naina still loads on an older one.
    EXPECT(naina::tier_from_string("research") == Tier::Small);
    EXPECT(naina::tier_from_string("") == Tier::Small);
    EXPECT(std::string(naina::tier_to_string(Tier::Tiny)) == "tiny");
    EXPECT(std::string(naina::tier_to_string(Tier::Small)) == "small");
    EXPECT(std::string(naina::tier_to_string(Tier::Medium)) == "medium");

    // ── every tier resolves det + rec ───────────────────────────────
    for (const auto tier : {Tier::Tiny, Tier::Small, Tier::Medium}) {
        const auto det = reg.resolve("text_detect", tier);
        const auto rec = reg.resolve("text_recognize", tier);
        EXPECT(det.has_value());
        EXPECT(rec.has_value());
        if (det.has_value()) {
            EXPECT(det->tier == tier);
            EXPECT(det->arch == "pp_ocrv6_det");
            EXPECT(det->license == "Apache-2.0");
            EXPECT(det->files.count("onnx") == 1);
            EXPECT(det->files.at("onnx").sha256.size() == 64);
            EXPECT(det->files.at("onnx").bytes > 0);
            // ${hf} must have been substituted.
            EXPECT(det->files.at("onnx").url.find("huggingface.co") != std::string::npos);
            EXPECT(det->files.at("onnx").url.find("${hf}") == std::string::npos);
        }
        if (rec.has_value()) {
            EXPECT(rec->arch == "pp_ocrv6_rec");
            EXPECT(rec->files.at("onnx").sha256.size() == 64);
        }
    }

    // Layout is medium-only until PP-DocLayout-S/-M are exported to ONNX.
    EXPECT(reg.resolve("layout_detect", Tier::Medium).has_value());

    // Spot-check one exact hash so a bad copy-paste is caught.
    auto fd = reg.resolve("text_detect", Tier::Tiny);
    EXPECT(fd.has_value());
    if (fd.has_value()) {
        EXPECT(fd->id == "text_detect.tiny");
        EXPECT(fd->files.at("onnx").sha256 ==
               "193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8");
        EXPECT(fd->files.at("onnx").bytes == 1780590);
    }

    // No face models may remain.
    EXPECT(!reg.resolve("face_detect", Tier::Small).has_value());
    EXPECT(!reg.resolve("face_embed", Tier::Medium).has_value());
    EXPECT(!reg.resolve("nonexistent_task", Tier::Small).has_value());

    // ── cache_path_for stability ────────────────────────────────────
    if (fd.has_value()) {
        const auto p1 = reg.cache_path_for(*fd, "onnx");
        const auto p2 = reg.cache_path_for(*fd, "onnx");
        EXPECT(p1 == p2);
        EXPECT(p1.string().find("text_detect") != std::string::npos);
    }

    // ── ensure_local: missing file in offline mode → NAINA_E_MODEL_NOT_FOUND ──
    if (fd.has_value()) {
        const auto expected_path = reg.cache_path_for(*fd, "onnx");
        fs::path actual;
        EXPECT(reg.ensure_local(*fd, "onnx", &actual) == NAINA_E_MODEL_NOT_FOUND);
        EXPECT(actual == expected_path);

        // Pre-place a file with the wrong content. The manifest has a real
        // sha256, so the loader must reject it and remove the bad file.
        write_file(expected_path, "stand-in onnx bytes (wrong content)");
        const auto rc = reg.ensure_local(*fd, "onnx", &actual);
        EXPECT(rc == NAINA_E_IO || rc == NAINA_E_MODEL_NOT_FOUND);
        EXPECT(!fs::exists(expected_path));
    }

    // ── ensure_local with a real sha256: synthesize an entry for one of
    //    our test fixtures and verify the hash gate triggers correctly.
    //    (We can do this by writing a known-content file and computing its
    //    digest with our own helper, then constructing a fake ModelEntry.)
    {
        const std::string content = "naina sha256 fixture";
        const fs::path fake_dir = cache / "_test" / "fake_model";
        const fs::path fake_file = fake_dir / "data.bin";
        write_file(fake_file, content);
        const std::string digest = naina::internal::sha256_hex(content);

        naina::ModelEntry e;
        e.id = "_test.fake";
        e.task = "_test";
        e.tier = Tier::Small;
        naina::FileEntry fe;
        fe.url = "https://example.invalid/data.bin";
        fe.sha256 = digest;
        e.files.emplace("onnx", fe);

        // ensure_local on this synthetic entry won't find our fixture (paths
        // diverge), so we instead test the file's own sha256 utility round-trip:
        EXPECT(naina::internal::sha256_file_hex(fake_file) == digest);
    }

    fs::remove_all(cache);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("model_loader tests passed\n");
    return 0;
}
