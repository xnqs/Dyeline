// Dyeline OFX plugin entry point.
//
// Milestone 0: one effect with a single Exposure tool, rendered on the CPU or on the GPU the
// host hands over (CUDA, Metal or OpenCL), plus a spike of the Look Designer round trip. The
// tool chain and the real designer arrive in later milestones; this file stays a thin bridge
// between OFX and the render core in core/.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "dyeline/designer_link.h"
#include "dyeline/exposure.h"
#include "dyeline/gpu.h"
#include "module_path.h"
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"

namespace {

// The plugin identifier is saved in host projects, so it must never change.
constexpr const char* kPluginIdentifier = "io.github.xnqs.Dyeline";
constexpr unsigned int kPluginVersionMajor = 0;
constexpr unsigned int kPluginVersionMinor = 1;

constexpr const char* kParamExposure = "exposure";
constexpr const char* kParamStrength = "strength";

// Look Designer link. Hosts that take parameter values back from a plugin (Natron, Resolve)
// use the Edit Look round trip, and the look is stored in a hidden string so it saves with the
// project. Hosts that don't (EffectCraft) point Look File at a .dyeline file the designer
// saves; while it is set, the file's look replaces the Exposure and Strength sliders.
constexpr const char* kParamEditLook = "editLook";
constexpr const char* kParamLookFile = "lookFile";
constexpr const char* kParamLook = "look";
constexpr const char* kParamLookRevision = "lookRevision";
constexpr const char* kParamDesignerStatus = "designerStatus";

// The last look read from a look file, reloaded when the file's size or time changes. Shared
// by render threads, so it is locked.
class LookFileCache {
public:
    // The look in `path`, or false with `error` set.
    bool get(const std::string& path, dyeline::ExposureParams& params, std::string& error) {
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
            ok_ = !ec && dyeline::designer::read_look_file(path, params_, error_);
            if (ec) {
                error_ = "could not open the look file " + path;
            }
        }
        params = params_;
        error = error_;
        return ok_;
    }

private:
    std::mutex mutex_;
    std::string path_;
    std::filesystem::file_time_type time_{};
    std::uintmax_t size_ = 0;
    bool ok_ = false;
    dyeline::ExposureParams params_;
    std::string error_;
};

class ExposureProcessor : public OFX::ImageProcessor {
public:
    ExposureProcessor(OFX::ImageEffect& effect, const OFX::Image& src, int channels,
                      const dyeline::ExposureParams& params)
        : OFX::ImageProcessor(effect), src_(src), channels_(channels), params_(params) {}

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
            dyeline::apply_exposure_row(src, dst, width, channels_, params_);
        }
    }

private:
    const OFX::Image& src_;
    int channels_;
    dyeline::ExposureParams params_;
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

class DyelineEffect : public OFX::ImageEffect {
public:
    explicit DyelineEffect(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle),
          dst_clip_(fetchClip(kOfxImageEffectOutputClipName)),
          src_clip_(fetchClip(kOfxImageEffectSimpleSourceClipName)),
          exposure_(fetchDoubleParam(kParamExposure)),
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

        const dyeline::ExposureParams params = look_at(args.time);
        if (args.isEnabledCudaRender || args.isEnabledMetalRender || args.isEnabledOpenCLRender) {
            render_gpu(args, *src, *dst, channels, params);
            return;
        }

        ExposureProcessor processor(*this, *src, channels, params);
        processor.setDstImg(dst.get());
        processor.setRenderWindow(args.renderWindow);
        processor.process();
    }

    bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identity_clip,
                    double& identity_time) override {
        dyeline::ExposureParams params;
        try {
            params = look_at(args.time);
        } catch (...) {
            return false;  // render reports the look file error
        }
        if (dyeline::exposure_is_identity(params)) {
            identity_clip = src_clip_;
            identity_time = args.time;
            return true;
        }
        return false;
    }

private:
    // Hands the current look to the designer app and blocks until the user applies or
    // cancels, the way Looks' own editor behaves. Setting parameters is allowed here because
    // this runs inside the host's instance-changed action. With a look file set, the designer
    // edits the file instead, and bumping the hidden revision makes the host render again.
    void edit_look(double time) {
        std::string file;
        look_file_->getValue(file);
        std::vector<std::string> args;
        if (!file.empty()) {
            args = {"--file", file};
        }
        const dyeline::designer::RoundTrip trip =
            dyeline::designer::run(dyeline::plugin::designer_path(),
                                   dyeline::designer::format_look(params_at(time)), args);
        char status[256];
        switch (trip.outcome) {
            case dyeline::designer::Outcome::Applied: {
                if (!file.empty()) {
                    look_revision_->setValue(look_revision_->getValue() + 1);
                    std::snprintf(status, sizeof(status), "Look file saved after %.1f s",
                                  trip.seconds);
                    break;
                }
                dyeline::ExposureParams edited;
                if (!dyeline::designer::parse_look(trip.look, edited)) {
                    std::snprintf(status, sizeof(status), "Designer returned an unreadable look");
                    break;
                }
                beginEditBlock("Edit Look");
                exposure_->setValue(edited.stops);
                strength_->setValue(edited.strength);
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

    // The look to render: the look file's when one is set, otherwise the sliders'. A look
    // file that can't be read fails the render with the reason, rather than silently
    // rendering a different look.
    dyeline::ExposureParams look_at(double time) {
        std::string file;
        look_file_->getValueAtTime(time, file);
        if (file.empty()) {
            return params_at(time);
        }
        dyeline::ExposureParams params;
        std::string error;
        if (!look_file_cache_.get(file, params, error)) {
            showing_look_error_ = true;
            setPersistentMessage(OFX::Message::eMessageError, "", error);
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (showing_look_error_.exchange(false)) {
            clearPersistentMessage();
        }
        return params;
    }

    // On a GPU render the images' pixel data are device buffers on the host's GPU: CUDA
    // device pointers, MTLBuffers for Metal, or cl_mem buffers for OpenCL.
    void render_gpu(const OFX::RenderArguments& args, const OFX::Image& src, OFX::Image& dst,
                    int channels, const dyeline::ExposureParams& params) {
        const OfxRectI& window = args.renderWindow;
        const OfxRectI& src_bounds = src.getBounds();
        const OfxRectI& dst_bounds = dst.getBounds();

        dl_exposure_args kernel_args{};
        kernel_args.width = window.x2 - window.x1;
        kernel_args.height = window.y2 - window.y1;
        kernel_args.channels = channels;
        kernel_args.src_row_floats = src.getRowBytes() / static_cast<int>(sizeof(float));
        kernel_args.dst_row_floats = dst.getRowBytes() / static_cast<int>(sizeof(float));
        kernel_args.src_x0 = window.x1 - src_bounds.x1;
        kernel_args.src_y0 = window.y1 - src_bounds.y1;
        kernel_args.dst_x0 = window.x1 - dst_bounds.x1;
        kernel_args.dst_y0 = window.y1 - dst_bounds.y1;
        kernel_args.gain = dyeline::exposure_gain(params);

        std::string error;
        if (args.isEnabledCudaRender) {
            error = dyeline::cuda::exposure(args.pCudaStream,
                                            static_cast<const float*>(src.getPixelData()),
                                            static_cast<float*>(dst.getPixelData()), kernel_args);
        } else if (args.isEnabledOpenCLRender) {
            error = dyeline::opencl::exposure(args.pOpenCLCmdQ, src.getPixelData(),
                                              dst.getPixelData(), kernel_args);
        } else {
            error = dyeline::metal::exposure(args.pMetalCmdQ, src.getPixelData(),
                                             dst.getPixelData(), kernel_args);
        }
        if (!error.empty()) {
            setPersistentMessage(OFX::Message::eMessageError, "", error);
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    dyeline::ExposureParams params_at(double time) const {
        dyeline::ExposureParams params;
        params.stops = exposure_->getValueAtTime(time);
        params.strength = strength_->getValueAtTime(time);
        return params;
    }

    OFX::Clip* dst_clip_;
    OFX::Clip* src_clip_;
    OFX::DoubleParam* exposure_;
    OFX::DoubleParam* strength_;
    OFX::StringParam* look_file_;
    OFX::StringParam* look_;
    OFX::IntParam* look_revision_;
    OFX::StringParam* designer_status_;
    LookFileCache look_file_cache_;
    std::atomic<bool> showing_look_error_{false};
};

}  // namespace

mDeclarePluginFactory(DyelineFactory, {}, {});

void DyelineFactory::describe(OFX::ImageEffectDescriptor& desc) {
    desc.setLabels("Dyeline", "Dyeline", "Dyeline");
    desc.setPluginGrouping("Dyeline");
    desc.setPluginDescription(
        "Builds film-style looks from a chain of color, lens and film tools. "
        "Milestone 0 build: Exposure only.");

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

    OFX::DoubleParamDescriptor* exposure = desc.defineDoubleParam(kParamExposure);
    exposure->setLabels("Exposure", "Exposure", "Exposure");
    exposure->setHint("Exposure change in stops. +1 doubles the light, -1 halves it.");
    exposure->setDefault(0.0);
    exposure->setRange(dyeline::kExposureMinStops, dyeline::kExposureMaxStops);
    exposure->setDisplayRange(-4.0, 4.0);
    exposure->setIncrement(0.1);
    exposure->setDigits(2);
    exposure->setAnimates(true);
    page->addChild(*exposure);

    OFX::DoubleParamDescriptor* strength = desc.defineDoubleParam(kParamStrength);
    strength->setLabels("Strength", "Strength", "Strength");
    strength->setHint("How much of the effect to apply. 0 leaves the image untouched.");
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
    look_file->setHint("A .dyeline look saved by the Look Designer. While set, it replaces the "
                       "Exposure and Strength sliders and is re-read whenever the file changes. "
                       "Use it in hosts where Edit Look does nothing.");
    look_file->setStringType(OFX::eStringTypeFilePath);
    look_file->setFilePathExists(false);
    look_file->setAnimates(false);
    page->addChild(*look_file);

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
    look->setEvaluateOnChange(false);

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
