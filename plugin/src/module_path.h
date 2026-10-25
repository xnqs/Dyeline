#pragma once

#include <string>

namespace dyeline::plugin {

// The folder holding the plugin binary, with a trailing separator, or "" if unknown.
std::string module_directory();

// Full path of the designer app inside the bundle.
std::string designer_path();

}  // namespace dyeline::plugin
