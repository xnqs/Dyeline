#pragma once

// Paths typed by users, into a host's text field or into a look.

#include <string>

namespace dyeline {

// Expands a leading "~" to the home folder, since a path typed into a host's text field
// doesn't go through a shell.
std::string expand_home(const std::string& path);

// Expands "~", then resolves a relative path against `base_dir` when one is given (for a
// path inside a look file, the look file's folder).
std::string resolve_path(const std::string& path, const std::string& base_dir);

}  // namespace dyeline
