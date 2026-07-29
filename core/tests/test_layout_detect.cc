// Layout post-processing: class mapping and cross-class duplicate suppression.
//
// Pure logic — no model, no session. The class-id table and the dedupe threshold
// are both places where a wrong value produces plausible-looking output rather
// than an error, so both are pinned here.

#include "modules/layout_detect.hpp"

#include <cstdio>
#include <vector>

using naina::internal::layout_detect::dedupe_overlapping;
using naina::internal::layout_detect::kind_from_class_id;

static int failures = 0;
#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static naina_region reg(float x, float y, float w, float h, float score, naina_region_kind k) {
    naina_region r{};
    r.bbox.x = x;
    r.bbox.y = y;
    r.bbox.w = w;
    r.bbox.h = h;
    r.bbox.score = score;
    r.kind = k;
    r.order = -1;
    return r;
}

// ── class mapping ───────────────────────────────────────────────────────

static void test_class_ids_map_to_expected_kinds() {
    // Spot-checks against PP-DocLayout's own 23-entry label_list, read from the
    // model config. A shifted table mislabels every region on every page.
    EXPECT(kind_from_class_id(11) == NAINA_REGION_TITLE);    // doc_title
    EXPECT(kind_from_class_id(0) == NAINA_REGION_TITLE);     // paragraph_title
    EXPECT(kind_from_class_id(2) == NAINA_REGION_TEXT);      // text
    EXPECT(kind_from_class_id(8) == NAINA_REGION_TABLE);     // table
    EXPECT(kind_from_class_id(1) == NAINA_REGION_FIGURE);    // image
    EXPECT(kind_from_class_id(6) == NAINA_REGION_CAPTION);   // figure_title
    EXPECT(kind_from_class_id(7) == NAINA_REGION_FORMULA);   // formula
    EXPECT(kind_from_class_id(13) == NAINA_REGION_HEADER);   // header
    EXPECT(kind_from_class_id(15) == NAINA_REGION_FOOTER);   // footer
    EXPECT(kind_from_class_id(3) == NAINA_REGION_PAGENUM);   // number
}

static void test_out_of_range_class_is_unknown() {
    EXPECT(kind_from_class_id(-1) == NAINA_REGION_UNKNOWN);
    EXPECT(kind_from_class_id(23) == NAINA_REGION_UNKNOWN);
    EXPECT(kind_from_class_id(9999) == NAINA_REGION_UNKNOWN);
}

// ── cross-class dedupe ──────────────────────────────────────────────────

static void test_identical_box_under_two_labels_keeps_highest_score() {
    // The real case. Measured on an 800x620 page: the running head came back as
    // `text` at 0.677 AND `header` at 0.481 with byte-identical coordinates,
    // because PaddleDetection's NMS runs per class. Emitting both gives
    // doc_assemble two regions for one box, and whichever wins the line
    // assignment decides whether that text appears in the markdown at all.
    std::vector<naina_region> regions = {
        reg(59, 24, 136, 12, 0.677F, NAINA_REGION_TEXT),
        reg(59, 24, 136, 12, 0.481F, NAINA_REGION_HEADER),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 1);
    EXPECT(regions[0].kind == NAINA_REGION_TEXT);
    EXPECT(regions[0].bbox.score == 0.677F);
}

static void test_lower_scoring_duplicate_wins_when_listed_first() {
    // Order in the input must not decide the outcome; score must.
    std::vector<naina_region> regions = {
        reg(10, 10, 100, 20, 0.40F, NAINA_REGION_HEADER),
        reg(10, 10, 100, 20, 0.90F, NAINA_REGION_TEXT),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 1);
    EXPECT(regions[0].kind == NAINA_REGION_TEXT);
}

static void test_caption_touching_its_figure_is_kept() {
    // The failure mode a loose threshold would create. A caption sits directly
    // against its figure and the boxes share an edge; they are different
    // regions and both belong in the output. Their IoU is 0, but a naive
    // "any overlap" rule or a low threshold applied to nested boxes would be
    // tempting — this pins that both survive.
    std::vector<naina_region> regions = {
        reg(100, 100, 400, 300, 0.90F, NAINA_REGION_FIGURE),
        reg(100, 400, 400, 40, 0.80F, NAINA_REGION_CAPTION),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 2);
}

static void test_region_nested_inside_a_larger_one_is_kept() {
    // A title inside a text block, or a formula inside a paragraph. The small
    // box is fully contained, so intersection / smaller_area is 1.0 — but IoU is
    // only 0.04, which is why IoU is the right metric here and containment is
    // not.
    std::vector<naina_region> regions = {
        reg(0, 0, 500, 500, 0.90F, NAINA_REGION_TEXT),
        reg(10, 10, 100, 100, 0.85F, NAINA_REGION_TITLE),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 2);
}

static void test_near_identical_boxes_are_deduped() {
    // Duplicates are not always bit-identical: the same box can come back a
    // pixel or two different between classes. IoU here is about 0.96.
    std::vector<naina_region> regions = {
        reg(100, 100, 200, 50, 0.80F, NAINA_REGION_TEXT),
        reg(101, 101, 199, 49, 0.70F, NAINA_REGION_TITLE),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 1);
    EXPECT(regions[0].kind == NAINA_REGION_TEXT);
}

static void test_survivors_keep_input_order() {
    // Reading order is computed later from geometry, but dedupe must not shuffle
    // the list underneath it — a stable result makes output reproducible.
    std::vector<naina_region> regions = {
        reg(0, 0, 100, 20, 0.60F, NAINA_REGION_TITLE),
        reg(0, 100, 100, 20, 0.95F, NAINA_REGION_TEXT),
        reg(0, 200, 100, 20, 0.75F, NAINA_REGION_TEXT),
    };
    dedupe_overlapping(0.85F, &regions);
    EXPECT(regions.size() == 3);
    EXPECT(regions[0].bbox.y == 0.0F);
    EXPECT(regions[1].bbox.y == 100.0F);
    EXPECT(regions[2].bbox.y == 200.0F);
}

static void test_empty_and_single_are_untouched() {
    std::vector<naina_region> none;
    dedupe_overlapping(0.85F, &none);
    EXPECT(none.empty());

    std::vector<naina_region> one = {reg(0, 0, 10, 10, 0.9F, NAINA_REGION_TEXT)};
    dedupe_overlapping(0.85F, &one);
    EXPECT(one.size() == 1);
}

static void test_null_is_safe() {
    dedupe_overlapping(0.85F, nullptr);
    EXPECT(true);  // reaching here without a crash is the assertion
}

int main() {
    test_class_ids_map_to_expected_kinds();
    test_out_of_range_class_is_unknown();
    test_identical_box_under_two_labels_keeps_highest_score();
    test_lower_scoring_duplicate_wins_when_listed_first();
    test_caption_touching_its_figure_is_kept();
    test_region_nested_inside_a_larger_one_is_kept();
    test_near_identical_boxes_are_deduped();
    test_survivors_keep_input_order();
    test_empty_and_single_are_untouched();
    test_null_is_safe();

    if (failures == 0) {
        std::printf("test_layout_detect: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
