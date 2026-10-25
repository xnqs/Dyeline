// The Look Designer app. The plugin starts it to edit a look and waits for it to exit.
//
// Usage:
//   dyeline-designer --in <look.json> --out <look.json> [--file <look.dyeline>]
//   dyeline-designer --file <look.dyeline>
//
// --in/--out is the round trip with the plugin: the window offers Apply, which writes the
// edited look to <out> and exits with kExitApplied, and Cancel, which exits with
// kExitCancelled. --file edits a look file instead (created with a neutral look if it doesn't
// exist), which is how hosts like EffectCraft get the look; with --out too, Apply also saves
// the file. --test-frames <n> draws n frames and then applies, and --test-screenshot <file.bmp>
// saves the last one, for tests.

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

#include "app.h"
#include "dyeline/designer_link.h"
#include "dyeline/look.h"
#include "look_editor.h"

int main(int argc, char* argv[]) {
    std::string in_path;
    std::string out_path;
    dyeline::designer::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            options.file_path = argv[++i];
        } else if (std::strcmp(argv[i], "--test-frames") == 0 && i + 1 < argc) {
            options.test_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--test-screenshot") == 0 && i + 1 < argc) {
            options.test_screenshot = argv[++i];
        }
    }
    if (options.file_path.empty() && (in_path.empty() || out_path.empty())) {
        std::cerr << "usage: dyeline-designer --in <look.json> --out <look.json> | --file <look.dyeline>\n";
        return 2;
    }
    options.round_trip = !out_path.empty();

    // The look to start from: the look file when it exists, else what the plugin handed over,
    // else an empty look.
    dyeline::Look look;
    std::string error;
    if (!options.file_path.empty() && std::ifstream(options.file_path)) {
        if (!dyeline::read_look_file(options.file_path, look, error)) {
            std::cerr << "dyeline-designer: " << error << "\n";
            return 3;
        }
    } else if (!in_path.empty()) {
        std::ifstream in(in_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!dyeline::parse_look(text, look, error)) {
            std::cerr << "dyeline-designer: " << error << "\n";
            return 3;
        }
    }

    dyeline::designer::LookEditor editor(std::move(look));
    switch (dyeline::designer::run_app(editor, options, error)) {
        case dyeline::designer::AppResult::Cancelled:
            return dyeline::designer::kExitCancelled;
        case dyeline::designer::AppResult::Failed:
            std::cerr << "dyeline-designer: " << error << "\n";
            return 4;
        case dyeline::designer::AppResult::Applied:
            break;
    }
    if (out_path.empty()) {
        return dyeline::designer::kExitApplied;
    }
    std::ofstream out(out_path, std::ios::binary);
    out << dyeline::write_look(editor.look());
    return out ? dyeline::designer::kExitApplied : 4;
}
