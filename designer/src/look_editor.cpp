#include "look_editor.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dyeline::designer {

namespace {

// Undo keeps this many steps.
constexpr std::size_t kUndoLimit = 200;

// Brings an entry saved by an older version of its tool up to date, so the designer edits
// the parameters the current tool has. Entries this build can't run are left alone.
void migrate(ToolEntry& entry) {
    const ToolSpec* tool = find_tool(entry.tool);
    if (tool == nullptr || entry.tool_version >= tool->version) {
        return;
    }
    if (tool->migrate != nullptr) {
        tool->migrate(entry.tool_version, entry.params);
    }
    entry.tool_version = tool->version;
}

}  // namespace

LookEditor::LookEditor(Look look) : look_(std::move(look)) {
    for (ToolEntry& entry : look_.chain) {
        migrate(entry);
    }
}

const ToolSpec* LookEditor::tool_at(std::size_t i) const {
    if (!valid(i)) {
        return nullptr;
    }
    const ToolSpec* tool = find_tool(look_.chain[i].tool);
    if (tool == nullptr || look_.chain[i].tool_version > tool->version) {
        return nullptr;
    }
    return tool;
}

std::size_t LookEditor::add_tool(std::string_view id, std::size_t at) {
    const ToolSpec* tool = find_tool(id);
    if (tool == nullptr) {
        return look_.chain.size();
    }
    checkpoint({});
    ToolEntry entry;
    entry.tool = tool->id;
    entry.tool_version = tool->version;
    for (const ParamSpec& spec : tool->params) {
        entry.params[spec.id] = spec.default_value;
    }
    at = std::min(at, look_.chain.size());
    look_.chain.insert(look_.chain.begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
    return at;
}

void LookEditor::remove(std::size_t i) {
    if (!valid(i)) {
        return;
    }
    checkpoint({});
    look_.chain.erase(look_.chain.begin() + static_cast<std::ptrdiff_t>(i));
}

void LookEditor::duplicate(std::size_t i) {
    if (!valid(i)) {
        return;
    }
    checkpoint({});
    const ToolEntry copy = look_.chain[i];
    look_.chain.insert(look_.chain.begin() + static_cast<std::ptrdiff_t>(i) + 1, copy);
}

void LookEditor::move(std::size_t from, std::size_t to) {
    if (!valid(from)) {
        return;
    }
    to = std::min(to, look_.chain.size() - 1);
    if (from == to) {
        return;
    }
    checkpoint({});
    ToolEntry entry = std::move(look_.chain[from]);
    look_.chain.erase(look_.chain.begin() + static_cast<std::ptrdiff_t>(from));
    look_.chain.insert(look_.chain.begin() + static_cast<std::ptrdiff_t>(to), std::move(entry));
}

void LookEditor::set_enabled(std::size_t i, bool enabled) {
    if (!valid(i) || look_.chain[i].enabled == enabled) {
        return;
    }
    checkpoint({});
    look_.chain[i].enabled = enabled;
}

void LookEditor::set_strength(std::size_t i, double strength) {
    if (!valid(i)) {
        return;
    }
    strength = std::clamp(strength, 0.0, 1.0);
    if (look_.chain[i].strength == strength) {
        return;
    }
    checkpoint("strength:" + std::to_string(i));
    look_.chain[i].strength = strength;
}

double LookEditor::param(std::size_t i, const ParamSpec& spec) const {
    double value = spec.default_value;
    if (valid(i)) {
        const nlohmann::json& params = look_.chain[i].params;
        if (const auto it = params.find(spec.id); it != params.end() && it->is_number()) {
            value = it->get<double>();
        }
    }
    return std::clamp(value, spec.min, spec.max);
}

void LookEditor::set_param(std::size_t i, const ParamSpec& spec, double value) {
    if (!valid(i) || tool_at(i) == nullptr) {
        return;
    }
    value = std::clamp(value, spec.min, spec.max);
    if (spec.kind != ParamKind::Number) {
        value = std::round(value);
    }
    nlohmann::json& params = look_.chain[i].params;
    if (const auto it = params.find(spec.id);
        it != params.end() && it->is_number() && it->get<double>() == value) {
        return;
    }
    checkpoint("param:" + std::to_string(i) + ":" + spec.id);
    params[spec.id] = value;
}

void LookEditor::reset_param(std::size_t i, const ParamSpec& spec) {
    end_gesture();
    set_param(i, spec, spec.default_value);
    end_gesture();
}

std::string LookEditor::text_param(std::size_t i, const char* id) const {
    if (!valid(i)) {
        return {};
    }
    const nlohmann::json& params = look_.chain[i].params;
    const auto it = params.find(id);
    return it != params.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void LookEditor::set_text_param(std::size_t i, const char* id, const std::string& value) {
    if (!valid(i) || tool_at(i) == nullptr || text_param(i, id) == value) {
        return;
    }
    checkpoint("text:" + std::to_string(i) + ":" + id);
    look_.chain[i].params[id] = value;
}

void LookEditor::set_name(const std::string& name) {
    if (look_.meta.name == name) {
        return;
    }
    checkpoint("name");
    look_.meta.name = name;
}

void LookEditor::set_color(const LookColor& color) {
    if (look_.color.mode == color.mode && look_.color.input == color.input &&
        look_.color.output == color.output) {
        return;
    }
    checkpoint({});
    look_.color = color;
}

bool LookEditor::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(std::move(look_));
    look_ = std::move(undo_.back());
    undo_.pop_back();
    gesture_.clear();
    modified_ = true;
    return true;
}

bool LookEditor::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(std::move(look_));
    look_ = std::move(redo_.back());
    redo_.pop_back();
    gesture_.clear();
    modified_ = true;
    return true;
}

void LookEditor::checkpoint(const std::string& key) {
    modified_ = true;
    redo_.clear();
    if (!key.empty() && key == gesture_) {
        return;
    }
    gesture_ = key;
    undo_.push_back(look_);
    if (undo_.size() > kUndoLimit) {
        undo_.erase(undo_.begin());
    }
}

}  // namespace dyeline::designer
