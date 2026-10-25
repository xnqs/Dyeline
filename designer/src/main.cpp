// The Look Designer app. The plugin starts it to edit a look and waits for it to exit, or,
// for Open Designer, starts it on a look file and carries on.
//
// Usage:
//   dyeline-designer --in <look.json> --out <look.json> [--file <look.dyeline>]
//                    [--frame <frame.dlframe>] [--effectcraft-port <port>]
//   dyeline-designer --file <look.dyeline>
//   dyeline-designer
//
// --in/--out is the round trip with the plugin: the window offers Apply, which writes the
// edited look to <out> and exits with kExitApplied, and Cancel, which exits with
// kExitCancelled. --file edits a look file instead (created with a neutral look if it doesn't
// exist), which is how hosts like EffectCraft get the look; with --out too, Apply also saves
// the file. --frame names the host's current frame (see frame_file.h), shown in the preview.
// --effectcraft-port names EffectCraft's control port, through which each save makes it
// render the look file again.
// With no arguments, as from the desktop menu, it starts a new look file in the user's looks.
// --test-frames <n> draws n frames and then applies, --test-looks opens on the looks
// browser, --test-export-cube <file.cube> exports the look, and --test-screenshot
// <file.bmp> saves the last frame, for tests.

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "app.h"
#include "dyeline/designer_link.h"
#include "dyeline/effectcraft.h"
#include "dyeline/frame_file.h"
#include "dyeline/library.h"
#include "dyeline/look.h"
#include "look_editor.h"

namespace {

// "Untitled.dyeline" in the user's looks folder, or "Untitled 2.dyeline" and so on when taken.
std::string new_look_path() {
    namespace fs = std::filesystem;
    const std::string dir = dyeline::user_looks_dir();
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    for (int n = 1;; ++n) {
        const std::string name = n == 1 ? "Untitled.dyeline" : "Untitled " + std::to_string(n) + ".dyeline";
        const fs::path path = fs::u8path(dir) / fs::u8path(name);
        if (!fs::exists(path, ec)) {
            return path.u8string();
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string in_path;
    std::string out_path;
    std::string frame_path;
    dyeline::designer::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            options.file_path = argv[++i];
        } else if (std::strcmp(argv[i], "--effectcraft-port") == 0 && i + 1 < argc) {
            options.effectcraft_port = dyeline::effectcraft::parse_port(argv[++i]);
        } else if (std::strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            frame_path = argv[++i];
        } else if (std::strcmp(argv[i], "--test-frames") == 0 && i + 1 < argc) {
            options.test_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--test-export-cube") == 0 && i + 1 < argc) {
            options.test_export_cube = argv[++i];
        } else if (std::strcmp(argv[i], "--test-looks") == 0) {
            options.test_looks = true;
        } else if (std::strcmp(argv[i], "--test-screenshot") == 0 && i + 1 < argc) {
            options.test_screenshot = argv[++i];
        }
    }
    if (in_path.empty() != out_path.empty()) {
        std::cerr << "usage: dyeline-designer --in <look.json> --out <look.json> | --file <look.dyeline>\n";
        return 2;
    }
    if (options.file_path.empty() && in_path.empty()) {
        // Started from the desktop menu: a new look in the user's own looks, so it shows up in
        // the looks browser and in hosts' Look File fields.
        options.file_path = new_look_path();
        if (options.file_path.empty()) {
            std::cerr << "dyeline-designer: no home folder to save the look in; use --file\n";
            return 2;
        }
    }
    options.round_trip = !out_path.empty();
    if (!options.file_path.empty()) {
        options.base_dir = std::filesystem::u8path(options.file_path).parent_path().u8string();
    }
    if (!frame_path.empty()) {
        // Without a readable frame the designer still opens, just without a preview.
        auto frame = std::make_shared<dyeline::Frame>();
        std::string frame_error;
        if (dyeline::read_frame_file(frame_path, *frame, frame_error)) {
            options.frame = std::move(frame);
        } else {
            std::cerr << "dyeline-designer: no preview: " << frame_error << "\n";
        }
    }

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
