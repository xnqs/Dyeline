#include "thumbnails.h"

#include <cmath>
#include <utility>

namespace dyeline::designer {

Frame sample_frame(int width, int height) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.reserve(static_cast<std::size_t>(width) * height * 4);
    const int ramp = height / 6;  // a grey ramp along the bottom
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            float r;
            float g;
            float b;
            if (y < ramp) {
                r = g = b = 0.005f * std::exp2(u * 9.0f);
            } else {
                // Hue across, brightness up, with saturation falling off towards the top.
                const float v = (static_cast<float>(y - ramp) + 0.5f) / static_cast<float>(height - ramp);
                const float level = 0.02f * std::exp2(v * 6.5f);
                const float saturation = 0.75f * (1.0f - 0.6f * v);
                const float h = u * 6.0f;
                auto channel = [&](float offset) {
                    const float k = std::fmod(h + offset, 6.0f);
                    const float c = std::fmax(0.0f, std::fmin(1.0f, std::fabs(k - 3.0f) - 1.0f));
                    return level * (1.0f - saturation + saturation * c);
                };
                r = channel(0.0f);
                g = channel(4.0f);
                b = channel(2.0f);
            }
            frame.rgba.insert(frame.rgba.end(), {r, g, b, 1.0f});
        }
    }
    frame.frame_right = static_cast<float>(width);
    frame.frame_top = static_cast<float>(height);
    return frame;
}

ThumbnailWorker::ThumbnailWorker(const Frame& frame)
    : frame_(shrink_to_fit(frame.rgba.data(), frame.width, frame.height, 4, frame.width * 4, frame,
                           kThumbnailWidth, kThumbnailHeight)),
      thread_([this] { run(); }) {}

ThumbnailWorker::~ThumbnailWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void ThumbnailWorker::request(std::vector<ThumbnailJob> jobs) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.assign(std::make_move_iterator(jobs.begin()), std::make_move_iterator(jobs.end()));
    }
    wake_.notify_all();
}

std::vector<std::pair<std::string, DisplayImage>> ThumbnailWorker::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(done_, {});
}

void ThumbnailWorker::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && !busy_; });
}

void ThumbnailWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
        if (stop_) {
            return;
        }
        ThumbnailJob job = std::move(jobs_.front());
        jobs_.pop_front();
        busy_ = true;
        lock.unlock();
        PreviewRequest request;
        request.look = std::move(job.look);
        request.base_dir = std::move(job.base_dir);
        std::vector<std::string> problems;
        DisplayImage image = render_preview(frame_, request, problems);
        lock.lock();
        busy_ = false;
        done_.emplace_back(std::move(job.key), std::move(image));
        if (jobs_.empty()) {
            idle_.notify_all();
        }
    }
}

}  // namespace dyeline::designer
