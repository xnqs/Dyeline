#include "dyeline/paths.h"

#include <cstdlib>
#include <filesystem>

namespace dyeline {

std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~' || (path.size() > 1 && path[1] != '/' && path[1] != '\\')) {
        return path;
    }
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home != nullptr && home[0] != '\0' ? std::string(home) + path.substr(1) : path;
}

std::string resolve_path(const std::string& path, const std::string& base_dir) {
    namespace fs = std::filesystem;
    const std::string expanded = expand_home(path);
    const fs::path p = fs::u8path(expanded);
    if (base_dir.empty() || p.empty() || p.is_absolute()) {
        return expanded;
    }
    return (fs::u8path(base_dir) / p).u8string();
}

}  // namespace dyeline
