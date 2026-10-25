#include "dyeline/library.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "dyeline/paths.h"
#include "dyeline/tools.h"

namespace dyeline {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

std::vector<LibraryLook> scan_library(const std::vector<LibraryRoot>& roots,
                                      std::vector<std::string>& problems) {
    std::vector<LibraryLook> looks;
    for (const LibraryRoot& root : roots) {
        std::error_code ec;
        const fs::path base = fs::u8path(root.path);
        if (root.path.empty() || !fs::is_directory(base, ec)) {
            continue;
        }
        const std::size_t first = looks.size();
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || lower(it->path().extension().u8string()) != ".dyeline") {
                continue;
            }
            LibraryLook entry;
            entry.path = it->path().u8string();
            entry.bundled = root.bundled;
            const fs::path relative = it->path().lexically_relative(base);
            entry.folder = relative.parent_path().generic_u8string();
            entry.key = std::string(root.bundled ? "bundled/" : "user/") + relative.generic_u8string();
            std::string error;
            if (!read_look_file(entry.path, entry.look, error)) {
                problems.push_back(error);
                continue;
            }
            if (entry.look.meta.name.empty()) {
                entry.look.meta.name = it->path().stem().u8string();
            }
            looks.push_back(std::move(entry));
        }
        std::sort(looks.begin() + static_cast<std::ptrdiff_t>(first), looks.end(),
                  [](const LibraryLook& a, const LibraryLook& b) {
                      const std::string fa = lower(a.folder);
                      const std::string fb = lower(b.folder);
                      if (fa != fb) {
                          return fa < fb;
                      }
                      return lower(a.look.meta.name) < lower(b.look.meta.name);
                  });
    }
    return looks;
}

bool library_match(const LibraryLook& look, const std::string& query) {
    std::string haystack = look.look.meta.name + " " + look.folder;
    for (const std::string& tag : look.look.meta.tags) {
        haystack += " " + tag;
    }
    haystack = lower(haystack);
    std::istringstream words(lower(query));
    std::string word;
    while (words >> word) {
        if (haystack.find(word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::string user_data_dir() {
    const std::string override_dir = env("DYELINE_USER_DIR");
    if (!override_dir.empty()) {
        return override_dir;
    }
#if defined(_WIN32)
    const std::string appdata = env("APPDATA");
    return appdata.empty() ? std::string() : (fs::u8path(appdata) / "Dyeline").u8string();
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    return home.empty() ? std::string()
                        : (fs::u8path(home) / "Library" / "Application Support" / "Dyeline").u8string();
#else
    const std::string data = env("XDG_DATA_HOME");
    if (!data.empty()) {
        return (fs::u8path(data) / "dyeline").u8string();
    }
    const std::string home = env("HOME");
    return home.empty() ? std::string() : (fs::u8path(home) / ".local" / "share" / "dyeline").u8string();
#endif
}

std::string user_looks_dir() {
    const std::string dir = user_data_dir();
    return dir.empty() ? dir : (fs::u8path(dir) / "looks").u8string();
}

std::string favorites_path() {
    const std::string dir = user_data_dir();
    return dir.empty() ? dir : (fs::u8path(dir) / "favorites.json").u8string();
}

std::set<std::string> read_favorites(const std::string& path) {
    std::set<std::string> keys;
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) {
        return keys;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_object() && json.contains("favorites") && json["favorites"].is_array()) {
        for (const auto& key : json["favorites"]) {
            if (key.is_string()) {
                keys.insert(key.get<std::string>());
            }
        }
    }
    return keys;
}

bool write_favorites(const std::string& path, const std::set<std::string>& keys, std::string& error) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    nlohmann::json json = {{"favorites", nlohmann::json::array()}};
    for (const std::string& key : keys) {
        json["favorites"].push_back(key);
    }
    std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
    out << json.dump(2) << "\n";
    if (!out) {
        error = "could not write " + path;
        return false;
    }
    return true;
}

std::string look_file_name(const std::string& name) {
    std::string file;
    for (const char c : name) {
        const bool refused = static_cast<unsigned char>(c) < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos;
        file += refused ? '_' : c;
    }
    // Windows refuses names ending in a dot or space.
    while (!file.empty() && (file.back() == '.' || file.back() == ' ')) {
        file.pop_back();
    }
    if (file.empty()) {
        file = "Untitled";
    }
    return file + ".dyeline";
}

Look preset_for_editing(const LibraryLook& preset) {
    Look look = preset.look;
    const std::string dir = fs::u8path(preset.path).parent_path().u8string();
    for (ToolEntry& entry : look.chain) {
        const ToolSpec* tool = find_tool(entry.tool);
        if (tool == nullptr || tool->file_param == nullptr) {
            continue;
        }
        const auto it = entry.params.find(tool->file_param);
        if (it != entry.params.end() && it->is_string() && !it->get<std::string>().empty()) {
            *it = resolve_path(it->get<std::string>(), dir);
        }
    }
    return look;
}

}  // namespace dyeline
