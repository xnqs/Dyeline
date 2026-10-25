#pragma once

// A look: the tool chain plus its color settings, as saved in a .dyeline file. The same JSON
// is what the plugin keeps in its hidden parameter, so a preset and a clip's grade are one
// thing. The format is documented by schema/dyeline-look-1.schema.json.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dyeline {

inline constexpr int kLookFormatVersion = 1;

struct LookMeta {
    std::string name;
    std::vector<std::string> tags;
    std::string author;
    std::string license;
};

// Color settings: the footage's color space, the space to output, and how much to convert
// (see color.h). The defaults change nothing, which suits hosts that already hand plugins
// scene-linear images, such as Natron and Nuke.
struct LookColor {
    std::string mode = "tone";     // "tone" (1D) or "full" (gamut and tone)
    std::string input = "linear";  // a color space id from color.h
    std::string output = "same";   // "same" means the input space
};

// One tool in the chain. `params` holds the tool's own parameters; each tool versions them
// separately (see tools.h).
struct ToolEntry {
    std::string tool;
    int tool_version = 1;
    bool enabled = true;
    bool pinned = false;
    double strength = 1.0;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json extra = nlohmann::json::object();  // fields from a newer Dyeline, kept as is
};

struct Look {
    LookMeta meta;
    LookColor color;
    std::vector<ToolEntry> chain;
    nlohmann::json extra = nlohmann::json::object();  // fields from a newer Dyeline, kept as is
};

// Parses a look. Fails (with `error` set) on invalid JSON, a look from a newer Dyeline, or a
// malformed chain entry. Tools this build doesn't know are kept as they are.
bool parse_look(const std::string& text, Look& look, std::string& error);

// Writes a look as pretty-printed JSON.
std::string write_look(const Look& look);

// Reads or writes a .dyeline file. Writes go to a temporary file that is then renamed over the
// target, so a render never reads half a look.
bool read_look_file(const std::string& path, Look& look, std::string& error);
bool write_look_file(const std::string& path, const Look& look, std::string& error);

}  // namespace dyeline
