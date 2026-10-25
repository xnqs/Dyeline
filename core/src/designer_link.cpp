#include "dyeline/designer_link.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dyeline::designer {

namespace {

namespace fs = std::filesystem;

// A path in the temp folder no other round trip uses, even from another host process.
fs::path unique_temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << "dyeline-" << pid << '-' << ticks << '-' << counter++ << suffix;
    return fs::temp_directory_path() / name.str();
}

bool write_file(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

bool read_file(const fs::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// Starts the program and waits for it. Returns its exit code, or -1 with `error` set.
#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

// Quotes one argument the way the Microsoft C runtime parses command lines.
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

int run_and_wait(const std::vector<std::string>& argv, std::string& error) {
    std::wstring command_line;
    for (const std::string& arg : argv) {
        if (!command_line.empty()) {
            command_line += L' ';
        }
        command_line += quote(widen(arg));
    }
    const std::wstring program = widen(argv.front());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        error = "could not start the designer (error " + std::to_string(GetLastError()) + ")";
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
int run_and_wait(const std::vector<std::string>& argv, std::string& error) {
    std::vector<char*> args;
    for (const std::string& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, args[0], nullptr, nullptr, args.data(), environ);
    if (spawned != 0) {
        error = "could not start the designer: " + std::generic_category().message(spawned);
        return -1;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error = "lost track of the designer process";
            return -1;
        }
    }
    if (!WIFEXITED(status)) {
        error = "the designer crashed";
        return -1;
    }
    return WEXITSTATUS(status);
}
#endif

// Finds `"key": <number>` in a flat JSON object. Numbers are read and written in the
// classic "C" locale, since the host may have set one that uses decimal commas.
bool find_number(const std::string& text, const char* key, double& value) {
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        return false;
    }
    at = text.find(':', at + needle.size());
    if (at == std::string::npos) {
        return false;
    }
    std::istringstream stream(text.substr(at + 1));
    stream.imbue(std::locale::classic());
    stream >> value;
    return !stream.fail();
}

}  // namespace

RoundTrip run(const std::string& designer_path, const std::string& look,
              const std::vector<std::string>& extra_args) {
    RoundTrip result;
    const auto started = std::chrono::steady_clock::now();
    const fs::path in_path = unique_temp_path("-in.json");
    const fs::path out_path = unique_temp_path("-out.json");

    if (!write_file(in_path, look)) {
        result.error = "could not write " + in_path.string();
        return result;
    }
    std::vector<std::string> argv = {designer_path, "--in", in_path.string(), "--out",
                                     out_path.string()};
    argv.insert(argv.end(), extra_args.begin(), extra_args.end());
    const int code = run_and_wait(argv, result.error);

    std::error_code ignored;
    if (code == kExitApplied) {
        if (read_file(out_path, result.look) && !result.look.empty()) {
            result.outcome = Outcome::Applied;
        } else {
            result.error = "the designer applied but wrote no look";
        }
    } else if (code == kExitCancelled) {
        result.outcome = Outcome::Cancelled;
    } else if (result.error.empty()) {
        result.error = "the designer exited with code " + std::to_string(code);
    }
    fs::remove(in_path, ignored);
    fs::remove(out_path, ignored);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

std::string format_look(const ExposureParams& params) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text.precision(17);
    text << "{\"version\": 1, \"exposure\": " << params.stops << ", \"strength\": "
         << params.strength << "}\n";
    return text.str();
}

bool parse_look(const std::string& look, ExposureParams& params) {
    double version = 0.0;
    ExposureParams parsed;
    if (!find_number(look, "version", version) || version != 1.0 ||
        !find_number(look, "exposure", parsed.stops) ||
        !find_number(look, "strength", parsed.strength)) {
        return false;
    }
    params = parsed;
    return true;
}

bool read_look_file(const std::string& path, ExposureParams& params, std::string& error) {
    std::string text;
    if (!read_file(fs::u8path(path), text)) {
        error = "could not open the look file " + path;
        return false;
    }
    if (!parse_look(text, params)) {
        error = "the look file " + path + " is not a Dyeline look";
        return false;
    }
    return true;
}

bool write_look_file(const std::string& path, const ExposureParams& params, std::string& error) {
    // Write next to the file, then rename over it, so a render never reads half a look.
    const fs::path target = fs::u8path(path);
    fs::path partial = target;
    partial += ".partial";
    std::error_code ec;
    if (!write_file(partial, format_look(params))) {
        error = "could not write " + partial.string();
        return false;
    }
    fs::rename(partial, target, ec);
    if (ec) {
        fs::remove(partial, ec);
        error = "could not replace the look file " + path;
        return false;
    }
    return true;
}

}  // namespace dyeline::designer
