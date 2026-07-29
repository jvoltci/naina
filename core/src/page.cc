#include "page.hpp"

#include <cstdio>

namespace naina::internal {

namespace {

// Escape a UTF-8 string for a JSON string literal. Multibyte sequences pass
// through untouched — JSON permits raw UTF-8 — so only the structural
// characters and C0 controls need handling.
void append_json_escaped(const std::string& in, std::string* out) {
    for (const char c : in) {
        switch (c) {
            case '"':
                *out += "\\\"";
                break;
            case '\\':
                *out += "\\\\";
                break;
            case '\n':
                *out += "\\n";
                break;
            case '\r':
                *out += "\\r";
                break;
            case '\t':
                *out += "\\t";
                break;
            default: {
                const auto u = static_cast<unsigned char>(c);
                if (u < 0x20U) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(u));
                    *out += buf;
                } else {
                    *out += c;
                }
                break;
            }
        }
    }
}

void append_float(float v, std::string* out) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    *out += buf;
}

}  // namespace

void Page::add_line(const naina_textbox& box, const std::string& text, float confidence) {
    // Store the text first so the pointer we hand out is stable for the
    // lifetime of the page. deque never moves existing nodes on growth.
    texts_.push_back(text);

    naina_textline line{};
    line.box = box;
    line.text = texts_.back().c_str();
    line.confidence = confidence;
    line.region_id = -1;  // unassigned until layout analysis runs (v0.3)
    lines_.push_back(line);

    // Any append invalidates the cached serialisations.
    markdown_.reset();
    json_.reset();
}

void Page::add_region(const naina_bbox& bbox, naina_region_kind kind, int32_t order) {
    naina_region r{};
    r.bbox = bbox;
    r.kind = kind;
    r.order = order;
    regions_.push_back(r);
    markdown_.reset();
    json_.reset();
}

void Page::set_markdown(std::string md) {
    markdown_ = std::move(md);
}

void Page::set_line_region(size_t line_index, int32_t region_id) {
    if (line_index < lines_.size()) {
        lines_[line_index].region_id = region_id;
        json_.reset();
    }
}

const char* Page::markdown() const {
    if (!markdown_.has_value()) {
        std::string out;
        bool first = true;
        for (const auto& line : lines_) {
            if (line.text == nullptr || line.text[0] == '\0') {
                // A box that recognised to nothing must not leave a blank
                // line — that would read as a paragraph break that isn't there.
                continue;
            }
            if (!first) {
                out += '\n';
            }
            out += line.text;
            first = false;
        }
        markdown_ = std::move(out);
    }
    return markdown_->c_str();
}

const char* Page::json() const {
    if (!json_.has_value()) {
        std::string out = "{\"lines\":[";
        for (size_t i = 0; i < lines_.size(); ++i) {
            const naina_textline& l = lines_[i];
            if (i != 0) {
                out += ',';
            }
            out += "{\"text\":\"";
            append_json_escaped(l.text != nullptr ? std::string(l.text) : std::string(), &out);
            out += "\",\"confidence\":";
            append_float(l.confidence, &out);
            out += ",\"score\":";
            append_float(l.box.score, &out);
            out += ",\"region_id\":";
            out += std::to_string(l.region_id);
            out += ",\"quad\":[";
            for (int c = 0; c < 4; ++c) {
                if (c != 0) {
                    out += ',';
                }
                out += '[';
                append_float(l.box.corners[c].x, &out);
                out += ',';
                append_float(l.box.corners[c].y, &out);
                out += ']';
            }
            out += "]}";
        }
        out += "],\"regions\":[";
        for (size_t i = 0; i < regions_.size(); ++i) {
            const naina_region& r = regions_[i];
            if (i != 0) {
                out += ',';
            }
            out += "{\"kind\":\"";
            out += naina_region_kind_str(r.kind);
            out += "\",\"order\":";
            out += std::to_string(r.order);
            out += ",\"bbox\":[";
            append_float(r.bbox.x, &out);
            out += ',';
            append_float(r.bbox.y, &out);
            out += ',';
            append_float(r.bbox.w, &out);
            out += ',';
            append_float(r.bbox.h, &out);
            out += "]}";
        }
        out += "]}";
        json_ = std::move(out);
    }
    return json_->c_str();
}

}  // namespace naina::internal
