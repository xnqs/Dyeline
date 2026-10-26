// Dyeline OFX plugin entry point.
//
// One effect that renders a look, a chain of tools, on the CPU or on the GPU the host hands
// over (CUDA, Metal or OpenCL), or on Dyeline's own GPU when the host renders on the CPU. The
// look is edited in the Look Designer app: through the Edit Look round trip in hosts that take
// values back from a plugin (Natron, Resolve), or through a look file the designer saves,
// opened with Open Designer (EffectCraft). This file stays a thin bridge between OFX and the
// render core in core/.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/designer_link.h"
#include "dyeline/effectcraft.h"
#include "dyeline/frame_file.h"
#include "dyeline/gpu.h"
#include "dyeline/look.h"
#include "dyeline/paths.h"
#include "dyeline/render.h"
#include "module_path.h"
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

namespace {

// The plugin identifier is saved in host projects, so it must never change.
constexpr const char* kPluginIdentifier = "io.github.xnqs.Dyeline";
constexpr unsigned int kPluginVersionMajor = DYELINE_VERSION_MAJOR;
constexpr unsigned int kPluginVersionMinor = DYELINE_VERSION_MINOR;

constexpr const char* kParamStrength = "strength";
constexpr const char* kParamOwnGpu = "ownGpu";

// Look Designer link. Hosts that take parameter values back from a plugin (Natron, Resolve)
// use the Edit Look round trip, and the look is stored in a hidden string so it saves with the
// project. Hosts that don't (EffectCraft) point Look File at a .dyeline file the designer
// saves; while it is set, the file's look is rendered instead.
constexpr const char* kParamEditLook = "editLook";
constexpr const char* kParamOpenDesigner = "openDesigner";
constexpr const char* kParamLookFile = "lookFile";
constexpr const char* kParamLookFileReload = "lookFileReload";
constexpr const char* kParamLook = "look";
constexpr const char* kParamLookRevision = "lookRevision";
constexpr const char* kParamDesignerStatus = "designerStatus";

// The largest frame handed to the designer for its preview; bigger frames are shrunk to fit.
constexpr int kPreviewWidth = 1920;
constexpr int kPreviewHeight = 1080;

// A parsed look, or why it couldn't be read.
struct LoadedLook {
    bool ok = true;
    std::string error;
    dyeline::Look look;
    // The folder a LUT path in the look is relative to: the look file's, or empty.
    std::string base_dir;
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

// Designers started by Open Designer, by look file, shared by every instance in the process.
// EffectCraft keeps several instances of one effect and tells each of them about a changed
// value, so a look file gets one designer per new Open Designer value, and none while one is
// still open on it.
class DesignerLaunches {
public:
    // True when a designer should be started for `path` at counter `value`; it is then
    // counted as open until finished() is called.
    bool claim(const std::string& path, int value) {
        std::lock_guard<std::mutex> lock(mutex_);
        Launch& launch = launches_[path];
        if (launch.open || launch.value == value) {
            return false;
        }
        launch.open = true;
        launch.value = value;
        return true;
    }

    void finished(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        launches_[path].open = false;
    }

private:
    struct Launch {
        bool open = false;
        int value = -1;
    };
    std::mutex mutex_;
    std::map<std::string, Launch> launches_;
};

DesignerLaunches& designer_launches() {
    static DesignerLaunches launches;
    return launches;
}

// Whether Dyeline's own GPU still renders in this process. It is switched off for good after
// it fails once, so a broken driver costs one failed frame, not every frame.
std::atomic<bool> own_gpu_works{true};

class DyelineEffect : public OFX::ImageEffect {
public:
    explicit DyelineEffect(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle),
          dst_clip_(fetchClip(kOfxImageEffectOutputClipName)),
          src_clip_(fetchClip(kOfxImageEffectSimpleSourceClipName)),
          strength_(fetchDoubleParam(kParamStrength)),
          own_gpu_(fetchBooleanParam(kParamOwnGpu)),
          open_designer_(fetchIntParam(kParamOpenDesigner)),
          look_file_(fetchStringParam(kParamLookFile)),
          look_(fetchStringParam(kParamLook)),
          look_revision_(fetchIntParam(kParamLookRevision)),
          designer_status_(fetchStringParam(kParamDesignerStatus)) {
        // The value this instance was created with is not a request to open the designer.
        open_designer_->getValue(open_designer_seen_);
    }

    void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
        if (name == kParamEditLook) {
            edit_look(args.time, args.renderScale);
        } else if (name == kParamOpenDesigner && args.reason == OFX::eChangeUserEdit &&
                   take_open_designer_request()) {
            open_designer(args.time, args.renderScale, /*with_frame=*/true);
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
        dyeline::RenderGeometry geometry =
            frame_geometry(args.time, args.renderScale, src->getPixelAspectRatio());
        geometry.window = to_rect(args.renderWindow);
        geometry.src_bounds = to_rect(src->getBounds());
        geometry.dst_bounds = to_rect(dst->getBounds());
        geometry.src_row_floats = src->getRowBytes() / static_cast<int>(sizeof(float));
        geometry.dst_row_floats = dst->getRowBytes() / static_cast<int>(sizeof(float));
        geometry.channels = channels;
        geometry.premultiplied = premultiplied;
        const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, geometry);

        const bool host_gpu =
            args.isEnabledCudaRender || args.isEnabledMetalRender || args.isEnabledOpenCLRender;
        // EffectCraft doesn't always tell an effect a value changed, so Open Designer is also
        // noticed here. A GPU render's images are on the GPU, so it opens without a frame.
        if (take_open_designer_request()) {
            open_designer(args.time, args.renderScale, /*with_frame=*/!host_gpu);
        }
        if (host_gpu) {
            render_gpu(args, *src, *dst, plan, chain);
            return;
        }
        if (own_gpu_->getValueAtTime(args.time) && render_own_gpu(*src, *dst, geometry, plan, chain)) {
            return;
        }
        dyeline::run_plan_cpu(static_cast<const float*>(src->getPixelData()),
                              static_cast<float*>(dst->getPixelData()), chain.table.data(), plan);
    }

    // Spatial tools read around each pixel and warps read elsewhere, so ask the host for the
    // source the render reads.
    void getRegionsOfInterest(const OFX::RegionsOfInterestArguments& args,
                              OFX::RegionOfInterestSetter& rois) override {
        dyeline::CompiledChain chain;
        try {
            chain = chain_at(args.time, /*report=*/false);
        } catch (...) {
            return;  // render reports the error
        }
        if (!chain.has_spatial()) {
            return;
        }
        dyeline::RenderGeometry geometry =
            frame_geometry(args.time, args.renderScale, src_clip_->getPixelAspectRatio());
        const double cx = geometry.canonical_x;
        const double cy = geometry.canonical_y;
        const OfxRectD& asked = args.regionOfInterest;
        geometry.window = {static_cast<int>(std::floor(asked.x1 / cx)),
                           static_cast<int>(std::floor(asked.y1 / cy)),
                           static_cast<int>(std::ceil(asked.x2 / cx)),
                           static_cast<int>(std::ceil(asked.y2 / cy))};
        const dyeline::PixelRect source = dyeline::source_region(chain.pass, geometry);
        OfxRectD roi;
        roi.x1 = source.x1 * cx;
        roi.y1 = source.y1 * cy;
        roi.x2 = source.x2 * cx;
        roi.y2 = source.y2 * cy;
        rois.setRegionOfInterest(*src_clip_, roi);
    }

    bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identity_clip,
                    double& identity_time) override {
        if (open_designer_requested()) {
            return false;  // render, which opens the designer
        }
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
            const std::string path = dyeline::expand_home(file);
            LoadedLook loaded = cache_.from_file(path);
            loaded.base_dir = std::filesystem::u8path(path).parent_path().u8string();
            return loaded;
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
            dyeline::compile_chain(loaded.look, strength_->getValueAtTime(time), chain, error,
                                   loaded.base_dir);
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
            } else if (!chain.problems.empty()) {
                show_message(OFX::Message::eMessageWarning,
                             "Dyeline: part of this look is bypassed. " + join(chain.problems));
            } else {
                show_message(OFX::Message::eMessageMessage, notice());
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
    void edit_look(double time, OfxPointD render_scale) {
        std::string file;
        look_file_->getValue(file);
        std::string text;
        look_->getValueAtTime(time, text);
        std::vector<std::string> args;
        if (!file.empty()) {
            args = {"--file", dyeline::expand_home(file)};
        }
        const std::string frame_path = write_preview_frame(time, render_scale);
        if (!frame_path.empty()) {
            args.push_back("--frame");
            args.push_back(frame_path);
        }
        const dyeline::designer::RoundTrip trip =
            dyeline::designer::run(dyeline::plugin::designer_path(),
                                   text.empty() ? dyeline::write_look(dyeline::Look{}) : text, args);
        if (!frame_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(std::filesystem::u8path(frame_path), ignored);
        }
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

    // Opens the designer on the look file without waiting for it, for hosts without push
    // buttons or that can't wait (EffectCraft). Each save renders again: the plugin re-reads a
    // changed look file, and in EffectCraft the designer also bumps Reload Look File through
    // the control channel, when EffectCraft was started with one.
    // True when Open Designer holds a value this instance hasn't acted on yet.
    bool open_designer_requested() {
        int value = 0;
        open_designer_->getValue(value);
        std::lock_guard<std::mutex> lock(message_mutex_);
        return value != open_designer_seen_;
    }

    // Like open_designer_requested(), and marks the value as acted on.
    bool take_open_designer_request() {
        int value = 0;
        open_designer_->getValue(value);
        std::lock_guard<std::mutex> lock(message_mutex_);
        if (value == open_designer_seen_) {
            return false;
        }
        open_designer_seen_ = value;
        return true;
    }

    void open_designer(double time, OfxPointD render_scale, bool with_frame) {
        std::string file;
        look_file_->getValue(file);
        if (file.empty()) {
            set_notice("Dyeline: set Look File to a .dyeline file (it is created if it doesn't "
                       "exist), then add one to Open Designer.");
            return;
        }
        set_notice("");
        int counter = 0;
        open_designer_->getValue(counter);
        const std::string path = dyeline::expand_home(file);
        if (!designer_launches().claim(path, counter)) {
            return;  // another instance already opened it, or it is still open
        }
        std::vector<std::string> args = {"--file", path};
        const std::string frame_path = with_frame ? write_preview_frame(time, render_scale) : std::string();
        if (!frame_path.empty()) {
            args.push_back("--frame");
            args.push_back(frame_path);
        }
        if (const int port = dyeline::effectcraft::control_port(); port > 0) {
            args.push_back("--effectcraft-port");
            args.push_back(std::to_string(port));
        }
        std::string error;
        const bool started = dyeline::designer::start(
            dyeline::plugin::designer_path(), args,
            [path, frame_path](int) {
                if (!frame_path.empty()) {
                    std::error_code ignored;
                    std::filesystem::remove(std::filesystem::u8path(frame_path), ignored);
                }
                designer_launches().finished(path);
            },
            error);
        if (!started) {
            designer_launches().finished(path);
            if (!frame_path.empty()) {
                std::error_code ignored;
                std::filesystem::remove(std::filesystem::u8path(frame_path), ignored);
            }
            set_notice("Dyeline: the designer failed: " + error);
        }
    }

    // A message to show while the look renders cleanly, such as how to open the designer.
    std::string notice() {
        std::lock_guard<std::mutex> lock(message_mutex_);
        return notice_;
    }

    void set_notice(const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(message_mutex_);
            notice_ = text;
        }
        show_message(text.empty() ? OFX::Message::eMessageMessage : OFX::Message::eMessageWarning,
                     text);
    }

    // Saves the source frame at `time`, shrunk to preview size, for the designer to show.
    // Returns its path, or empty when there's no frame to show: an unconnected input, a host
    // that doesn't hand out images outside a render, or pixels that aren't float.
    std::string write_preview_frame(double time, OfxPointD render_scale) {
        try {
            if (!src_clip_->isConnected()) {
                return {};
            }
            std::unique_ptr<OFX::Image> src(src_clip_->fetchImage(time));
            if (!src || src->getPixelDepth() != OFX::eBitDepthFloat) {
                return {};
            }
            const int channels = channel_count(src->getPixelComponents());
            const OfxRectI bounds = src->getBounds();
            if (channels == 0 || bounds.x2 <= bounds.x1 || bounds.y2 <= bounds.y1) {
                return {};
            }
            const dyeline::RenderGeometry g =
                frame_geometry(time, render_scale, src->getPixelAspectRatio());
            dyeline::Frame geometry;
            geometry.premultiplied = src->getPreMultiplication() == OFX::eImagePreMultiplied;
            geometry.x0 = bounds.x1;
            geometry.y0 = bounds.y1;
            geometry.canonical_x = g.canonical_x;
            geometry.canonical_y = g.canonical_y;
            geometry.frame_left = g.frame_left;
            geometry.frame_bottom = g.frame_bottom;
            geometry.frame_right = g.frame_right;
            geometry.frame_top = g.frame_top;
            geometry.seed = g.seed;
            const dyeline::Frame frame = dyeline::shrink_to_fit(
                static_cast<const float*>(src->getPixelData()), bounds.x2 - bounds.x1,
                bounds.y2 - bounds.y1, channels, src->getRowBytes() / static_cast<int>(sizeof(float)),
                geometry, kPreviewWidth, kPreviewHeight);
            const std::string path = dyeline::designer::temp_path("-frame.dlframe");
            std::string error;
            return dyeline::write_frame_file(path, frame, error) ? path : std::string();
        } catch (...) {
            return {};  // the designer opens without a preview
        }
    }

    static dyeline::PixelRect to_rect(const OfxRectI& r) {
        return {r.x1, r.y1, r.x2, r.y2};
    }

    // Where a render sits in the frame, for tools that depend on position or reach their
    // neighbors: the project's extent is the frame, positions are in canonical coordinates,
    // and the seed follows the frame number. The caller fills in the window and buffers.
    dyeline::RenderGeometry frame_geometry(double time, OfxPointD render_scale, double aspect) {
        dyeline::RenderGeometry g;
        if (!(aspect > 0.0)) {
            aspect = 1.0;
        }
        g.canonical_x = static_cast<float>(aspect / render_scale.x);
        g.canonical_y = static_cast<float>(1.0 / render_scale.y);
        const OfxPointD offset = getProjectOffset();
        const OfxPointD size = getProjectSize();
        g.frame_left = static_cast<float>(offset.x);
        g.frame_bottom = static_cast<float>(offset.y);
        g.frame_right = static_cast<float>(offset.x + size.x);
        g.frame_top = static_cast<float>(offset.y + size.y);
        g.seed = static_cast<int>(std::floor(time + 0.5));
        return g;
    }

    // On a GPU render the images' pixel data are device buffers on the host's GPU: CUDA
    // device pointers, MTLBuffers for Metal, or cl_mem buffers for OpenCL.
    void render_gpu(const OFX::RenderArguments& args, const OFX::Image& src, OFX::Image& dst,
                    const dyeline::RenderPlan& plan, const dyeline::CompiledChain& chain) {
        std::string error;
        if (args.isEnabledCudaRender) {
            error = dyeline::cuda::render_plan(args.pCudaStream,
                                               static_cast<const float*>(src.getPixelData()),
                                               static_cast<float*>(dst.getPixelData()), plan,
                                               chain.table.data(), chain.table.size());
        } else if (args.isEnabledOpenCLRender) {
            error = dyeline::opencl::render_plan(args.pOpenCLCmdQ, src.getPixelData(),
                                                 dst.getPixelData(), plan, chain.table.data(),
                                                 chain.table.size());
        } else {
            error = dyeline::metal::render_plan(args.pMetalCmdQ, src.getPixelData(),
                                                dst.getPixelData(), plan, chain.table.data(),
                                                chain.table.size());
        }
        if (!error.empty()) {
            show_message(OFX::Message::eMessageError, "Dyeline: " + error);
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // Renders on Dyeline's own GPU when the host renders on the CPU. Returns false, so the
    // caller renders on the CPU, when there is no such GPU or it failed.
    bool render_own_gpu(const OFX::Image& src, OFX::Image& dst,
                        const dyeline::RenderGeometry& geometry, const dyeline::RenderPlan& plan,
                        const dyeline::CompiledChain& chain) {
        if (!own_gpu_works.load() || geometry.src_row_floats <= 0 || geometry.dst_row_floats <= 0) {
            return false;
        }
        const auto src_floats = static_cast<std::size_t>(geometry.src_row_floats) *
                                static_cast<std::size_t>(geometry.src_bounds.height());
        const auto dst_floats = static_cast<std::size_t>(geometry.dst_row_floats) *
                                static_cast<std::size_t>(geometry.dst_bounds.height());
        const auto* in = static_cast<const float*>(src.getPixelData());
        auto* out = static_cast<float*>(dst.getPixelData());
#ifdef __APPLE__
        std::string error = dyeline::metal::render_plan_own_device(
            in, src_floats, out, dst_floats, plan, chain.table.data(), chain.table.size());
#else
        std::string error = dyeline::opencl::render_plan_own_device(
            in, src_floats, out, dst_floats, plan, chain.table.data(), chain.table.size());
#endif
        if (error.empty()) {
            return true;
        }
        if (own_gpu_works.exchange(false)) {
            std::fprintf(stderr, "Dyeline: rendering on the CPU: %s\n", error.c_str());
        }
        return false;
    }

    OFX::Clip* dst_clip_;
    OFX::Clip* src_clip_;
    OFX::DoubleParam* strength_;
    OFX::BooleanParam* own_gpu_;
    OFX::IntParam* open_designer_;
    OFX::StringParam* look_file_;
    OFX::StringParam* look_;
    OFX::IntParam* look_revision_;
    OFX::StringParam* designer_status_;
    LookCache cache_;
    std::mutex message_mutex_;
    std::string message_;
    std::string notice_;
    int open_designer_seen_ = 0;
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

    // Hosts without push buttons (EffectCraft) can't use Edit Look, and a host that has to wait
    // for the designer stalls. Any new value here opens the designer on the look file and
    // returns at once.
    OFX::IntParamDescriptor* open_designer = desc.defineIntParam(kParamOpenDesigner);
    open_designer->setLabels("Open Designer", "Open Designer", "Open Designer");
    open_designer->setHint("Add one to open the Look Designer on the Look File, without the host "
                           "waiting for it. Each Save in the designer updates the host. Use it "
                           "where Edit Look isn't shown, such as EffectCraft.");
    open_designer->setDefault(0);
    open_designer->setRange(0, 1000000);
    open_designer->setDisplayRange(0, 100);
    open_designer->setAnimates(false);
    page->addChild(*open_designer);

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

    OFX::BooleanParamDescriptor* own_gpu = desc.defineBooleanParam(kParamOwnGpu);
    own_gpu->setLabels("Use Dyeline's GPU", "Use Dyeline's GPU", "Use Dyeline's GPU");
    own_gpu->setHint("When the host renders on the CPU, render on the computer's GPU anyway, "
                     "through OpenCL (Windows, Linux) or Metal (macOS). Falls back to the CPU "
                     "when there is no GPU. The host's own GPU rendering is used first.");
    own_gpu->setDefault(true);
    own_gpu->setAnimates(false);
    page->addChild(*own_gpu);

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
