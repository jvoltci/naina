// Page: the storage behind naina_page_t.
//
// A page owns every string it hands out through the C ABI, so a binding needs
// exactly one release call. Line text lives in a deque of stable std::string
// nodes rather than a vector, because the ABI hands out raw `const char*` into
// that storage and a vector would dangle every pointer on reallocation.
//
// Markdown and JSON are built lazily and cached, so naina_page_markdown can
// return a stable borrowed pointer.
#ifndef NAINA_INTERNAL_PAGE_HPP
#define NAINA_INTERNAL_PAGE_HPP

#include "naina/naina.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace naina::internal {

class Page {
public:
    // Append a recognised line. `text` is copied into storage owned by the
    // page; the returned naina_textline::text points into that copy.
    void add_line(const naina_textbox& box, const std::string& text, float confidence);

    // Append a layout region. v0.2 never calls this — layout analysis is v0.3 —
    // but the accessor exists so the ABI shape is stable.
    void add_region(const naina_bbox& bbox, naina_region_kind kind, int32_t order);

    // Borrowed views. Valid for the lifetime of the Page.
    const std::vector<naina_textline>& lines() const { return lines_; }
    const std::vector<naina_region>& regions() const { return regions_; }

    // Serialised views. Built on first call, then cached. Never null.
    const char* markdown() const;
    const char* json() const;

private:
    // Stable string storage. Never reallocates existing nodes, unlike vector.
    std::deque<std::string> texts_;
    std::vector<naina_textline> lines_;
    std::vector<naina_region> regions_;

    mutable std::optional<std::string> markdown_;
    mutable std::optional<std::string> json_;
};

}  // namespace naina::internal

#endif  // NAINA_INTERNAL_PAGE_HPP
