// The Look Designer's editing model: every edit the window can make.

#include <gtest/gtest.h>

#include <string>

#include "dyeline/look.h"
#include "dyeline/tools.h"
#include "look_editor.h"

namespace {

using dyeline::Look;
using dyeline::ParamSpec;
using dyeline::ToolEntry;
using dyeline::designer::LookEditor;

const ParamSpec& spec_of(const char* tool, const char* param) {
    for (const ParamSpec& spec : dyeline::find_tool(tool)->params) {
        if (std::string(spec.id) == param) {
            return spec;
        }
    }
    throw std::runtime_error("no such parameter");
}

std::vector<std::string> tool_ids(const LookEditor& editor) {
    std::vector<std::string> ids;
    for (const ToolEntry& entry : editor.look().chain) {
        ids.push_back(entry.tool);
    }
    return ids;
}

LookEditor three_tools() {
    LookEditor editor{Look{}};
    editor.add_tool("exposure", 0);
    editor.add_tool("contrast", 1);
    editor.add_tool("grain", 2);
    return editor;
}

}  // namespace

TEST(LookEditor, AddsToolsWithTheirDefaults) {
    LookEditor editor{Look{}};
    EXPECT_FALSE(editor.modified());
    EXPECT_EQ(editor.add_tool("contrast", 5), 0u);  // past the end goes at the end
    EXPECT_EQ(editor.add_tool("exposure", 0), 0u);
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"exposure", "contrast"}));
    const ToolEntry& contrast = editor.look().chain[1];
    EXPECT_EQ(contrast.tool_version, dyeline::find_tool("contrast")->version);
    EXPECT_DOUBLE_EQ(contrast.params.at("contrast").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(contrast.params.at("pivot").get<double>(), 0.18);
    EXPECT_TRUE(editor.modified());
}

TEST(LookEditor, IgnoresUnknownTools) {
    LookEditor editor{Look{}};
    EXPECT_EQ(editor.add_tool("teleport", 0), 0u);
    EXPECT_TRUE(editor.look().chain.empty());
    EXPECT_FALSE(editor.can_undo());
}

TEST(LookEditor, MovesDuplicatesAndRemoves) {
    LookEditor editor = three_tools();
    editor.move(0, 2);
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"contrast", "grain", "exposure"}));
    editor.move(2, 0);
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"exposure", "contrast", "grain"}));
    editor.duplicate(1);
    EXPECT_EQ(tool_ids(editor),
              (std::vector<std::string>{"exposure", "contrast", "contrast", "grain"}));
    editor.remove(0);
    editor.remove(9);  // out of range: nothing happens
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"contrast", "contrast", "grain"}));
}

TEST(LookEditor, ReadsParamsAsTheRendererDoes) {
    Look look;
    ToolEntry entry;
    entry.tool = "exposure";
    entry.params = {{"stops", 40.0}};
    look.chain.push_back(entry);
    entry.params = {{"stops", "loud"}};
    look.chain.push_back(entry);
    const LookEditor editor(look);
    const ParamSpec& stops = spec_of("exposure", "stops");
    EXPECT_DOUBLE_EQ(editor.param(0, stops), 16.0);  // clamped to the range
    EXPECT_DOUBLE_EQ(editor.param(1, stops), 0.0);   // not a number: the default
}

TEST(LookEditor, SetsAndResetsParams) {
    LookEditor editor = three_tools();
    const ParamSpec& contrast = spec_of("contrast", "contrast");
    editor.set_param(1, contrast, 2.5);
    EXPECT_DOUBLE_EQ(editor.param(1, contrast), 2.5);
    editor.set_param(1, contrast, 99.0);
    EXPECT_DOUBLE_EQ(editor.param(1, contrast), 4.0);
    editor.reset_param(1, contrast);
    EXPECT_DOUBLE_EQ(editor.param(1, contrast), 1.0);
}

TEST(LookEditor, RoundsTogglesAndWholeNumbers) {
    LookEditor editor = three_tools();
    editor.set_param(2, spec_of("grain", "seed"), 41.6);
    EXPECT_DOUBLE_EQ(editor.param(2, spec_of("grain", "seed")), 42.0);
    editor.set_param(2, spec_of("grain", "animate"), 0.2);
    EXPECT_DOUBLE_EQ(editor.param(2, spec_of("grain", "animate")), 0.0);
}

TEST(LookEditor, EditsTextParams) {
    LookEditor editor{Look{}};
    editor.add_tool("lut", 0);
    EXPECT_STREQ(dyeline::find_tool("lut")->file_param, "file");
    EXPECT_EQ(editor.text_param(0, "file"), "");
    editor.set_text_param(0, "file", "looks/warm.cube");
    EXPECT_EQ(editor.look().chain[0].params.at("file").get<std::string>(), "looks/warm.cube");
    EXPECT_EQ(editor.text_param(0, "file"), "looks/warm.cube");
}

TEST(LookEditor, ClampsStrengthAndToggles) {
    LookEditor editor = three_tools();
    editor.set_strength(0, 1.7);
    EXPECT_DOUBLE_EQ(editor.look().chain[0].strength, 1.0);
    editor.set_strength(0, 0.4);
    EXPECT_DOUBLE_EQ(editor.look().chain[0].strength, 0.4);
    editor.set_enabled(1, false);
    EXPECT_FALSE(editor.look().chain[1].enabled);
}

TEST(LookEditor, UndoesAndRedoes) {
    LookEditor editor = three_tools();
    editor.remove(0);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"exposure", "contrast", "grain"}));
    ASSERT_TRUE(editor.redo());
    EXPECT_EQ(tool_ids(editor), (std::vector<std::string>{"contrast", "grain"}));
    EXPECT_FALSE(editor.redo());
    // A new edit drops what could have been redone.
    editor.undo();
    editor.set_enabled(0, false);
    EXPECT_FALSE(editor.can_redo());
    // Undo goes all the way back to the empty look.
    while (editor.undo()) {
    }
    EXPECT_TRUE(editor.look().chain.empty());
}

TEST(LookEditor, ADragIsOneUndoStep) {
    LookEditor editor = three_tools();
    const ParamSpec& stops = spec_of("exposure", "stops");
    for (double v = 0.1; v < 2.0; v += 0.1) {
        editor.set_param(0, stops, v);  // one gesture, as a slider being dragged
    }
    editor.end_gesture();
    editor.set_param(0, stops, 3.0);  // the next drag
    editor.end_gesture();
    ASSERT_TRUE(editor.undo());
    EXPECT_NEAR(editor.param(0, stops), 1.9, 1e-9);
    ASSERT_TRUE(editor.undo());
    EXPECT_DOUBLE_EQ(editor.param(0, stops), 0.0);
}

TEST(LookEditor, EditsTheColorSettingsAndName) {
    LookEditor editor{Look{}};
    dyeline::LookColor color;
    color.mode = "full";
    color.input = "logc3";
    color.output = "rec709";
    editor.set_color(color);
    editor.set_name("Warm night");
    EXPECT_EQ(editor.look().color.input, "logc3");
    EXPECT_EQ(editor.look().meta.name, "Warm night");
    editor.undo();
    editor.undo();
    EXPECT_EQ(editor.look().color.input, "linear");
}

TEST(LookEditor, KeepsToolsItCannotEdit) {
    Look look;
    ToolEntry future;
    future.tool = "exposure";
    future.tool_version = 99;  // from a newer Dyeline
    future.params = {{"stops", 2.0}, {"curve", "new"}};
    look.chain.push_back(future);
    ToolEntry unknown;
    unknown.tool = "teleport";
    look.chain.push_back(unknown);
    LookEditor editor(look);
    EXPECT_EQ(editor.tool_at(0), nullptr);
    EXPECT_EQ(editor.tool_at(1), nullptr);
    editor.set_param(0, spec_of("exposure", "stops"), 5.0);  // refused
    EXPECT_EQ(editor.look().chain[0].params, future.params);
    EXPECT_EQ(editor.look().chain[0].tool_version, 99);
    editor.move(1, 0);  // but they can still be moved and removed
    EXPECT_EQ(editor.look().chain[0].tool, "teleport");
}

TEST(LookEditor, SavedClearsModified) {
    LookEditor editor = three_tools();
    EXPECT_TRUE(editor.modified());
    editor.mark_saved();
    EXPECT_FALSE(editor.modified());
}
