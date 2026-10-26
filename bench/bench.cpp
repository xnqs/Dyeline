// Times Dyeline's renders: each tool on its own at its default settings, then each bundled
// look, at 1080p and UHD, on the CPU and on Dyeline's own GPU device when there is one.
//
//   dyeline_bench [--looks <dir>] [--sizes 1080,2160] [--backends cpu,opencl,metal]
//                 [--runs N] [--only <text>]
//
// Prints a Markdown table of milliseconds per frame, the median of N runs after a warm-up.
// Times include what the plugin does per frame: compiling the look and planning the render,
// and for the own GPU device the upload and read-back.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/gpu.h"
#include "dyeline/library.h"
#include "dyeline/look.h"
#include "dyeline/render.h"
#include "dyeline/tools.h"

namespace {

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream in(text);
    for (std::string part; std::getline(in, part, ',');) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

// An RGBA frame with gradients, detail and highlights, so data-dependent tools do real work.
std::vector<float> make_frame(int width, int height) {
    std::vector<float> f(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float* p = &f[(static_cast<std::size_t>(y) * width + x) * 4];
            const bool bright = (x * 7 + y * 13) % 997 == 0;
            p[0] = bright ? 20.0f : 0.02f + 0.7f * static_cast<float>(x) / width;
            p[1] = bright ? 15.0f : 0.02f + 0.6f * static_cast<float>(y) / height;
            p[2] = bright ? 10.0f : 0.1f + 0.3f * std::fabs(std::sin(0.01f * (x + y)));
            p[3] = 1.0f;
        }
    }
    return f;
}

struct Case {
    std::string name;
    dyeline::Look look;
};

std::vector<Case> cases(const std::string& looks_dir) {
    std::vector<Case> out;
    for (const dyeline::ToolSpec& spec : dyeline::all_tools()) {
        if (spec.file_param != nullptr) {
            continue;  // needs a file (LUT)
        }
        dyeline::ToolEntry entry;
        entry.tool = spec.id;
        Case c;
        c.name = std::string("tool: ") + spec.label;
        c.look.chain.push_back(entry);
        dyeline::CompiledChain chain;
        std::string error;
        if (dyeline::compile_chain(c.look, 1.0, chain, error) && chain.pass.count > 0) {
            out.push_back(c);
        }
    }
    if (!looks_dir.empty()) {
        std::vector<std::string> problems;
        for (const dyeline::LibraryLook& look : dyeline::scan_library({{looks_dir, true}}, problems)) {
            out.push_back({"look: " + look.look.meta.name, look.look});
        }
    }
    return out;
}

using Renderer = std::function<std::string(const float*, std::size_t, float*, std::size_t,
                                           const dyeline::RenderPlan&, const dyeline::CompiledChain&)>;

}  // namespace

int main(int argc, char** argv) {
    std::string looks_dir = DYELINE_LOOKS_DIR;
    std::vector<std::string> sizes = {"1080", "2160"};
    std::vector<std::string> backends = {"cpu", "opencl", "metal"};
    std::string only;
    int runs = 5;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        const std::string value = argv[i + 1];
        if (flag == "--looks") looks_dir = value;
        else if (flag == "--sizes") sizes = split(value);
        else if (flag == "--backends") backends = split(value);
        else if (flag == "--runs") runs = std::max(1, std::atoi(value.c_str()));
        else if (flag == "--only") only = value;
        else {
            std::fprintf(stderr, "unknown option %s\n", flag.c_str());
            return 2;
        }
    }

    // The backends this machine has.
    std::vector<std::pair<std::string, Renderer>> renderers;
    for (const std::string& name : backends) {
        std::string error;
        if (name == "cpu") {
            renderers.push_back({"CPU", [](const float* src, std::size_t, float* dst, std::size_t,
                                           const dyeline::RenderPlan& plan,
                                           const dyeline::CompiledChain& chain) {
                                     dyeline::run_plan_cpu(src, dst, chain.table.data(), plan);
                                     return std::string();
                                 }});
        } else if (name == "opencl" && dyeline::opencl::available()) {
            const std::string device = dyeline::opencl::own_device_name(error);
            if (!device.empty()) {
                renderers.push_back({"OpenCL (" + device + ")",
                                     [](const float* src, std::size_t sf, float* dst, std::size_t df,
                                        const dyeline::RenderPlan& plan,
                                        const dyeline::CompiledChain& chain) {
                                         return dyeline::opencl::render_plan_own_device(
                                             src, sf, dst, df, plan, chain.table.data(),
                                             chain.table.size());
                                     }});
            } else {
                std::fprintf(stderr, "OpenCL: %s\n", error.c_str());
            }
        } else if (name == "metal" && dyeline::metal::available()) {
            const std::string device = dyeline::metal::own_device_name(error);
            if (!device.empty()) {
                renderers.push_back({"Metal (" + device + ")",
                                     [](const float* src, std::size_t sf, float* dst, std::size_t df,
                                        const dyeline::RenderPlan& plan,
                                        const dyeline::CompiledChain& chain) {
                                         return dyeline::metal::render_plan_own_device(
                                             src, sf, dst, df, plan, chain.table.data(),
                                             chain.table.size());
                                     }});
            }
        }
    }

    std::printf("| Case |");
    for (const auto& size : sizes) {
        for (const auto& r : renderers) {
            std::printf(" %s %sp |", r.first.c_str(), size.c_str());
        }
    }
    std::printf("\n| --- |");
    for (std::size_t i = 0; i < sizes.size() * renderers.size(); ++i) {
        std::printf(" ---: |");
    }
    std::printf("\n");

    struct Frame {
        int width;
        int height;
        std::vector<float> src;
        std::vector<float> dst;
    };
    std::vector<Frame> frames;
    for (const std::string& size : sizes) {
        const int height = std::atoi(size.c_str());
        const int width = height * 16 / 9;
        frames.push_back({width, height, make_frame(width, height),
                          std::vector<float>(static_cast<std::size_t>(width) * height * 4)});
    }

    for (const Case& c : cases(looks_dir)) {
        if (!only.empty() && c.name.find(only) == std::string::npos) {
            continue;
        }
        std::printf("| %s |", c.name.c_str());
        for (Frame& frame : frames) {
            dyeline::RenderGeometry g;
            g.window = {0, 0, frame.width, frame.height};
            g.src_bounds = g.window;
            g.dst_bounds = g.window;
            g.src_row_floats = frame.width * 4;
            g.dst_row_floats = frame.width * 4;
            g.channels = 4;
            g.frame_right = static_cast<float>(frame.width);
            g.frame_top = static_cast<float>(frame.height);
            for (const auto& r : renderers) {
                std::vector<double> times;
                std::string error;
                for (int run = 0; run <= runs && error.empty(); ++run) {
                    const auto start = std::chrono::steady_clock::now();
                    dyeline::CompiledChain chain;
                    dyeline::compile_chain(c.look, 1.0, chain, error);
                    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
                    error = r.second(frame.src.data(), frame.src.size(), frame.dst.data(),
                                     frame.dst.size(), plan, chain);
                    const auto end = std::chrono::steady_clock::now();
                    if (run > 0) {  // the first run warms up caches and builds GPU kernels
                        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
                    }
                }
                if (!error.empty()) {
                    std::printf(" error |");
                    std::fprintf(stderr, "%s: %s\n", c.name.c_str(), error.c_str());
                    continue;
                }
                std::sort(times.begin(), times.end());
                std::printf(" %.1f |", times[times.size() / 2]);
            }
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}
