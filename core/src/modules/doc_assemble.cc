#include "doc_assemble.hpp"

#include <algorithm>
#include <cstddef>

namespace naina::internal::doc_assemble {

namespace {

struct Rect {
    float x1, y1, x2, y2;
};

Rect rect_of(const naina_bbox& b) {
    return Rect{b.x, b.y, b.x + b.w, b.y + b.h};
}

// Axis-aligned bounds of a (possibly rotated) text quad.
Rect rect_of(const naina_textbox& t) {
    Rect r{t.corners[0].x, t.corners[0].y, t.corners[0].x, t.corners[0].y};
    for (int i = 1; i < 4; ++i) {
        r.x1 = std::min(r.x1, t.corners[i].x);
        r.y1 = std::min(r.y1, t.corners[i].y);
        r.x2 = std::max(r.x2, t.corners[i].x);
        r.y2 = std::max(r.y2, t.corners[i].y);
    }
    return r;
}

float area(const Rect& r) {
    return std::max(0.0F, r.x2 - r.x1) * std::max(0.0F, r.y2 - r.y1);
}

float intersect_area(const Rect& a, const Rect& b) {
    const float x1 = std::max(a.x1, b.x1);
    const float y1 = std::max(a.y1, b.y1);
    const float x2 = std::min(a.x2, b.x2);
    const float y2 = std::min(a.y2, b.y2);
    return std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
}

// Fraction of the shorter vertical span that the two regions share. Used to
// decide "side by side" versus "stacked".
float vertical_overlap_ratio(const Rect& a, const Rect& b) {
    const float top = std::max(a.y1, b.y1);
    const float bot = std::min(a.y2, b.y2);
    const float shared = std::max(0.0F, bot - top);
    const float shorter = std::min(a.y2 - a.y1, b.y2 - b.y1);
    return shorter > 0.0F ? shared / shorter : 0.0F;
}

// Header, footer and page numbers are page furniture, not part of the reading
// flow. They are ordered last and omitted from markdown.
bool is_furniture(naina_region_kind k) {
    return k == NAINA_REGION_HEADER || k == NAINA_REGION_FOOTER || k == NAINA_REGION_PAGENUM;
}

// Lines belonging to `region_id`, in reading order within the region: top to
// bottom, then left to right for lines sharing a row.
std::vector<const Line*> lines_of(const std::vector<Line>& lines, int32_t region_id) {
    std::vector<const Line*> out;
    for (const auto& l : lines) {
        if (l.region_id == region_id) {
            out.push_back(&l);
        }
    }
    std::sort(out.begin(), out.end(), [](const Line* a, const Line* b) {
        const Rect ra = rect_of(a->box);
        const Rect rb = rect_of(b->box);
        // Treat lines whose vertical centres are close as the same row.
        const float ca = (ra.y1 + ra.y2) * 0.5F;
        const float cb = (rb.y1 + rb.y2) * 0.5F;
        const float tol = std::max(2.0F, std::min(ra.y2 - ra.y1, rb.y2 - rb.y1) * 0.5F);
        if (std::abs(ca - cb) > tol) {
            return ca < cb;
        }
        return ra.x1 < rb.x1;
    });
    return out;
}

std::string join_with_space(const std::vector<const Line*>& lines) {
    std::string out;
    for (const Line* l : lines) {
        if (l->text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ' ';
        }
        out += l->text;
    }
    return out;
}

std::string join_with_newline(const std::vector<const Line*>& lines) {
    std::string out;
    for (const Line* l : lines) {
        if (l->text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += '\n';
        }
        out += l->text;
    }
    return out;
}

void append_block(std::string* md, const std::string& block) {
    if (block.empty()) {
        return;
    }
    if (!md->empty()) {
        *md += "\n\n";
    }
    *md += block;
}

}  // namespace

void assign_lines_to_regions(const std::vector<naina_region>& regions,
                             const Config& cfg,
                             std::vector<Line>* lines) {
    if (lines == nullptr) {
        return;
    }
    for (auto& line : *lines) {
        line.region_id = -1;
        const Rect lr = rect_of(line.box);
        const float la = area(lr);
        if (la <= 0.0F) {
            continue;
        }
        float best_frac = cfg.min_overlap;
        int32_t best = -1;
        for (size_t i = 0; i < regions.size(); ++i) {
            const float frac = intersect_area(lr, rect_of(regions[i].bbox)) / la;
            // Strictly greater, so the first region wins an exact tie and the
            // result does not depend on iteration order beyond that.
            if (frac > best_frac) {
                best_frac = frac;
                best = static_cast<int32_t>(i);
            }
        }

        // Fallback, applied only when nothing matched by line area. A region
        // smaller than the text quad can never reach min_overlap that way — a
        // page number's box is a few pixels wider than its digit, while DBNet
        // unclips the quad outward. Scoring by region area instead lets such a
        // region claim its line, so page furniture stays furniture rather than
        // orphaning into the unstructured tail.
        //
        // Deliberately a fallback and not a change to the primary metric: a
        // sliver region inside a long line scores 1.0 by region area, and must
        // not outrank the line's true home.
        if (best < 0) {
            float best_covered = cfg.min_overlap;
            for (size_t i = 0; i < regions.size(); ++i) {
                const Rect rr = rect_of(regions[i].bbox);
                const float ra = area(rr);
                if (ra <= 0.0F) {
                    continue;
                }
                const float covered = intersect_area(lr, rr) / ra;
                if (covered > best_covered) {
                    best_covered = covered;
                    best = static_cast<int32_t>(i);
                }
            }
        }
        line.region_id = best;
    }
}

void order_regions(const Config& cfg, std::vector<naina_region>* regions) {
    if (regions == nullptr || regions->empty()) {
        return;
    }
    const size_t n = regions->size();

    std::vector<size_t> body;
    std::vector<size_t> furniture;
    for (size_t i = 0; i < n; ++i) {
        (is_furniture((*regions)[i].kind) ? furniture : body).push_back(i);
    }

    // Sort body regions top to bottom as the basis for banding.
    std::sort(body.begin(), body.end(), [&](size_t a, size_t b) {
        const Rect ra = rect_of((*regions)[a].bbox);
        const Rect rb = rect_of((*regions)[b].bbox);
        if (ra.y1 != rb.y1) {
            return ra.y1 < rb.y1;
        }
        return ra.x1 < rb.x1;
    });

    // Greedily group into bands of vertically-overlapping peers. Regions in a
    // band are side by side (columns) and read left to right; bands themselves
    // read top to bottom. This is what makes a two-column page come out in the
    // right order instead of interleaved by y.
    std::vector<std::vector<size_t>> bands;
    for (const size_t idx : body) {
        const Rect r = rect_of((*regions)[idx].bbox);
        bool placed = false;
        if (!bands.empty()) {
            for (const size_t member : bands.back()) {
                if (vertical_overlap_ratio(r, rect_of((*regions)[member].bbox)) >=
                    cfg.column_overlap) {
                    bands.back().push_back(idx);
                    placed = true;
                    break;
                }
            }
        }
        if (!placed) {
            bands.push_back({idx});
        }
    }

    int32_t order = 0;
    for (auto& band : bands) {
        std::sort(band.begin(), band.end(), [&](size_t a, size_t b) {
            const Rect ra = rect_of((*regions)[a].bbox);
            const Rect rb = rect_of((*regions)[b].bbox);
            if (ra.x1 != rb.x1) {
                return ra.x1 < rb.x1;
            }
            return ra.y1 < rb.y1;
        });
        for (const size_t idx : band) {
            (*regions)[idx].order = order++;
        }
    }

    // Furniture last, itself top to bottom so the ordering is deterministic.
    std::sort(furniture.begin(), furniture.end(), [&](size_t a, size_t b) {
        const Rect ra = rect_of((*regions)[a].bbox);
        const Rect rb = rect_of((*regions)[b].bbox);
        if (ra.y1 != rb.y1) {
            return ra.y1 < rb.y1;
        }
        return ra.x1 < rb.x1;
    });
    for (const size_t idx : furniture) {
        (*regions)[idx].order = order++;
    }
}

std::string to_markdown(const std::vector<naina_region>& regions, const std::vector<Line>& lines) {
    std::string md;

    // No layout model available: fall back to the recognised lines in order.
    // Emitting nothing would make a missing layout model look like a blank page.
    if (regions.empty()) {
        std::vector<const Line*> all;
        all.reserve(lines.size());
        for (const auto& l : lines) {
            all.push_back(&l);
        }
        return join_with_newline(all);
    }

    // Visit regions in reading order.
    std::vector<size_t> by_order(regions.size());
    for (size_t i = 0; i < regions.size(); ++i) {
        by_order[i] = i;
    }
    std::sort(by_order.begin(), by_order.end(), [&](size_t a, size_t b) {
        return regions[a].order < regions[b].order;
    });

    bool seen_title = false;
    for (const size_t idx : by_order) {
        const naina_region& r = regions[idx];
        if (is_furniture(r.kind)) {
            continue;  // furniture is not document content
        }
        const auto owned = lines_of(lines, static_cast<int32_t>(idx));
        if (owned.empty()) {
            continue;
        }

        switch (r.kind) {
            case NAINA_REGION_TITLE: {
                // The first title on a page is the document heading; later ones
                // are sections.
                const std::string text = join_with_space(owned);
                append_block(&md, (seen_title ? "## " : "# ") + text);
                seen_title = true;
                break;
            }
            case NAINA_REGION_CAPTION:
                append_block(&md, "*" + join_with_space(owned) + "*");
                break;

            case NAINA_REGION_FORMULA:
                append_block(&md, "$$\n" + join_with_newline(owned) + "\n$$");
                break;

            case NAINA_REGION_TABLE:
                // naina detects tables but does not parse their structure.
                // Emitting a markdown grid would fabricate column boundaries
                // that were never determined, so the raw cell text is fenced
                // and labelled instead.
                append_block(&md,
                             "```text\n[table: structure not parsed]\n" + join_with_newline(owned) +
                                 "\n```");
                break;

            case NAINA_REGION_FIGURE:
                append_block(&md, "![figure]()\n\n*" + join_with_space(owned) + "*");
                break;

            case NAINA_REGION_LIST: {
                std::string block;
                for (const Line* l : owned) {
                    if (l->text.empty()) {
                        continue;
                    }
                    if (!block.empty()) {
                        block += '\n';
                    }
                    block += "- " + l->text;
                }
                append_block(&md, block);
                break;
            }

            case NAINA_REGION_TEXT:
            case NAINA_REGION_UNKNOWN:
            default:
                append_block(&md, join_with_space(owned));
                break;
        }
    }

    // Lines layout never matched are appended rather than dropped: losing real
    // text to a layout miss is worse than an unstructured tail.
    std::vector<const Line*> orphans;
    for (const auto& l : lines) {
        if (l.region_id < 0) {
            orphans.push_back(&l);
        }
    }
    if (!orphans.empty()) {
        std::sort(orphans.begin(), orphans.end(), [](const Line* a, const Line* b) {
            const Rect ra = rect_of(a->box);
            const Rect rb = rect_of(b->box);
            if (ra.y1 != rb.y1) {
                return ra.y1 < rb.y1;
            }
            return ra.x1 < rb.x1;
        });
        append_block(&md, join_with_newline(orphans));
    }

    return md;
}

}  // namespace naina::internal::doc_assemble
