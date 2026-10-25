#include "module_path.h"

#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace dyeline::plugin {

namespace {
void address_in_this_module() {}
}  // namespace

std::string module_directory() {
    std::string path;
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&address_in_this_module), &module)) {
        return {};
    }
    wchar_t wide[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(module, wide, static_cast<DWORD>(std::size(wide)));
    if (length == 0 || length >= std::size(wide)) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0,
                                         nullptr, nullptr);
    path.resize(static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), path.data(), size, nullptr,
                        nullptr);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&address_in_this_module), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    path = info.dli_fname;
#endif
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string designer_path() {
#ifdef _WIN32
    return module_directory() + "dyeline-designer.exe";
#else
    return module_directory() + "dyeline-designer";
#endif
}

}  // namespace dyeline::plugin
