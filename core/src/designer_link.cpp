#include "dyeline/designer_link.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>

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

namespace fs = std::filesystem;

std::string temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << "dyeline-" << pid << '-' << ticks << '-' << counter++ << suffix;
    return (fs::temp_directory_path() / name.str()).u8string();
}

namespace {

fs::path unique_temp_path(const char* suffix) {
    return fs::u8path(temp_path(suffix));
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

// Starts the program. Returns its process handle, or null with `error` set.
HANDLE spawn(const std::vector<std::string>& argv, std::string& error) {
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
        return nullptr;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

int wait_for(HANDLE process, std::string& /*error*/) {
    WaitForSingleObject(process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return static_cast<int>(code);
}

using Process = HANDLE;
const Process kNoProcess = nullptr;
#else
using Process = pid_t;
const Process kNoProcess = -1;

// Starts the program. Returns its process id, or -1 with `error` set.
pid_t spawn(const std::vector<std::string>& argv, std::string& error) {
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
    return pid;
}

int wait_for(pid_t pid, std::string& error) {
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

// Starts the program and waits for it. Returns its exit code, or -1 with `error` set.
int run_and_wait(const std::vector<std::string>& argv, std::string& error) {
    const Process process = spawn(argv, error);
    return process == kNoProcess ? -1 : wait_for(process, error);
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

bool start(const std::string& designer_path, const std::vector<std::string>& args,
           std::function<void(int)> on_exit, std::string& error) {
    std::vector<std::string> argv = {designer_path};
    argv.insert(argv.end(), args.begin(), args.end());
    const Process process = spawn(argv, error);
    if (process == kNoProcess) {
        return false;
    }
    // Waiting also reaps the process, so it never lingers as a zombie in the host.
    std::thread([process, on_exit = std::move(on_exit)] {
        std::string ignored;
        const int code = wait_for(process, ignored);
        if (on_exit) {
            on_exit(code);
        }
    }).detach();
    return true;
}

}  // namespace dyeline::designer
