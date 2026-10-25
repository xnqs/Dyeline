// Dyeline OFX plugin entry point.
//
// One effect that renders a look, a chain of tools, on the CPU or on the GPU the host hands
// over (CUDA, Metal or OpenCL). The look is edited in the Look Designer app: through the
// Edit Look round trip in hosts that take values back from a plugin (Natron, Resolve), or
// through a look file the designer saves (EffectCraft). This file stays a thin bridge between
// OFX and the render core in core/.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/designer_link.h"
#include "dyeline/gpu.h"
#include "dyeline/look.h"
#include "module_path.h"
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"

namespace {

// The plugin identifier is saved in host projects, so it must never change.
constexpr const char* kPluginIdentifier = "io.github.xnqs.Dyeline";
constexpr unsigned int kPluginVersionMajor = 0;
constexpr unsigned int kPluginVersionMinor = 1;

constexpr const char* kParamStrength = "strength";

// Look Designer link. Hosts that take parameter values back from a plugin (Natron, Resolve)
// use the Edit Look round trip, and the look is stored in a hidden string so it saves with the
// project. Hosts that don't (EffectCraft) point Look File at a .dyeline file the designer
// saves; while it is set, the file's look is rendered instead.
constexpr const char* kParamEditLook = "editLook";
constexpr const char* kParamLookFile = "lookFile";
constexpr const char* kParamLookFileReload = "lookFileReload";
constexpr const char* kParamLook = "look";
constexpr const char* kParamLookRevision = "lookRevision";
constexpr const char* kParamDesignerStatus = "designerStatus";

// Expands a leading "~" to the home folder, since a path typed into a host's text field
// doesn't go through a shell.
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

// A parsed look, or why it couldn't be read.
struct LoadedLook {
    bool ok = true;
    std::string error;
    dyeline::Look look;
};

// The last look parsed from the hidden parameter and from a look file, so a render only
// parses JSON when the look changed. A look file is reloaded when its size or time changes.
// Shared by render threads, so it is locked.
class LookCache {
public:
    LoadedLook from_text(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!text_valid_ || text != text_) {
            text_ = text;
            text_valid_ = true;
            text_look_ = LoadedLook{};
            if (!text.empty()) {
                text_look_.ok = dyeline::parse_look(text, text_look_.look, text_look_.error);
            }
        }
        return text_look_;
    }

    LoadedLook from_file(const std::string& path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path file = fs::u8path(path);
        const auto time = fs::last_write_time(file, ec);
        const auto size = ec ? 0 : fs::file_size(file, ec);
        std::lock_guard<std::mutex> lock(mutex_);
        if (ec || path != path_ || time != time_ || size != size_) {
            path_ = path;
            time_ = time;
            size_ = size;
            file_look_ = LoadedLook{};
            if (ec) {
                file_look_.ok = false;
                file_look_.error = "could not open the look file " + path;
            } else {
                file_look_.ok = dyeline::read_look_file(path, file_look_.look, file_look_.error);
            }
        }
        return file_look_;
    }

private:
    std::mutex mutex_;
    std::string text_;
    bool text_valid_ = false;
    LoadedLook text_look_;
    std::string path_;
    std::filesystem::file_time_type time_{};
    std::uintmax_t size_ = 0;
    LoadedLook file_look_;
};

class ChainProcessor : public OFX::ImageProcessor {
public:
    ChainProcessor(OFX::ImageEffect& effect, const OFX::Image& src, int channels,
                   bool premultiplied, const dl_chain& chain)
        : OFX::ImageProcessor(effect),
          src_(src),
          channels_(channels),
          premultiplied_(premultiplied),
          chain_(chain) {}

    void multiThreadProcessImages(OfxRectI window) override {
        const auto width = static_cast<std::size_t>(window.x2 - window.x1);
        for (int y = window.y1; y < window.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            auto* dst = static_cast<float*>(_dstImg->getPixelAddress(window.x1, y));
            const auto* src = static_cast<const float*>(src_.getPixelAddress(window.x1, y));
            if (dst == nullptr || src == nullptr) {
                continue;
            }
            dyeline::render_chain_row(src, dst, width, channels_, premultiplied_, chain_);
        }
    }

private:
    const OFX::Image& src_;
    int channels_;
    bool premultiplied_;
    const dl_chain& chain_;
};

int channel_count(OFX::PixelComponentEnum components) {
    switch (components) {
        case OFX::ePixelComponentRGBA:
            return 4;
        case OFX::ePixelComponentRGB:
            return 3;
        case OFX::ePixelComponentAlpha:
            return 1;
        default:
            return 0;
    }
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

class DyelineEffect : public OFX::ImageEffect {
public:
    explicit DyelineEffect(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle),
          dst_clip_(fetchClip(kOfxImageEffectOutputClipName)),
          src_clip_(fetchClip(kOfxImageEffectSimpleSourceClipName)),
          strength_(fetchDoubleParam(kParamStrength)),
          look_file_(fetchStringParam(kParamLookFile)),
          look_(fetchStringParam(kParamLook)),
          look_revision_(fetchIntParam(kParamLookRevision)),
          designer_status_(fetchStringParam(kParamDesignerStatus)) {}

    void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
        if (name == kParamEditLook) {
            edit_look(args.time);
        }
    }

    void render(const OFX::RenderArguments& args) override {
        std::unique_ptr<OFX::Image> dst(dst_clip_->fetchImage(args.time));
        std::unique_ptr<OFX::Image> src(src_clip_->fetchImage(args.time));
        if (!dst || !src) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (dst->getPixelDepth() != OFX::eBitDepthFloat ||
            src->getPixelDepth() != OFX::eBitDepthFloat ||
            dst->getPixelComponents() != src->getPixelComponents()) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        const int channels = channel_count(dst->getPixelComponents());
        if (channels == 0) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        const bool premultiplied = src->getPreMultiplication() == OFX::eImagePreMultiplied;

        const dyeline::CompiledChain chain = chain_at(args.time, /*report=*/true);
        if (args.isEnabledCudaRender || args.isEnabledMetalRender || args.isEnabledOpenCLRender) {
            render_gpu(args, *src, *dst, channels, premultiplied, chain.pass);
            return;
        }

        ChainProcessor processor(*this, *src, channels, premultiplied, chain.pass);
        processor.setDstImg(dst.get());
        processor.setRenderWindow(args.renderWindow);
        processor.process();
    }

    bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identity_clip,
                    double& identity_time) override {
        dyeline::CompiledChain chain;
        try {
            chain = chain_at(args.time, /*report=*/false);
        } catch (...) {
            return false;  // render reports the error
        }
        if (chain.is_identity()) {
            identity_clip = src_clip_;
            identity_time = args.time;
            return true;
        }
        return false;
    }

private:
    // The look to render at `time`: the look file's when one is set, else the saved one.
    LoadedLook look_at(double time) {
        std::string file;
        look_file_->getValueAtTime(time, file);
        if (!file.empty()) {
            return cache_.from_file(expand_home(file));
        }
        std::string text;
        look_->getValueAtTime(time, text);
        return cache_.from_text(text);
    }

    // The compiled pass for `time`. A look that can't be read or run fails the render with
    // the reason, rather than silently rendering something else; tools this build doesn't
    // have are bypassed with a warning. With `report` off nothing is shown to the user.
    dyeline::CompiledChain chain_at(double time, bool report) {
        const LoadedLook loaded = look_at(time);
        dyeline::CompiledChain chain;
        std::string error = loaded.error;
        if (loaded.ok) {
            dyeline::compile_chain(loaded.look, strength_->getValueAtTime(time), chain, error);
        }
        if (!error.empty()) {
            if (report) {
                show_message(OFX::Message::eMessageError, "Dyeline: " + error);
            }
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (report) {
            if (!chain.missing_tools.empty()) {
                show_message(OFX::Message::eMessageWarning,
                             "Dyeline: this look uses tools this version doesn't have, so they "
                             "are bypassed: " + join(chain.missing_tools));
            } else {
                show_message(OFX::Message::eMessageMessage, "");
            }
        }
        return chain;
    }

    // Shows a persistent message, or clears it when `text` is empty. Hosts are only called
    // when the message changes, since renders run often and on several threads.
    void show_message(OFX::Message::MessageTypeEnum type, const std::string& text) {
        std::lock_guard<std::mutex> lock(message_mutex_);
        if (text == message_) {
            return;
        }
        message_ = text;
        if (text.empty()) {
            clearPersistentMessage();
        } else {
            setPersistentMessage(type, "", text);
        }
    }

    // Hands the current look to the designer app and blocks until the user applies or
    // cancels, the way Looks' own editor behaves. Setting parameters is allowed here because
    // this runs inside the host's instance-changed action. With a look file set, the designer
    // edits the file instead, and bumping the hidden revision makes the host render again.
    void edit_look(double time) {
        std::string file;
        look_file_->getValue(file);
        std::string text;
        look_->getValueAtTime(time, text);
        std::vector<std::string> args;
        if (!file.empty()) {
            args = {"--file", expand_home(file)};
        }
        const dyeline::designer::RoundTrip trip =
            dyeline::designer::run(dyeline::plugin::designer_path(),
                                   text.empty() ? dyeline::write_look(dyeline::Look{}) : text, args);
        char status[256];
        switch (trip.outcome) {
            case dyeline::designer::Outcome::Applied: {
                if (!file.empty()) {
                    look_revision_->setValue(look_revision_->getValue() + 1);
                    std::snprintf(status, sizeof(status), "Look file saved after %.1f s",
                                  trip.seconds);
                    break;
                }
                dyeline::Look edited;
                std::string error;
                if (!dyeline::parse_look(trip.look, edited, error)) {
                    std::snprintf(status, sizeof(status), "Designer returned a bad look: %s",
                                  error.c_str());
                    break;
                }
                beginEditBlock("Edit Look");
                look_->setValue(trip.look);
                endEditBlock();
                std::snprintf(status, sizeof(status), "Look applied after %.1f s", trip.seconds);
                break;
            }
            case dyeline::designer::Outcome::Cancelled:
                std::snprintf(status, sizeof(status), "Designer closed without changes after %.1f s",
                              trip.seconds);
                break;
            case dyeline::designer::Outcome::Failed:
                std::snprintf(status, sizeof(status), "Designer failed: %s", trip.error.c_str());
                break;
        }
        designer_status_->setValue(status);
    }

    // On a GPU render the images' pixel data are device buffers on the host's GPU: CUDA
    // device pointers, MTLBuffers for Metal, or cl_mem buffers for OpenCL.
    void render_gpu(const OFX::RenderArguments& args, const OFX::Image& src, OFX::Image& dst,
                    int channels, bool premultiplied, const dl_chain& chain) {
        const OfxRectI& window = args.renderWindow;
        const OfxRectI& src_bounds = src.getBounds();
        const OfxRectI& dst_bounds = dst.getBounds();

        dl_image_args image{};
        image.width = window.x2 - window.x1;
        image.height = window.y2 - window.y1;
        image.channels = channels;
        image.premultiplied = premultiplied ? 1 : 0;
        image.src_row_floats = src.getRowBytes() / static_cast<int>(sizeof(float));
        image.dst_row_floats = dst.getRowBytes() / static_cast<int>(sizeof(float));
        image.src_x0 = window.x1 - src_bounds.x1;
        image.src_y0 = window.y1 - src_bounds.y1;
        image.dst_x0 = window.x1 - dst_bounds.x1;
        image.dst_y0 = window.y1 - dst_bounds.y1;

        std::string error;
        if (args.isEnabledCudaRender) {
            error = dyeline::cuda::render_chain(args.pCudaStream,
                                                static_cast<const float*>(src.getPixelData()),
                                                static_cast<float*>(dst.getPixelData()), image, chain);
        } else if (args.isEnabledOpenCLRender) {
            error = dyeline::opencl::render_chain(args.pOpenCLCmdQ, src.getPixelData(),
                                                  dst.getPixelData(), image, chain);
        } else {
            error = dyeline::metal::render_chain(args.pMetalCmdQ, src.getPixelData(),
                                                 dst.getPixelData(), image, chain);
        }
        if (!error.empty()) {
            show_message(OFX::Message::eMessageError, "Dyeline: " + error);
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    OFX::Clip* dst_clip_;
    OFX::Clip* src_clip_;
    OFX::DoubleParam* strength_;
    OFX::StringParam* look_file_;
    OFX::StringParam* look_;
    OFX::IntParam* look_revision_;
    OFX::StringParam* designer_status_;
    LookCache cache_;
    std::mutex message_mutex_;
    std::string message_;
};

}  // namespace

mDeclarePluginFactory(DyelineFactory, {}, {});

void DyelineFactory::describe(OFX::ImageEffectDescriptor& desc) {
    desc.setLabels("Dyeline", "Dyeline", "Dyeline");
    desc.setPluginGrouping("Dyeline");
    desc.setPluginDescription(
        "Builds film-style looks from a chain of color, lens and film tools, edited in the "
        "Dyeline Look Designer.");

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);

    // Offer the GPU paths this build has. Hosts without GPU support ignore these.
    if (dyeline::cuda::available()) {
        desc.setSupportsCudaRender(true);
        desc.setSupportsCudaStream(true);
    }
    if (dyeline::metal::available()) {
        desc.setSupportsMetalRender(true);
    }
    if (dyeline::opencl::available()) {
        desc.setSupportsOpenCLBuffersRender(true);
    }
}

void DyelineFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                       OFX::ContextEnum /*context*/) {
    OFX::ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(OFX::ePixelComponentRGBA);
    src->addSupportedComponent(OFX::ePixelComponentRGB);
    src->addSupportedComponent(OFX::ePixelComponentAlpha);
    src->setTemporalClipAccess(false);
    src->setSupportsTiles(true);
    src->setIsMask(false);

    OFX::ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(OFX::ePixelComponentRGBA);
    dst->addSupportedComponent(OFX::ePixelComponentRGB);
    dst->addSupportedComponent(OFX::ePixelComponentAlpha);
    dst->setSupportsTiles(true);

    OFX::PageParamDescriptor* page = desc.definePageParam("Controls");

    OFX::DoubleParamDescriptor* strength = desc.defineDoubleParam(kParamStrength);
    strength->setLabels("Strength", "Strength", "Strength");
    strength->setHint("How much of the look to apply. 0 leaves the image untouched.");
    strength->setDoubleType(OFX::eDoubleTypeScale);
    strength->setDefault(1.0);
    strength->setRange(0.0, 1.0);
    strength->setDisplayRange(0.0, 1.0);
    strength->setIncrement(0.01);
    strength->setDigits(2);
    strength->setAnimates(true);
    page->addChild(*strength);

    OFX::PushButtonParamDescriptor* edit_look = desc.definePushButtonParam(kParamEditLook);
    edit_look->setLabels("Edit Look...", "Edit Look...", "Edit Look...");
    edit_look->setHint("Opens the Look Designer. The host waits until you apply or close it.");
    page->addChild(*edit_look);

    OFX::StringParamDescriptor* look_file = desc.defineStringParam(kParamLookFile);
    look_file->setLabels("Look File", "Look File", "Look File");
    look_file->setHint("A .dyeline look saved by the Look Designer. While set, it is rendered "
                       "instead of the look saved with the project, and re-read whenever the "
                       "file changes. Use it in hosts where Edit Look does nothing.");
    look_file->setStringType(OFX::eStringTypeFilePath);
    look_file->setFilePathExists(false);
    look_file->setAnimates(false);
    page->addChild(*look_file);

    // Hosts that cache frames by parameter values alone (EffectCraft) don't notice a changed
    // look file. Any new value here makes them render again, and the plugin re-reads the file.
    OFX::IntParamDescriptor* reload = desc.defineIntParam(kParamLookFileReload);
    reload->setLabels("Reload Look File", "Reload Look File", "Reload Look File");
    reload->setHint("Add one after saving the look file if the host still shows the old look. "
                    "Some hosts, such as EffectCraft, only render again when a value changes.");
    reload->setDefault(0);
    reload->setRange(0, 1000000);
    reload->setDisplayRange(0, 100);
    reload->setAnimates(false);
    page->addChild(*reload);

    OFX::StringParamDescriptor* status = desc.defineStringParam(kParamDesignerStatus);
    status->setLabels("Designer", "Designer", "Designer");
    status->setStringType(OFX::eStringTypeLabel);
    status->setDefault("Not opened yet");
    status->setAnimates(false);
    status->setEvaluateOnChange(false);
    status->setIsPersistant(false);
    page->addChild(*status);

    OFX::StringParamDescriptor* look = desc.defineStringParam(kParamLook);
    look->setLabels("Look", "Look", "Look");
    look->setIsSecret(true);
    look->setAnimates(false);

    // Bumped after the designer saves the look file, so the host renders again.
    OFX::IntParamDescriptor* revision = desc.defineIntParam(kParamLookRevision);
    revision->setLabels("Look Revision", "Look Revision", "Look Revision");
    revision->setIsSecret(true);
    revision->setDefault(0);
    revision->setAnimates(false);
}

OFX::ImageEffect* DyelineFactory::createInstance(OfxImageEffectHandle handle,
                                                 OFX::ContextEnum /*context*/) {
    return new DyelineEffect(handle);
}

namespace OFX::Plugin {

void getPluginIDs(OFX::PluginFactoryArray& ids) {
    static DyelineFactory factory(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&factory);
}

}  // namespace OFX::Plugin
