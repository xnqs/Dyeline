#pragma once

// Thumbnails for the looks browser: a small copy of the frame rendered through each look, on
// a worker thread, one at a time, in the order asked.

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "dyeline/frame_file.h"
#include "dyeline/look.h"
#include "preview.h"

namespace dyeline::designer {

constexpr int kThumbnailWidth = 192;
constexpr int kThumbnailHeight = 108;

// A stand-in for the host's frame when the designer has none: a sweep of hues over a range of
// brightness, with a grey ramp and highlights, so looks still show their character.
Frame sample_frame(int width, int height);

struct ThumbnailJob {
    std::string key;  // LibraryLook::key
    Look look;
    std::string base_dir;
};

class ThumbnailWorker {
public:
    // Shrinks `frame` to thumbnail size once, up front.
    explicit ThumbnailWorker(const Frame& frame);
    ~ThumbnailWorker();
    ThumbnailWorker(const ThumbnailWorker&) = delete;
    ThumbnailWorker& operator=(const ThumbnailWorker&) = delete;

    const Frame& frame() const { return frame_; }

    // Replaces the jobs not yet started.
    void request(std::vector<ThumbnailJob> jobs);
    // Takes the finished thumbnails since the last call, as (key, image) pairs.
    std::vector<std::pair<std::string, DisplayImage>> take();
    // Blocks until every job so far is done.
    void wait_idle();

private:
    void run();

    Frame frame_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<ThumbnailJob> jobs_;
    std::vector<std::pair<std::string, DisplayImage>> done_;
    bool busy_ = false;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace dyeline::designer
