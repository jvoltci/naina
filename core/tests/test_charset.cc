// Charset parsing from a PaddleOCR inference.yml PostProcess.character_dict.
#include "modules/charset.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using naina::internal::charset::Charset;
using naina::internal::charset::load_from_yaml;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static fs::path write_temp(const std::string& name, const std::string& body) {
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    return p;
}

static void test_parses_character_dict() {
    // Minimal stand-in for a real inference.yml.
    const std::string yml =
        "Global:\n"
        "  model_name: fake_rec\n"
        "PostProcess:\n"
        "  name: CTCLabelDecode\n"
        "  character_dict:\n"
        "  - a\n"
        "  - b\n"
        "  - c\n";
    const fs::path p = write_temp("naina_charset_ok.yml", yml);

    Charset cs;
    EXPECT(load_from_yaml(p, &cs));
    // 3 chars + blank + space = 5 classes.
    EXPECT(cs.num_classes() == 5);
    // Index 0 is the CTC blank and maps to no character.
    EXPECT(cs.at(0).empty());
    EXPECT(cs.at(1) == "a");
    EXPECT(cs.at(2) == "b");
    EXPECT(cs.at(3) == "c");
    // The final class is a space (PaddleOCR use_space_char).
    EXPECT(cs.at(4) == " ");
    // Out of range is empty, never a crash.
    EXPECT(cs.at(5).empty());
    EXPECT(cs.at(-1).empty());
    fs::remove(p);
}

static void test_preserves_multibyte_characters() {
    // yaml-cpp hands back UTF-8 bytes; a CJK glyph is 3 bytes and must
    // survive intact as one entry.
    const std::string yml =
        "PostProcess:\n"
        "  character_dict:\n"
        "  - \xe4\xb8\xad\n"   // 中
        "  - \xe6\x96\x87\n";  // 文
    const fs::path p = write_temp("naina_charset_utf8.yml", yml);

    Charset cs;
    EXPECT(load_from_yaml(p, &cs));
    EXPECT(cs.num_classes() == 4);
    EXPECT(cs.at(1) == "\xe4\xb8\xad");
    EXPECT(cs.at(1).size() == 3);
    EXPECT(cs.at(2) == "\xe6\x96\x87");
    fs::remove(p);
}

static void test_rejects_missing_or_malformed_files() {
    Charset cs;
    EXPECT(!load_from_yaml("/nonexistent/naina/definitely-not-here.yml", &cs));

    // Valid YAML with no PostProcess.character_dict is a hard failure —
    // silently returning an empty charset would decode every page to "".
    const fs::path p = write_temp("naina_charset_bad.yml", "Global:\n  model_name: x\n");
    EXPECT(!load_from_yaml(p, &cs));
    fs::remove(p);

    // An empty character_dict is equally useless.
    const fs::path e = write_temp("naina_charset_empty.yml",
                                  "PostProcess:\n  character_dict: []\n");
    EXPECT(!load_from_yaml(e, &cs));
    fs::remove(e);

    EXPECT(!load_from_yaml(p, nullptr));
}

static void test_real_registry_charset_if_cached() {
    // Opportunistic: if the tiny rec inference.yml is already in the model
    // cache, assert the real class count. Skipped when absent so the test
    // stays runnable offline with no weights.
    const char* cache = std::getenv("NAINA_CACHE");
    if (cache == nullptr) {
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(cache)) {
        if (entry.is_regular_file() && entry.path().filename() == "inference.yml") {
            Charset cs;
            if (load_from_yaml(entry.path(), &cs)) {
                // Only the two known PP-OCRv6 shapes are valid.
                EXPECT(cs.num_classes() == 6906 || cs.num_classes() == 18710);
            }
        }
    }
}

int main() {
    test_parses_character_dict();
    test_preserves_multibyte_characters();
    test_rejects_missing_or_malformed_files();
    test_real_registry_charset_if_cached();
    if (failures == 0) {
        std::printf("test_charset: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
