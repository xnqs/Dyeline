#pragma once

// The designer's preview: the host's frame rendered through the look being edited, turned
// into 8-bit pixels for the screen. Rendering runs on a worker thread so the controls stay
// responsive while a heavy look renders.

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "dyeline/frame_file.h"
#include "dyeline/look.h"

namespace dyeline::designer {

// How preview values become screen values.
enum class ViewTransform {
    AsIs,          // the values are already display-encoded (Rec.709, sRGB, log)
    LinearToSrgb,  // the values are scene-linear: encode them with the sRGB curve
};

// The view that suits a color space: sRGB encoding for a linear space, none for the rest.
// Unknown spaces are shown as they are.
ViewTransform view_for(const std::string& space_id);

// 8-bit RGBA, top row first, as screens want it.
struct DisplayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct PreviewRequest {
    Look look;
    std::string base_dir;  // where the look's relative paths (LUT files) are found
    bool original = false; // show the frame without the look
    std::optional<ViewTransform> view;  // unset: view_for the space shown
};

// Renders `frame` through the request's look and converts it for display. The original is
// shown in the look's input space; the result in its output space. `problems` lists parts of
// the look that were left out (as in CompiledChain::problems).
DisplayImage render_preview(const Frame& frame, const PreviewRequest& request,
                            std::vector<std::string>& problems);

// Renders previews on a worker thread, always the latest request: requests made while a
// render runs replace each other, and only the last one is rendered next.
class PreviewWorker {
public:
    explicit PreviewWorker(std::shared_ptr<const Frame> frame);
    ~PreviewWorker();
    PreviewWorker(const PreviewWorker&) = delete;
    PreviewWorker& operator=(const PreviewWorker&) = delete;

    void request(PreviewRequest request);
    // Takes the newest finished image, if one arrived since the last call.
    bool take(DisplayImage& image, std::vector<std::string>& problems);
    // Blocks until every request so far has been rendered.
    void wait_idle();

private:
    void run();

    std::shared_ptr<const Frame> frame_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::unique_ptr<PreviewRequest> pending_;
    bool busy_ = false;
    bool stop_ = false;
    bool fresh_ = false;
    DisplayImage done_;
    std::vector<std::string> done_problems_;
    std::thread thread_;
};

}  // namespace dyeline::designer
