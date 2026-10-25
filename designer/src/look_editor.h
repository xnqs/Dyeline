#pragma once

// The Look Designer's model: a look being edited, with undo and redo. The window (app.cpp)
// only draws it and calls these, so every edit the designer can make is tested here without a
// window.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dyeline/look.h"
#include "dyeline/tools.h"

namespace dyeline::designer {

class LookEditor {
public:
    explicit LookEditor(Look look);

    const Look& look() const { return look_; }

    // True when the look differs from the one the editor was opened with, or last saved.
    bool modified() const { return modified_; }
    // Goes up with every change to the look, including undo and redo.
    int revision() const { return revision_; }
    void mark_saved() { modified_ = false; }

    // The registry entry for chain entry `i`, or null when this build can't edit it (an
    // unknown tool, or one saved by a newer Dyeline). Such entries are kept as they are.
    const ToolSpec* tool_at(std::size_t i) const;

    // Chain edits. `add_tool` inserts a tool with its default parameters at `at` (clamped to
    // the chain's end) and returns where it went.
    std::size_t add_tool(std::string_view id, std::size_t at);
    void remove(std::size_t i);
    void duplicate(std::size_t i);
    void move(std::size_t from, std::size_t to);
    void set_enabled(std::size_t i, bool enabled);
    void set_strength(std::size_t i, double strength);

    // A parameter's value as the renderer sees it: the saved number, or the default when there
    // is none, clamped to its range.
    double param(std::size_t i, const ParamSpec& spec) const;
    void set_param(std::size_t i, const ParamSpec& spec, double value);
    // Sets several of entry i's parameters at once, as one undo step (an on-image handle
    // moving a center sets x and y together).
    void set_params(std::size_t i, const std::vector<std::pair<const ParamSpec*, double>>& values);
    void reset_param(std::size_t i, const ParamSpec& spec);

    // A text parameter, such as a LUT's file path.
    std::string text_param(std::size_t i, const char* id) const;
    void set_text_param(std::size_t i, const char* id, const std::string& value);

    void set_name(const std::string& name);
    // Replaces the chain, name and tags with a preset's, as one undo step. The color settings
    // stay: they describe the footage, and presets are graded in the working space.
    void apply_preset(const Look& preset);
    void set_color(const LookColor& color);

    // Repeated changes to the same control (a slider being dragged) are one undo step until
    // the UI calls end_gesture, when the user lets go.
    void end_gesture() { gesture_.clear(); }

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

private:
    // Saves the look for undo before a change, unless the change continues the gesture `key`.
    void checkpoint(const std::string& key);
    bool valid(std::size_t i) const { return i < look_.chain.size(); }

    Look look_;
    std::vector<Look> undo_;
    std::vector<Look> redo_;
    std::string gesture_;
    bool modified_ = false;
    int revision_ = 0;
};

}  // namespace dyeline::designer
