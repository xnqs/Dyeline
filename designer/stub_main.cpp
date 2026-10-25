// Stand-in for the Look Designer app, for the Milestone 0 round-trip spike. It has no window:
// it reads the look the plugin hands over, waits as if the user were editing, raises Exposure
// by one stop and hands the look back. The real designer (Dear ImGui on SDL3) replaces it in
// Milestone 1 and keeps this command line.
//
// Usage: dyeline-designer --in <look.json> --out <look.json> [--delay <seconds>] [--cancel]
// DYELINE_DESIGNER_DELAY sets the delay when --delay is not given (default 2 seconds), so a
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

int main(int argc, char** argv) {
    std::string in_path;
    std::string out_path;
    double delay = 2.0;
    if (const char* env = std::getenv("DYELINE_DESIGNER_DELAY")) {
        delay = std::atof(env);
    }
    bool cancel = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--cancel") == 0) {
            cancel = true;
        }
    }
    if (in_path.empty() || out_path.empty()) {
        std::cerr << "usage: dyeline-designer --in <look.json> --out <look.json>\n";
        return 2;
    }

    std::ifstream in(in_path, std::ios::binary);
    const std::string look((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    dyeline::ExposureParams params;
    if (!dyeline::designer::parse_look(look, params)) {
        std::cerr << "dyeline-designer: could not read the look in " << in_path << "\n";
        return 3;
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(std::max(delay, 0.0)));
    if (cancel) {
        return dyeline::designer::kExitCancelled;
    }

    params.stops = std::min(params.stops + 1.0, dyeline::kExposureMaxStops);
    std::ofstream out(out_path, std::ios::binary);
    out << dyeline::designer::format_look(params);
    return out ? dyeline::designer::kExitApplied : 4;
}
