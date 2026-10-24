// Opens the built Dyeline.ofx the way an OFX host does and checks the entry points a host
// calls before anything else: the plugin count and each plugin's identity.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using GetNumberOfPluginsFn = int (*)();
using GetPluginFn = OfxPlugin* (*)(int);

class PluginLibrary {
public:
    explicit PluginLibrary(const char* path) {
#ifdef _WIN32
        handle_ = LoadLibraryA(path);
#else
        handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~PluginLibrary() {
#ifdef _WIN32
        if (handle_) FreeLibrary(static_cast<HMODULE>(handle_));
#else
        if (handle_) dlclose(handle_);
#endif
    }
    PluginLibrary(const PluginLibrary&) = delete;
    PluginLibrary& operator=(const PluginLibrary&) = delete;

    bool loaded() const { return handle_ != nullptr; }

    void* symbol(const char* name) const {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
        return dlsym(handle_, name);
#endif
    }

private:
    void* handle_ = nullptr;
};

TEST(PluginLoad, ExportsOneImageEffect) {
    PluginLibrary library(DYELINE_PLUGIN_PATH);
    ASSERT_TRUE(library.loaded()) << "could not load " << DYELINE_PLUGIN_PATH;

    auto count_fn = reinterpret_cast<GetNumberOfPluginsFn>(library.symbol("OfxGetNumberOfPlugins"));
    auto get_fn = reinterpret_cast<GetPluginFn>(library.symbol("OfxGetPlugin"));
    ASSERT_NE(count_fn, nullptr);
    ASSERT_NE(get_fn, nullptr);

    ASSERT_EQ(count_fn(), 1);
    const OfxPlugin* plugin = get_fn(0);
    ASSERT_NE(plugin, nullptr);
    EXPECT_STREQ(plugin->pluginApi, kOfxImageEffectPluginApi);
    EXPECT_EQ(plugin->apiVersion, 1);
    EXPECT_STREQ(plugin->pluginIdentifier, "io.github.xnqs.Dyeline");
    EXPECT_EQ(plugin->pluginVersionMajor, 0u);
    EXPECT_EQ(plugin->pluginVersionMinor, 1u);
    EXPECT_NE(plugin->setHost, nullptr);
    EXPECT_NE(plugin->mainEntry, nullptr);
}

TEST(PluginLoad, ExportsOnlyTheOfxEntryPoints) {
#ifdef _WIN32
    GTEST_SKIP() << "Windows exports only dllexport symbols by default";
#else
    PluginLibrary library(DYELINE_PLUGIN_PATH);
    ASSERT_TRUE(library.loaded());
    // OFX::throwSuiteStatusException(int) is linked in from the support library and must stay
    // private to the plugin.
    EXPECT_EQ(library.symbol("_ZN3OFX25throwSuiteStatusExceptionEi"), nullptr);
#endif
}

}  // namespace
