// doc_assemble: line-to-region assignment, reading order, markdown emission.
#include "modules/doc_assemble.hpp"

#include <cstdio>
#include <string>
#include <vector>

using naina::internal::doc_assemble::assign_lines_to_regions;
using naina::internal::doc_assemble::Config;
using naina::internal::doc_assemble::Line;
using naina::internal::doc_assemble::order_regions;
using naina::internal::doc_assemble::to_markdown;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static naina_region region(float x, float y, float w, float h, naina_region_kind kind) {
    naina_region r{};
    r.bbox.x = x;
    r.bbox.y = y;
    r.bbox.w = w;
    r.bbox.h = h;
    r.bbox.score = 0.9F;
    r.kind = kind;
    r.order = -1;
    return r;
}

static Line line(float x, float y, float w, float h, const std::string& text) {
    Line l{};
    l.box.corners[0] = {x, y};
    l.box.corners[1] = {x + w, y};
    l.box.corners[2] = {x + w, y + h};
    l.box.corners[3] = {x, y + h};
    l.box.score = 0.9F;
    l.text = text;
    l.confidence = 0.9F;
    l.region_id = -1;
    return l;
}

// ── assignment ──────────────────────────────────────────────────────

static void test_line_assigned_to_containing_region() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 50, NAINA_REGION_TEXT),
        region(0, 100, 100, 50, NAINA_REGION_TEXT),
    };
    std::vector<Line> lines = {
        line(10, 10, 50, 12, "in the first"),
        line(10, 110, 50, 12, "in the second"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == 0);
    EXPECT(lines[1].region_id == 1);
}

static void test_line_poking_outside_still_assigned() {
    // Lines routinely extend a pixel or two past their region. Requiring full
    // containment would orphan most real lines.
    std::vector<naina_region> regions = {region(10, 10, 80, 40, NAINA_REGION_TEXT)};
    std::vector<Line> lines = {line(8, 12, 80, 12, "slightly left of the region")};
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == 0);
}

static void test_line_matching_no_region_is_kept_unassigned() {
    // Layout misses things. An unmatched line must survive with region_id -1
    // rather than being dropped, or text vanishes silently.
    std::vector<naina_region> regions = {region(0, 0, 50, 50, NAINA_REGION_TEXT)};
    std::vector<Line> lines = {line(500, 500, 40, 12, "far away")};
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == -1);
}

static void test_region_smaller_than_line_still_captures_it() {
    // A page-number region is barely wider than its digit, so the text quad —
    // which DBNet unclips outward — ends up larger than the region. Measured on
    // a real page: region 6x14 at (612,1704) against a 20x22 quad. Judged by
    // line area alone that overlap is well under min_overlap, so the line
    // orphaned and the page number leaked into the markdown tail, defeating
    // furniture omission.
    std::vector<naina_region> regions = {region(612, 1704, 6, 14, NAINA_REGION_PAGENUM)};
    std::vector<Line> lines = {line(605, 1700, 20, 22, "1")};
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == 0);
}

static void test_small_region_does_not_steal_a_well_matched_line() {
    // The containment fallback must not outrank a genuine match. A sliver
    // region sitting inside a long line is fully covered by it, so scored by
    // region area it reaches 1.0 — the line's true home must still win.
    std::vector<naina_region> regions = {
        region(0, 0, 200, 20, NAINA_REGION_TEXT),    // the true home
        region(190, 5, 4, 4, NAINA_REGION_PAGENUM),  // sliver inside the line
    };
    std::vector<Line> lines = {line(0, 0, 200, 20, "a long line of body text")};
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == 0);
}

static void test_line_picks_best_overlap_when_regions_overlap() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 40, NAINA_REGION_TEXT),   // covers a little
        region(0, 20, 100, 100, NAINA_REGION_TEXT), // covers most of the line
    };
    std::vector<Line> lines = {line(10, 30, 50, 20, "mostly in the second")};
    assign_lines_to_regions(regions, {}, &lines);
    EXPECT(lines[0].region_id == 1);
}

// ── reading order ───────────────────────────────────────────────────

static void test_single_column_orders_top_to_bottom() {
    std::vector<naina_region> regions = {
        region(0, 200, 100, 50, NAINA_REGION_TEXT),
        region(0, 0, 100, 50, NAINA_REGION_TITLE),
        region(0, 100, 100, 50, NAINA_REGION_TEXT),
    };
    order_regions({}, &regions);
    EXPECT(regions[1].order == 0);  // y=0
    EXPECT(regions[2].order == 1);  // y=100
    EXPECT(regions[0].order == 2);  // y=200
}

static void test_two_columns_order_left_then_right() {
    // The whole point of column handling: on a two-column page, the left
    // column must be read before the right, not interleaved by y.
    std::vector<naina_region> regions = {
        region(300, 100, 200, 300, NAINA_REGION_TEXT),  // right column
        region(0, 100, 200, 300, NAINA_REGION_TEXT),    // left column
    };
    order_regions({}, &regions);
    EXPECT(regions[1].order == 0);  // left first
    EXPECT(regions[0].order == 1);
}

static void test_furniture_is_ordered_last() {
    std::vector<naina_region> regions = {
        region(0, 0, 500, 20, NAINA_REGION_HEADER),
        region(0, 100, 500, 200, NAINA_REGION_TEXT),
        region(0, 700, 500, 20, NAINA_REGION_FOOTER),
        region(240, 730, 20, 15, NAINA_REGION_PAGENUM),
    };
    order_regions({}, &regions);
    // Body text reads first even though the header is physically above it.
    EXPECT(regions[1].order == 0);
    EXPECT(regions[0].order > 0);
    EXPECT(regions[2].order > 0);
    EXPECT(regions[3].order > 0);
}

static void test_order_is_a_permutation() {
    std::vector<naina_region> regions = {
        region(0, 50, 100, 40, NAINA_REGION_TEXT),
        region(200, 50, 100, 40, NAINA_REGION_TEXT),
        region(0, 200, 300, 40, NAINA_REGION_TEXT),
        region(0, 0, 300, 20, NAINA_REGION_HEADER),
    };
    order_regions({}, &regions);
    std::vector<int32_t> seen;
    for (const auto& r : regions) {
        seen.push_back(r.order);
    }
    std::sort(seen.begin(), seen.end());
    for (size_t i = 0; i < seen.size(); ++i) {
        EXPECT(seen[i] == static_cast<int32_t>(i));
    }
}

// ── markdown ────────────────────────────────────────────────────────

static void test_markdown_titles_and_paragraphs() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 30, NAINA_REGION_TITLE),
        region(0, 40, 100, 60, NAINA_REGION_TEXT),
    };
    std::vector<Line> lines = {
        line(0, 5, 90, 12, "Quarterly Report"),
        line(0, 45, 90, 12, "Revenue grew"),
        line(0, 60, 90, 12, "by twelve percent."),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    // First title is h1; body lines join into one paragraph with a space.
    EXPECT(md.find("# Quarterly Report") != std::string::npos);
    EXPECT(md.find("Revenue grew by twelve percent.") != std::string::npos);
}

static void test_markdown_second_title_is_h2() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 30, NAINA_REGION_TITLE),
        region(0, 50, 100, 30, NAINA_REGION_TITLE),
    };
    std::vector<Line> lines = {
        line(0, 5, 90, 12, "Doc Title"),
        line(0, 55, 90, 12, "Section One"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("# Doc Title") != std::string::npos);
    EXPECT(md.find("## Section One") != std::string::npos);
}

static void test_markdown_omits_furniture() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 20, NAINA_REGION_HEADER),
        region(0, 40, 100, 40, NAINA_REGION_TEXT),
        region(0, 200, 100, 20, NAINA_REGION_PAGENUM),
    };
    std::vector<Line> lines = {
        line(0, 2, 90, 12, "CONFIDENTIAL DRAFT"),
        line(0, 45, 90, 12, "Real content."),
        line(0, 202, 20, 12, "7"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("Real content.") != std::string::npos);
    // Running headers and page numbers are furniture, not document text.
    EXPECT(md.find("CONFIDENTIAL DRAFT") == std::string::npos);
    EXPECT(md.find("\n7") == std::string::npos);
}

static void test_markdown_marks_caption_and_formula() {
    std::vector<naina_region> regions = {
        region(0, 0, 100, 20, NAINA_REGION_CAPTION),
        region(0, 40, 100, 20, NAINA_REGION_FORMULA),
    };
    std::vector<Line> lines = {
        line(0, 2, 90, 12, "Figure 1: results"),
        line(0, 42, 90, 12, "E = mc^2"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("*Figure 1: results*") != std::string::npos);
    EXPECT(md.find("$$") != std::string::npos);
    EXPECT(md.find("E = mc^2") != std::string::npos);
}

static void test_markdown_table_is_marked_unparsed_not_faked() {
    // naina detects tables but does not parse their structure. Emitting a fake
    // markdown grid would be a lie; a fenced block that says so is honest.
    std::vector<naina_region> regions = {region(0, 0, 200, 80, NAINA_REGION_TABLE)};
    std::vector<Line> lines = {
        line(0, 5, 90, 12, "Item Qty"),
        line(0, 25, 90, 12, "Widget 2"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("```") != std::string::npos);
    EXPECT(md.find("Item Qty") != std::string::npos);
    // Must NOT pretend to be a parsed markdown table.
    EXPECT(md.find("|---") == std::string::npos);
}

static void test_unassigned_lines_are_appended_not_lost() {
    std::vector<naina_region> regions = {region(0, 0, 100, 30, NAINA_REGION_TEXT)};
    std::vector<Line> lines = {
        line(0, 5, 90, 12, "inside a region"),
        line(0, 900, 90, 12, "layout missed me"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("inside a region") != std::string::npos);
    EXPECT(md.find("layout missed me") != std::string::npos);
}

static void test_empty_input_is_empty_output() {
    std::vector<naina_region> regions;
    std::vector<Line> lines;
    assign_lines_to_regions(regions, {}, &lines);
    order_regions({}, &regions);
    EXPECT(to_markdown(regions, lines).empty());
}

static void test_no_regions_falls_back_to_line_order() {
    // With no layout model available, markdown must still be the recognised
    // lines rather than nothing.
    std::vector<naina_region> regions;
    std::vector<Line> lines = {
        line(0, 0, 90, 12, "first"),
        line(0, 20, 90, 12, "second"),
    };
    assign_lines_to_regions(regions, {}, &lines);
    const std::string md = to_markdown(regions, lines);
    EXPECT(md.find("first") != std::string::npos);
    EXPECT(md.find("second") != std::string::npos);
}

int main() {
    test_line_assigned_to_containing_region();
    test_line_poking_outside_still_assigned();
    test_line_matching_no_region_is_kept_unassigned();
    test_region_smaller_than_line_still_captures_it();
    test_small_region_does_not_steal_a_well_matched_line();
    test_line_picks_best_overlap_when_regions_overlap();
    test_single_column_orders_top_to_bottom();
    test_two_columns_order_left_then_right();
    test_furniture_is_ordered_last();
    test_order_is_a_permutation();
    test_markdown_titles_and_paragraphs();
    test_markdown_second_title_is_h2();
    test_markdown_omits_furniture();
    test_markdown_marks_caption_and_formula();
    test_markdown_table_is_marked_unparsed_not_faked();
    test_unassigned_lines_are_appended_not_lost();
    test_empty_input_is_empty_output();
    test_no_regions_falls_back_to_line_order();
    if (failures == 0) {
        std::printf("test_doc_assemble: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
