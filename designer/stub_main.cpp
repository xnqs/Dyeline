// A windowless stand-in for the Look Designer, for the round-trip tests and for builds with
// DYELINE_BUILD_DESIGNER=OFF. It reads the look the plugin hands over, waits as if the user
// were editing, raises the look's Exposure tool by one stop (adding one if there is none) and
// hands the look back. It takes the real designer's command line.
//
// Usage:
//   dyeline-designer --in <look.json> --out <look.json> [--file <look.dyeline>]
//                    [--delay <seconds>] [--cancel]
//   dyeline-designer --file <look.dyeline>
//
// --in/--out is the round trip with the plugin. --file edits a look file instead (created
// with a neutral look if it doesn't exist), which is how hosts like EffectCraft get the look;
// with --out too, the saved look is also handed back. DYELINE_DESIGNER_DELAY sets the delay
// when --delay is not given (default 2 seconds with --in, none with only --file), so a
// longer wait can be tried from inside a host.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "dyeline/designer_link.h"
#include "dyeline/look.h"

int main(int argc, char** argv) {
    std::string in_path;
    std::string out_path;
    std::string file_path;
    double delay = -1.0;
    bool cancel = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--cancel") == 0) {
            cancel = true;
        }
    }
    if (file_path.empty() && (in_path.empty() || out_path.empty())) {
        std::cerr << "usage: dyeline-designer --in <look.json> --out <look.json> | --file <look.dyeline>\n";
        return 2;
    }
    if (delay < 0.0) {
        const char* env = std::getenv("DYELINE_DESIGNER_DELAY");
        delay = env != nullptr ? std::atof(env) : (in_path.empty() ? 0.0 : 2.0);
    }

    // The look to start from: the look file when it exists, else what the plugin handed over,
    // else an empty look.
    dyeline::Look look;
    std::string error;
    if (!file_path.empty() && std::ifstream(file_path)) {
        if (!dyeline::read_look_file(file_path, look, error)) {
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

    std::this_thread::sleep_for(std::chrono::duration<double>(std::max(delay, 0.0)));
    if (cancel) {
        return dyeline::designer::kExitCancelled;
    }

    // The stand-in edit: one more stop on the first Exposure tool.
    auto exposure = std::find_if(look.chain.begin(), look.chain.end(),
                                 [](const dyeline::ToolEntry& e) { return e.tool == "exposure"; });
    if (exposure == look.chain.end()) {
        dyeline::ToolEntry entry;
        entry.tool = "exposure";
        entry.params["stops"] = 0.0;
        look.chain.push_back(entry);
        exposure = look.chain.end() - 1;
    }
    const double stops = exposure->params.value("stops", 0.0);
    exposure->params["stops"] = std::min(stops + 1.0, 16.0);

    if (!file_path.empty()) {
        if (!dyeline::write_look_file(file_path, look, error)) {
            std::cerr << "dyeline-designer: " << error << "\n";
            return 4;
        }
        std::cout << "Saved " << file_path << ": exposure " << exposure->params["stops"].get<double>()
                  << " stops\n";
    }
    if (out_path.empty()) {
        return dyeline::designer::kExitApplied;
    }
    std::ofstream out(out_path, std::ios::binary);
    out << dyeline::write_look(look);
    return out ? dyeline::designer::kExitApplied : 4;
}
