#pragma once

// The plugin's side of the Look Designer round trip: hand the current look to the designer
// app, wait while the user edits it, and read the edited look back. The designer runs as its
// own process, so a crash there never takes the host down.
//
// Hosts that never take parameter values back from a plugin (EffectCraft) use a look file
// instead: the designer saves a .dyeline file and the plugin reads it when it renders.

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

// Runs `designer_path --in <file> --out <file> [extra_args...]` and waits for it to exit.
// <in> holds `look`; on Applied, <out> holds the edited look. Both files are removed after.
RoundTrip run(const std::string& designer_path, const std::string& look,
              const std::vector<std::string>& extra_args = {});

}  // namespace dyeline::designer
