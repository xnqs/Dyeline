#pragma once

// The Look Designer window.

#include <string>

#include "look_editor.h"

namespace dyeline::designer {

struct AppOptions {
    // The look file being edited (--file), or empty. Save writes it.
    std::string file_path;
    // True when the plugin waits for the result (--in/--out): the window offers Apply and
    // Cancel instead of Save and Close.
    bool round_trip = false;
    // For tests: draw this many frames, then act as if Apply (or Save) was pressed.
    int test_frames = 0;
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
