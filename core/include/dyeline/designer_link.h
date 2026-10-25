#pragma once

// The plugin's side of the Look Designer round trip: hand the current look to the designer
// app, wait while the user edits it, and read the edited look back. The designer runs as its
// own process, so a crash there never takes the host down.
//
// Hosts that never take parameter values back from a plugin (EffectCraft) use a look file
// instead: the designer saves a .dyeline file and the plugin reads it when it renders.

#include <functional>
#include <string>
#include <vector>

namespace dyeline::designer {

enum class Outcome {
    Applied,    // the user applied an edited look
    Cancelled,  // the user closed the designer without applying
    Failed,     // the designer could not be started, crashed or returned nothing usable
};

struct RoundTrip {
    Outcome outcome = Outcome::Failed;
    std::string look;   // the edited look (.dyeline JSON), when applied
    std::string error;  // why it failed
    double seconds = 0.0;
};

// Exit codes the designer app uses to report back.
inline constexpr int kExitApplied = 0;
inline constexpr int kExitCancelled = 1;

// A path in the temp folder that no other round trip uses, even from another host process,
// ending in `suffix`.
std::string temp_path(const char* suffix);

// Runs `designer_path --in <file> --out <file> [extra_args...]` and waits for it to exit.
// <in> holds `look`; on Applied, <out> holds the edited look. Both files are removed after.
RoundTrip run(const std::string& designer_path, const std::string& look,
              const std::vector<std::string>& extra_args = {});

// Starts `designer_path args...` without waiting for it, for Open Designer: the host keeps
// running while the designer edits a look file. `on_exit` is called on another thread with the
// designer's exit code, or -1 if it crashed. Returns false with `error` set if it didn't start.
bool start(const std::string& designer_path, const std::vector<std::string>& args,
           std::function<void(int)> on_exit, std::string& error);

}  // namespace dyeline::designer
