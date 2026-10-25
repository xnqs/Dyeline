#include "dyeline/tools.h"

#include <cmath>

namespace dyeline {

namespace {

// Exposure: stops of light, +1 doubles it.
void encode_exposure(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_EXPOSURE;
    op.p[0] = static_cast<float>(std::exp2(v[0]));
}

bool exposure_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

std::vector<ToolSpec> make_tools() {
    std::vector<ToolSpec> tools;
    tools.push_back({"exposure", "Exposure", ToolCategory::Matte, 1,
                     {{"stops", "Exposure", 0.0, -16.0, 16.0}},
                     nullptr, encode_exposure, exposure_is_identity});
    return tools;
}

}  // namespace

const std::vector<ToolSpec>& all_tools() {
    static const std::vector<ToolSpec> tools = make_tools();
    return tools;
}

const ToolSpec* find_tool(std::string_view id) {
    for (const ToolSpec& tool : all_tools()) {
        if (id == tool.id) {
            return &tool;
        }
    }
    return nullptr;
}

}  // namespace dyeline
