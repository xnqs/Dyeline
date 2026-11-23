#include "cube_cli.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <system_error>

#include "dyeline/color.h"
#include "dyeline/cube.h"
#include "dyeline/cube_export.h"
#include "dyeline/look.h"

namespace dyeline::cli {

namespace {

namespace fs = std::filesystem;

const char* const kUsage =
    "usage: dyeline-cube [options] <look.dyeline | folder>...\n"
    "\n"
    "Bakes Dyeline looks into .cube 3D LUTs, as the Look Designer's Export .cube does. A folder\n"
    "bakes every .dyeline file in it and its subfolders. Each LUT is written beside its look,\n"
    "with the look file's name, unless --out says where.\n"
    "\n"
    "  --size N         points per axis, 2 to 129 (default 33)\n"
    "  --input SPACE    the footage's color space, instead of each look's own\n"
    "  --output SPACE   the color space the LUT gives (\"same\" for the input's)\n"
    "  --mode MODE      tone (convert the transfer curve only) or full (curve and gamut)\n"
    "  --out DIR        write the LUTs into DIR, keeping a folder's subfolders\n"
    "  --list-spaces    list the color space ids and stop\n"
    "  --help           show this and stop\n"
    "\n"
    "A LUT only maps colors to colors, so tools that depend on where a pixel is or on its\n"
    "neighbors (grain, vignettes, glows, lens tools) are left out; each LUT's file lists them.\n";

struct Options {
    int size = kDefaultCubeExportSize;
    std::string input;
    std::string output;
    std::string mode;
    std::string out_dir;
    std::vector<std::string> paths;
};

// One look to bake, and where its LUT goes.
struct Job {
    fs::path look;
    fs::path cube;
};

bool parse_size(const std::string& text, int& size) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0' || value < kMinCubeExportSize || value > kMaxCube3dSize) {
        return false;
    }
    size = static_cast<int>(value);
    return true;
}

bool is_look_file(const fs::path& path) {
    std::string ext = path.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return ext == ".dyeline";
}

// Where a look's LUT goes: beside it, or under --out, keeping its folder below `root` (the
// folder that was named on the command line, or empty for a look named itself).
fs::path cube_path(const fs::path& look, const fs::path& root, const Options& options) {
    fs::path name = look.filename();
    name.replace_extension(".cube");
    if (options.out_dir.empty()) {
        return look.parent_path() / name;
    }
    fs::path dir = fs::u8path(options.out_dir);
    if (!root.empty()) {
        dir /= look.parent_path().lexically_relative(root);
    }
    return (dir / name).lexically_normal();
}

}  // namespace

int run_cube(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--help" || arg == "-h") {
            out << kUsage;
            return 0;
        }
        if (arg == "--list-spaces") {
            for (const ColorSpace& space : all_color_spaces()) {
                out << space.id << "\t" << space.label << "\n";
            }
            return 0;
        }
        if ((arg == "--size" || arg == "--input" || arg == "--output" || arg == "--mode" ||
             arg == "--out") && !has_value) {
            err << "dyeline-cube: " << arg << " needs a value\n";
            return 2;
        }
        if (arg == "--size") {
            if (!parse_size(args[++i], options.size)) {
                err << "dyeline-cube: --size must be a whole number from " << kMinCubeExportSize
                    << " to " << kMaxCube3dSize << "\n";
                return 2;
            }
        } else if (arg == "--input") {
            options.input = args[++i];
            if (find_color_space(options.input) == nullptr) {
                err << "dyeline-cube: no color space \"" << options.input
                    << "\" (--list-spaces lists them)\n";
                return 2;
            }
        } else if (arg == "--output") {
            options.output = args[++i];
            if (options.output != "same" && find_color_space(options.output) == nullptr) {
                err << "dyeline-cube: no color space \"" << options.output
                    << "\" (--list-spaces lists them)\n";
                return 2;
            }
        } else if (arg == "--mode") {
            options.mode = args[++i];
            if (options.mode != "tone" && options.mode != "full") {
                err << "dyeline-cube: --mode must be tone or full\n";
                return 2;
            }
        } else if (arg == "--out") {
            options.out_dir = args[++i];
        } else if (arg.size() > 1 && arg[0] == '-') {
            err << "dyeline-cube: unknown option " << arg << "\n" << kUsage;
            return 2;
        } else {
            options.paths.push_back(arg);
        }
    }
    if (options.paths.empty()) {
        err << kUsage;
        return 2;
    }

    // Every look to bake, in the order given, folders sorted so runs are repeatable.
    bool failed = false;
    std::vector<Job> jobs;
    for (const std::string& arg : options.paths) {
        const fs::path path = fs::u8path(arg);
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            std::vector<fs::path> looks;
            for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_regular_file(ec) && is_look_file(it->path())) {
                    looks.push_back(it->path());
                }
            }
            if (ec) {
                err << "dyeline-cube: " << arg << ": " << ec.message() << "\n";
                failed = true;
            }
            std::sort(looks.begin(), looks.end());
            if (looks.empty() && !ec) {
                err << "dyeline-cube: " << arg << ": no .dyeline files in it\n";
                failed = true;
            }
            for (const fs::path& look : looks) {
                jobs.push_back({look, cube_path(look, path, options)});
            }
        } else if (fs::is_regular_file(path, ec)) {
            jobs.push_back({path, cube_path(path, fs::path(), options)});
        } else {
            err << "dyeline-cube: " << arg << ": no such file or folder\n";
            failed = true;
        }
    }

    std::set<fs::path> written;
    int baked = 0;
    for (const Job& job : jobs) {
        const std::string look_name = job.look.u8string();
        const std::string cube_name = job.cube.u8string();
        // Two looks with the same name would overwrite each other's LUT under --out.
        if (!written.insert(job.cube).second) {
            err << "dyeline-cube: " << look_name << ": another look was already written to "
                << cube_name << "\n";
            failed = true;
            continue;
        }
        Look look;
        std::string error;
        if (!read_look_file(look_name, look, error)) {
            err << "dyeline-cube: " << error << "\n";
            failed = true;
            continue;
        }
        if (look.meta.name.empty()) {
            look.meta.name = job.look.stem().u8string();
        }
        if (!options.input.empty()) {
            look.color.input = options.input;
        }
        if (!options.output.empty()) {
            look.color.output = options.output;
        }
        if (!options.mode.empty()) {
            look.color.mode = options.mode;
        }
        std::error_code ec;
        if (!job.cube.parent_path().empty()) {
            fs::create_directories(job.cube.parent_path(), ec);
        }
        CubeExport result;
        if (ec || !export_cube_file(look, options.size, job.look.parent_path().u8string(), cube_name,
                                    result, error)) {
            err << "dyeline-cube: " << look_name << ": "
                << (ec ? "could not make the folder for " + cube_name : error) << "\n";
            failed = true;
            continue;
        }
        ++baked;
        out << look_name << " -> " << cube_name << "\n";
        for (const std::string& tool : result.left_out) {
            out << "  left out: " << tool << "\n";
        }
        for (const std::string& note : result.notes) {
            out << "  note: " << note << "\n";
        }
    }
    if (jobs.size() > 1) {
        out << "Baked " << baked << " of " << jobs.size() << " looks.\n";
    }
    return failed ? 1 : 0;
}

}  // namespace dyeline::cli
