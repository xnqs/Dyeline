#pragma once

// The looks library: .dyeline files found in folders, the looks that ship with Dyeline and
// the user's own. Subfolders group looks (by mood, say); the browser shows them as folders.

#include <set>
#include <string>
#include <vector>

#include "dyeline/look.h"

namespace dyeline {

struct LibraryRoot {
    std::string path;
    bool bundled = false;  // shipped with Dyeline: read only
};

struct LibraryLook {
    std::string path;    // the .dyeline file
    std::string folder;  // its folder below the root, "/"-separated, or empty
    std::string key;     // stable across installs: "bundled/<folder>/<file>" or "user/..."
    bool bundled = false;
    Look look;
};

// Reads every .dyeline file below the roots, sorted by folder and then name. Roots that don't
// exist are skipped; files that fail to read are left out, with a line in `problems`.
std::vector<LibraryLook> scan_library(const std::vector<LibraryRoot>& roots,
                                      std::vector<std::string>& problems);

// True when every word of `query` is found, ignoring case, in the look's name, tags or folder.
bool library_match(const LibraryLook& look, const std::string& query);

// The folder for the user's own looks, and the file of their favorites:
//   Linux    $XDG_DATA_HOME/dyeline (else ~/.local/share/dyeline)
//   macOS    ~/Library/Application Support/Dyeline
//   Windows  %APPDATA%\Dyeline
// with looks/ and favorites.json inside. DYELINE_USER_DIR overrides the folder. Empty when no
// home folder is known.
std::string user_data_dir();
std::string user_looks_dir();
std::string favorites_path();

// Favorites, by LibraryLook::key. A missing file is no favorites.
std::set<std::string> read_favorites(const std::string& path);
bool write_favorites(const std::string& path, const std::set<std::string>& keys, std::string& error);

// A file name for a look called `name`: characters that file systems refuse become "_".
std::string look_file_name(const std::string& name);

// A preset ready to go into the look being edited: its file paths (LUTs) made absolute against
// the preset's folder, so they still resolve from wherever the look is saved.
Look preset_for_editing(const LibraryLook& preset);

}  // namespace dyeline
