// Renders a frame file through a look on the CPU, the reference host tests compare against:
//
//   dyeline_render_frame <look.dyeline> <in.frame> <out.frame> [strength]
//
// The frame's header gives the render geometry, as the plugin would work it out in a host.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "dyeline/chain.h"
#include "dyeline/frame_file.h"
#include "dyeline/look.h"
#include "dyeline/render.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <look.dyeline> <in.frame> <out.frame> [strength]\n",
                     argv[0]);
        return 2;
    }
    std::string error;
    dyeline::Look look;
    dyeline::Frame frame;
    if (!dyeline::read_look_file(argv[1], look, error) ||
        !dyeline::read_frame_file(argv[2], frame, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    dyeline::CompiledChain chain;
    const double strength = argc > 4 ? std::atof(argv[4]) : 1.0;
    if (!dyeline::compile_chain(look, strength, chain, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    for (const std::string& problem : chain.problems) {
        std::fprintf(stderr, "warning: %s\n", problem.c_str());
    }

    dyeline::RenderGeometry g;
    g.window = {frame.x0, frame.y0, frame.x0 + frame.width, frame.y0 + frame.height};
    g.src_bounds = g.window;
    g.dst_bounds = g.window;
    g.src_row_floats = frame.width * 4;
    g.dst_row_floats = frame.width * 4;
    g.channels = 4;
    g.premultiplied = frame.premultiplied;
    g.canonical_x = frame.canonical_x;
    g.canonical_y = frame.canonical_y;
    g.frame_left = frame.frame_left;
    g.frame_bottom = frame.frame_bottom;
    g.frame_right = frame.frame_right;
    g.frame_top = frame.frame_top;
    g.seed = frame.seed;

    dyeline::Frame out = frame;
    dyeline::run_plan_cpu(frame.rgba.data(), out.rgba.data(), chain.table.data(),
                          dyeline::plan_render(chain.pass, g));
    if (!dyeline::write_frame_file(argv[3], out, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
}
