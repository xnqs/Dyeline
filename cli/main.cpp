#include <iostream>
#include <string>
#include <vector>

#include "cube_cli.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
#ifdef _WIN32
    // argv is in the ANSI code page; paths are UTF-8 everywhere else in Dyeline.
    int count = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        args.clear();
        for (int i = 1; i < count; ++i) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(static_cast<std::size_t>(bytes > 0 ? bytes : 1), '\0');
            if (bytes > 0) {
                WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), bytes, nullptr, nullptr);
            }
            arg.pop_back();  // the terminator
            args.push_back(arg);
        }
        LocalFree(wide);
    }
#endif
    return dyeline::cli::run_cube(args, std::cout, std::cerr);
}
