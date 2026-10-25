#include "preview.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "dyeline/chain.h"
#include "dyeline/color.h"
#include "dyeline/render.h"

namespace dyeline::designer {

namespace {

std::uint8_t to_byte(float v, ViewTransform view) {
    if (view == ViewTransform::LinearToSrgb) {
        v = dl_encode_curve(DL_CURVE_SRGB, v);
    }
    if (!(v > 0.0f)) {
        return 0;  // also catches NaN
    }
    return static_cast<std::uint8_t>(std::lround(std::min(v, 1.0f) * 255.0f));
}

DisplayImage to_display(const Frame& frame, const std::vector<float>& rgba, ViewTransform view) {
    DisplayImage image;
    image.width = frame.width;
    image.height = frame.height;
    image.rgba.resize(static_cast<std::size_t>(frame.width) * frame.height * 4);
    for (int y = 0; y < frame.height; ++y) {
        // OFX rows run bottom up; screens draw top down.
        const float* src = &rgba[static_cast<std::size_t>(frame.height - 1 - y) * frame.width * 4];
        std::uint8_t* dst = &image.rgba[static_cast<std::size_t>(y) * frame.width * 4];
        for (int x = 0; x < frame.width; ++x) {
            dst[x * 4] = to_byte(src[x * 4], view);
            dst[x * 4 + 1] = to_byte(src[x * 4 + 1], view);
            dst[x * 4 + 2] = to_byte(src[x * 4 + 2], view);
            dst[x * 4 + 3] = 255;
        }
    }
    return image;
}

}  // namespace

ViewTransform view_for(const std::string& space_id) {
    const ColorSpace* space = find_color_space(space_id);
    return space != nullptr && space->curve == DL_CURVE_LINEAR ? ViewTransform::LinearToSrgb
                                                               : ViewTransform::AsIs;
}

DisplayImage render_preview(const Frame& frame, const PreviewRequest& request,
                            std::vector<std::string>& problems) {
    problems.clear();
    const LookColor& color = request.look.color;
    if (request.original || frame.width <= 0 || frame.height <= 0) {
        return to_display(frame, frame.rgba, request.view.value_or(view_for(color.input)));
    }
    const ViewTransform view = request.view.value_or(
        view_for(color.output == "same" ? color.input : color.output));

    CompiledChain chain;
    std::string error;
    if (!compile_chain(request.look, 1.0, chain, error, request.base_dir)) {
        problems.push_back(error);
        return to_display(frame, frame.rgba, view);
    }
    problems = chain.problems;
    if (chain.is_identity()) {
        return to_display(frame, frame.rgba, view);
    }

    RenderGeometry g;
    g.window = {frame.x0, frame.y0, frame.x0 + frame.width, frame.y0 + frame.height};
    g.src_bounds = g.window;
    g.dst_bounds = g.window;
    g.src_row_floats = frame.width * 4;
    g.dst_row_floats = frame.width * 4;
    g.channels = 4;
    g.premultiplied = frame.premultiplied;
    g.canonical_x = frame.canonical_x;
    g.canonical_y = frame.canonical_y;
    g.frame_left = frame.frame_left;
    g.frame_bottom = frame.frame_bottom;
    g.frame_right = frame.frame_right;
    g.frame_top = frame.frame_top;
    g.seed = frame.seed;
    const RenderPlan plan = plan_render(chain.pass, g);
    std::vector<float> out(frame.rgba.size());
    run_plan_cpu(frame.rgba.data(), out.data(), chain.table.data(), plan);
    return to_display(frame, out, view);
}

PreviewWorker::PreviewWorker(std::shared_ptr<const Frame> frame)
    : frame_(std::move(frame)), thread_([this] { run(); }) {}

PreviewWorker::~PreviewWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void PreviewWorker::request(PreviewRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::make_unique<PreviewRequest>(std::move(request));
    }
    wake_.notify_all();
}

bool PreviewWorker::take(DisplayImage& image, std::vector<std::string>& problems) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fresh_) {
        return false;
    }
    fresh_ = false;
    image = std::move(done_);
    problems = std::move(done_problems_);
    return true;
}

void PreviewWorker::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return pending_ == nullptr && !busy_; });
}

void PreviewWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || pending_ != nullptr; });
        if (stop_) {
            return;
        }
        const std::unique_ptr<PreviewRequest> request = std::move(pending_);
        busy_ = true;
        lock.unlock();
        std::vector<std::string> problems;
        DisplayImage image = render_preview(*frame_, *request, problems);
        lock.lock();
        busy_ = false;
        done_ = std::move(image);
        done_problems_ = std::move(problems);
        fresh_ = true;
        if (pending_ == nullptr) {
            idle_.notify_all();
        }
    }
}

}  // namespace dyeline::designer
