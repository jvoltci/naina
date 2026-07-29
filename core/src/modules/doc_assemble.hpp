// doc_assemble — lines + regions -> reading order + structured markdown.
//
// Pure logic: no model, no session, no randomness. That is deliberate. It makes
// the whole structure layer testable from hand-built inputs, and it makes the
// cross-binding identity guarantee provable with plain equality rather than
// float-tolerance comparisons.
#ifndef NAINA_INTERNAL_DOC_ASSEMBLE_HPP
#define NAINA_INTERNAL_DOC_ASSEMBLE_HPP

#include "naina/naina.h"

#include <string>
#include <vector>

namespace naina::internal::doc_assemble {

// One recognised line, as produced by text_recognize.
struct Line {
    naina_textbox box;
    std::string text;
    float confidence = 0.0F;
    int32_t region_id = -1;  // filled in by assign_lines_to_regions
};

struct Config {
    // A line is considered inside a region when at least this fraction of the
    // line's area overlaps it. Lines routinely poke a pixel or two outside
    // their region, so requiring full containment would orphan most of them.
    float min_overlap = 0.5F;

    // Two regions whose vertical spans overlap by at least this fraction of
    // the shorter one are treated as side by side, and ordered left-to-right
    // instead of top-to-bottom. This is what makes two-column pages read
    // correctly.
    float column_overlap = 0.5F;
};

// Assign each line to the region it best overlaps, writing region_id.
// Lines matching no region keep region_id == -1 and are still emitted, at the
// end — dropping them would silently lose text that layout simply missed.
void assign_lines_to_regions(const std::vector<naina_region>& regions,
                             const Config& cfg,
                             std::vector<Line>* lines);

// Compute reading order over regions, writing `order` on each.
//
// Groups regions into bands of vertically-overlapping peers, orders bands top
// to bottom, and orders within a band left to right. Page furniture (header,
// footer, page number) is pushed to the end regardless of position, since it
// is not part of the document's reading flow.
void order_regions(const Config& cfg, std::vector<naina_region>* regions);

// Emit markdown. Regions must already be ordered and lines assigned.
//
//   TITLE    -> "# "        (first) / "## " (subsequent)
//   CAPTION  -> "*...*"
//   TABLE    -> fenced block, marked as unparsed
//   FIGURE   -> "![figure]()" placeholder
//   FORMULA  -> "$$ ... $$"
//   TEXT     -> paragraph, lines joined with a space
//   HEADER / FOOTER / PAGENUM -> omitted; they are furniture, not content
std::string to_markdown(const std::vector<naina_region>& regions, const std::vector<Line>& lines);

}  // namespace naina::internal::doc_assemble

#endif  // NAINA_INTERNAL_DOC_ASSEMBLE_HPP
