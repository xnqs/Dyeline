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

// How the Look Designer shows a parameter. Every value is stored as a number either way.
enum class ParamKind {
    Number,   // a slider
    Toggle,   // a checkbox: 0 or 1
    Integer,  // a whole-number field
};

struct ParamSpec {
    const char* id;
    const char* label;
    double default_value;
    double min;
    double max;
    ParamKind kind = ParamKind::Number;
};

// A tool's parameter values, already migrated, defaulted and clamped, in ParamSpec order.
using ParamValues = std::vector<double>;

// What a tool that reads files gets when its op is built (see ToolSpec::prepare).
struct PrepareContext {
    // The entry's raw params, for values that aren't numbers, such as a file path.
    const nlohmann::json& params;
    const ParamValues& values;
    // The folder relative paths are resolved against: the look file's, or empty.
    const std::string& base_dir;
    // The chain's table data. A tool appends what it needs and records the offset in its op.
    std::vector<float>& table;
};

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
    // For tools that read files or need table data: used instead of `encode` when set.
    // Returns false to leave the tool out of the pass, with `problem` saying why when the
    // user should hear about it (a missing file, say), or empty when there is nothing to do.
    bool (*prepare)(PrepareContext& context, dl_op& op, std::string& problem);
    // The id of a parameter holding a file path, which the designer shows as a file field, or
    // null. It's stored in the entry's params as a string, beside the numbers.
    const char* file_param = nullptr;
};

// All registered tools, in tools-drawer order.
const std::vector<ToolSpec>& all_tools();

// The tool with this id, or null.
const ToolSpec* find_tool(std::string_view id);

}  // namespace dyeline
