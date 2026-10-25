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

bool compile_chain(const Look& look, double strength, CompiledChain& out, std::string& error) {
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
        dl_op& op = compiled.pass.ops[compiled.pass.count++];
        op = dl_op{};
        op.strength = static_cast<float>(tool_strength);
        tool->encode(values, op);
    }
    out = compiled;
    return true;
}

void render_chain_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                      bool premultiplied, const dl_chain& chain) {
    dl_image_args args{};
    args.width = static_cast<int>(pixel_count);
    args.height = 1;
    args.channels = channels;
    args.premultiplied = premultiplied ? 1 : 0;
    for (int x = 0; x < args.width; ++x) {
        dl_chain_at(src, dst, x, 0, args, &chain);
    }
}

}  // namespace dyeline
