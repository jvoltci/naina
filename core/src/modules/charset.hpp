// CTC charset: class index -> character.
//
// PaddleOCR stores the recognition charset as PostProcess.character_dict in
// the model's own inference.yml. naina fetches that file as the
// `charset_yaml` manifest kind and parses it here.
//
// Class layout (PaddleOCR CTCLabelDecode):
//   index 0            CTC blank, no character
//   index 1..N         character_dict[i - 1]
//   index N + 1        space (use_space_char)
// so num_classes == character_dict.size() + 2.
#ifndef NAINA_INTERNAL_CHARSET_HPP
#define NAINA_INTERNAL_CHARSET_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace naina::internal::charset {

class Charset {
public:
    // Total number of CTC classes, including blank and space.
    int32_t num_classes() const { return static_cast<int32_t>(entries_.size()); }

    // Character for a class index. Returns an empty string for the blank
    // class and for any out-of-range index — callers skip empties.
    const std::string& at(int32_t cls) const;

    // Replace the character list. `chars` excludes blank and space; this
    // adds both. Exposed for tests and for the yaml loader.
    void assign(std::vector<std::string> chars);

private:
    // entries_[0] is the blank; the last entry is the space.
    std::vector<std::string> entries_;
};

// Parse PostProcess.character_dict out of a PaddleOCR inference.yml.
// Returns false if the file cannot be read, has no character_dict, or that
// list is empty — a silent empty charset would decode every page to "".
bool load_from_yaml(const std::filesystem::path& yaml_path, Charset* out);

}  // namespace naina::internal::charset

#endif  // NAINA_INTERNAL_CHARSET_HPP
