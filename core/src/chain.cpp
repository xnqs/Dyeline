#include "dyeline/chain.h"

#include <algorithm>

#include "dyeline/tools.h"

namespace dyeline {

namespace {

// The entry's parameters for the current tool version, with missing or non-numeric values
// set to their defaults and every value clamped to its range.
ParamValues resolve_params(const ToolSpec& tool, const ToolEntry& entry) {
    nlohmann::json params = entry.params;
    if (entry.tool_version < tool.version && tool.migrate != nullptr) {
        tool.migrate(entry.tool_version, params);
    }
    ParamValues values;
    values.reserve(tool.params.size());
    for (const ParamSpec& spec : tool.params) {
        double value = spec.default_value;
        if (const auto it = params.find(spec.id); it != params.end() && it->is_number()) {
            value = it->get<double>();
        }
        values.push_back(std::clamp(value, spec.min, spec.max));
    }
    return values;
}

}  // namespace

bool compile_chain(const Look& look, double strength, CompiledChain& out, std::string& error,
                   const std::string& base_dir) {
    CompiledChain compiled;
    compiled.pass.strength = static_cast<float>(std::clamp(strength, 0.0, 1.0));
    for (const ToolEntry& entry : look.chain) {
        const ToolSpec* tool = find_tool(entry.tool);
        if (tool == nullptr || entry.tool_version > tool->version) {
            compiled.missing_tools.push_back(entry.tool);
            continue;
        }
        const double tool_strength = std::clamp(entry.strength, 0.0, 1.0);
        if (!entry.enabled || tool_strength == 0.0) {
            continue;
        }
        const ParamValues values = resolve_params(*tool, entry);
        if (tool->is_identity(values)) {
            continue;
        }
        if (compiled.pass.count == DL_MAX_OPS) {
            error = "a look can have at most " + std::to_string(DL_MAX_OPS) + " active tools";
            return false;
        }
        dl_op op{};
        op.strength = static_cast<float>(tool_strength);
        if (tool->prepare != nullptr) {
            PrepareContext context{entry.params, values, base_dir, compiled.table};
            std::string problem;
            if (!tool->prepare(context, op, problem)) {
                if (!problem.empty()) {
                    compiled.problems.push_back(std::string(tool->label) + ": " + problem);
                }
                continue;
            }
        } else {
            tool->encode(values, op);
        }
        compiled.pass.ops[compiled.pass.count++] = op;
    }
    out = compiled;
    return true;
}

void render_chain_window(const float* src, float* dst, const dl_image_args& args,
                         const CompiledChain& chain) {
    for (int y = 0; y < args.height; ++y) {
        for (int x = 0; x < args.width; ++x) {
            dl_chain_at(src, dst, x, y, args, &chain.pass, chain.table.data(), nullptr);
        }
    }
}

void render_chain_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                      bool premultiplied, const CompiledChain& chain) {
    dl_image_args args{};
    args.width = static_cast<int>(pixel_count);
    args.height = 1;
    args.channels = channels;
    args.premultiplied = premultiplied ? 1 : 0;
    args.src_width = args.width;
    args.src_height = 1;
    args.canonical_x = 1.0f;
    args.canonical_y = 1.0f;
    args.frame_right = static_cast<float>(pixel_count);
    args.frame_top = 1.0f;
    render_chain_window(src, dst, args, chain);
}

}  // namespace dyeline
