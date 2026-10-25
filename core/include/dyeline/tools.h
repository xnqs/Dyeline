#pragma once

// The tool registry: every tool Dyeline knows, its parameters, and how it becomes an op in the
// fused per-pixel pass. Saved looks name tools by `id`, so an id never changes once released.
//
// Each tool versions its own parameters. When a tool's parameters change, its version goes up
// and `migrate` learns to bring older entries forward, so old looks keep loading. An entry from
// a newer version than this build knows is treated like an unknown tool: kept, but bypassed.

#include <string>
#include <string_view>
#include <vector>

#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"

namespace dyeline {

enum class ToolCategory { Subject, Matte, Lens, Camera, Post };

struct ParamSpec {
    const char* id;
    const char* label;
    double default_value;
    double min;
    double max;
};

// A tool's parameter values, already migrated, defaulted and clamped, in ParamSpec order.
using ParamValues = std::vector<double>;

struct ToolSpec {
    const char* id;
    const char* label;
    ToolCategory category;
    int version;
    std::vector<ParamSpec> params;
    // Brings an entry's params from an older version up to `version`; may be null while a
    // tool is still at version 1.
    void (*migrate)(int from_version, nlohmann::json& params);
    // Fills the op for one frame. Never called with a strength of 0.
    void (*encode)(const ParamValues& values, dl_op& op);
    // True when the tool would leave every pixel unchanged at these values.
    bool (*is_identity)(const ParamValues& values);
};

// All registered tools, in tools-drawer order.
const std::vector<ToolSpec>& all_tools();

// The tool with this id, or null.
const ToolSpec* find_tool(std::string_view id);

}  // namespace dyeline
