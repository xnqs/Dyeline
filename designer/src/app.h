#pragma once

// The Look Designer window.

#include <memory>
#include <string>

#include "dyeline/frame_file.h"
#include "look_editor.h"

namespace dyeline::designer {

struct AppOptions {
    // The look file being edited (--file), or empty. Save writes it.
    std::string file_path;
    // The frame to preview (--frame), or null when the designer was opened without one.
    std::shared_ptr<const Frame> frame;
    // Where the look's relative paths, such as LUT files, are found: the look file's folder.
    std::string base_dir;
    // EffectCraft's control port (--effectcraft-port), or 0. Each save then makes EffectCraft
    // render the look file again.
    int effectcraft_port = 0;
    // True when the plugin waits for the result (--in/--out): the window offers Apply and
    // Cancel instead of Save and Close.
    bool round_trip = false;
    // For tests: draw this many frames, then act as if Apply (or Save) was pressed.
    int test_frames = 0;
    // For tests: opens on the Looks tab.
    bool test_looks = false;
    // For tests: exports the look as a .cube here before the last frame.
    std::string test_export_cube;
    // For tests: saves the last frame drawn as a .bmp.
    std::string test_screenshot;
};

enum class AppResult {
    Applied,    // the user applied or saved the look
    Cancelled,  // the user closed the window without applying
    Failed,     // the window could not be opened
};

// Runs the window until the user applies or closes it. `editor` holds the result.
AppResult run_app(LookEditor& editor, const AppOptions& options, std::string& error);

}  // namespace dyeline::designer
