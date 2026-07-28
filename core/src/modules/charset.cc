#include "charset.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace naina::internal::charset {

namespace {
const std::string& empty_string() {
    static const std::string kEmpty;
    return kEmpty;
}
}  // namespace

const std::string& Charset::at(int32_t cls) const {
    if (cls < 0 || cls >= num_classes()) {
        return empty_string();
    }
    return entries_[static_cast<size_t>(cls)];
}

void Charset::assign(std::vector<std::string> chars) {
    entries_.clear();
    entries_.reserve(chars.size() + 2);
    entries_.emplace_back();  // index 0: CTC blank, no character
    for (auto& c : chars) {
        entries_.push_back(std::move(c));
    }
    entries_.emplace_back(" ");  // final class: space
}

bool load_from_yaml(const std::filesystem::path& yaml_path, Charset* out) {
    if (out == nullptr) {
        return false;
    }
    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path.string());
    } catch (const std::exception&) {
        return false;
    }
    const YAML::Node pp = root["PostProcess"];
    if (!pp) {
        return false;
    }
    const YAML::Node dict = pp["character_dict"];
    if (!dict || !dict.IsSequence() || dict.size() == 0) {
        return false;
    }
    std::vector<std::string> chars;
    chars.reserve(dict.size());
    for (const auto& n : dict) {
        chars.push_back(n.as<std::string>(""));
    }
    out->assign(std::move(chars));
    return true;
}

}  // namespace naina::internal::charset
