// naina_page storage: owns every string it hands out, one release call.
#include "page.hpp"

#include <cstdio>
#include <string>
#include <vector>

using naina::internal::Page;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static naina_textbox box_at(float x, float y, float w, float h, float score) {
    naina_textbox b{};
    b.corners[0] = {x, y};
    b.corners[1] = {x + w, y};
    b.corners[2] = {x + w, y + h};
    b.corners[3] = {x, y + h};
    b.score = score;
    return b;
}

static void test_empty_page_is_valid_not_null() {
    Page p;
    EXPECT(p.lines().empty());
    EXPECT(p.regions().empty());
    // An empty page still returns usable C strings, never nullptr — the ABI
    // promises naina_page_markdown never returns null.
    EXPECT(p.markdown() != nullptr);
    EXPECT(p.json() != nullptr);
    EXPECT(std::string(p.markdown()).empty());
}

static void test_text_pointers_stay_valid_after_many_appends() {
    Page p;
    // Append enough lines to force any internal vector to reallocate. If the
    // page stored std::string values in a vector and handed out .c_str(),
    // these pointers would dangle after growth. This is the bug the arena
    // design exists to prevent, so it is asserted rather than assumed.
    constexpr int kN = 500;
    for (int i = 0; i < kN; ++i) {
        p.add_line(box_at(static_cast<float>(i), 0.0F, 10.0F, 5.0F, 0.9F),
                   "line-" + std::to_string(i),
                   0.8F);
    }
    const auto lines = p.lines();
    EXPECT(lines.size() == static_cast<size_t>(kN));
    if (lines.size() != static_cast<size_t>(kN)) {
        return;
    }
    // Every pointer captured earlier must still read correctly.
    for (int i = 0; i < kN; ++i) {
        const std::string want = "line-" + std::to_string(i);
        EXPECT(lines[static_cast<size_t>(i)].text != nullptr);
        EXPECT(std::string(lines[static_cast<size_t>(i)].text) == want);
    }
}

static void test_line_fields_round_trip() {
    Page p;
    p.add_line(box_at(3.0F, 4.0F, 20.0F, 8.0F, 0.77F), "hello", 0.66F);
    const auto lines = p.lines();
    EXPECT(lines.size() == 1);
    if (lines.empty()) {
        return;
    }
    EXPECT(std::string(lines[0].text) == "hello");
    EXPECT(lines[0].confidence > 0.65F && lines[0].confidence < 0.67F);
    EXPECT(lines[0].box.score > 0.76F && lines[0].box.score < 0.78F);
    EXPECT(lines[0].box.corners[0].x > 2.9F && lines[0].box.corners[0].x < 3.1F);
    // region_id defaults to -1 (unassigned) until layout analysis runs.
    EXPECT(lines[0].region_id == -1);
}

static void test_empty_text_is_stored_as_empty_not_null() {
    Page p;
    p.add_line(box_at(0.0F, 0.0F, 5.0F, 5.0F, 0.5F), "", 0.0F);
    const auto lines = p.lines();
    EXPECT(lines.size() == 1);
    if (!lines.empty()) {
        EXPECT(lines[0].text != nullptr);
        EXPECT(std::string(lines[0].text).empty());
    }
}

static void test_multibyte_text_survives() {
    Page p;
    p.add_line(box_at(0.0F, 0.0F, 5.0F, 5.0F, 0.9F), "\xe4\xb8\xad\xe6\x96\x87", 0.9F);
    const auto lines = p.lines();
    EXPECT(lines.size() == 1);
    if (!lines.empty()) {
        EXPECT(std::string(lines[0].text) == "\xe4\xb8\xad\xe6\x96\x87");
        EXPECT(std::string(lines[0].text).size() == 6);
    }
}

static void test_markdown_is_lines_joined_by_newline() {
    Page p;
    p.add_line(box_at(0.0F, 0.0F, 10.0F, 5.0F, 0.9F), "first", 0.9F);
    p.add_line(box_at(0.0F, 10.0F, 10.0F, 5.0F, 0.9F), "second", 0.9F);
    // v0.2 has no layout model, so markdown is just the recognised lines in
    // detection order. Structure arrives with layout analysis in v0.3.
    EXPECT(std::string(p.markdown()) == "first\nsecond");
}

static void test_markdown_skips_empty_lines() {
    Page p;
    p.add_line(box_at(0.0F, 0.0F, 10.0F, 5.0F, 0.9F), "kept", 0.9F);
    p.add_line(box_at(0.0F, 10.0F, 10.0F, 5.0F, 0.9F), "", 0.0F);
    p.add_line(box_at(0.0F, 20.0F, 10.0F, 5.0F, 0.9F), "also", 0.9F);
    // A box that recognised to nothing should not leave a blank line in the
    // markdown — that would read as a paragraph break that isn't there.
    EXPECT(std::string(p.markdown()) == "kept\nalso");
}

static void test_json_is_wellformed_and_escapes() {
    Page p;
    p.add_line(box_at(1.0F, 2.0F, 10.0F, 5.0F, 0.9F), "say \"hi\"\\n", 0.5F);
    const std::string j = p.json();
    EXPECT(!j.empty());
    EXPECT(j.front() == '{');
    EXPECT(j.back() == '}');
    EXPECT(j.find("\"lines\"") != std::string::npos);
    // The quote and backslash must be escaped, or the JSON is invalid.
    EXPECT(j.find("say \\\"hi\\\"") != std::string::npos);
    EXPECT(j.find("\\\\n") != std::string::npos);
}

static void test_markdown_and_json_are_stable_across_calls() {
    Page p;
    p.add_line(box_at(0.0F, 0.0F, 10.0F, 5.0F, 0.9F), "x", 0.9F);
    const char* m1 = p.markdown();
    const char* m2 = p.markdown();
    // Cached, so the pointer is stable and callers may hold it.
    EXPECT(m1 == m2);
    const char* j1 = p.json();
    EXPECT(j1 == p.json());
}

int main() {
    test_empty_page_is_valid_not_null();
    test_text_pointers_stay_valid_after_many_appends();
    test_line_fields_round_trip();
    test_empty_text_is_stored_as_empty_not_null();
    test_multibyte_text_survives();
    test_markdown_is_lines_joined_by_newline();
    test_markdown_skips_empty_lines();
    test_json_is_wellformed_and_escapes();
    test_markdown_and_json_are_stable_across_calls();
    if (failures == 0) {
        std::printf("test_page: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
