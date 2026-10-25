#pragma once

// The plugin's side of the Look Designer round trip: hand the current look to the designer
// app, wait while the user edits it, and read the edited look back. The designer runs as its
// own process, so a crash there never takes the host down.
//
// Milestone 0 spike: the look is a tiny flat JSON object holding the Exposure parameters.
// The full chain format arrives with the tool chain in Milestone 1.

#include <string>
#include <vector>

#include "dyeline/exposure.h"

namespace dyeline::designer {

enum class Outcome {
    Applied,    // the user applied an edited look
    Cancelled,  // the user closed the designer without applying
    Failed,     // the designer could not be started, crashed or returned nothing usable
};

struct RoundTrip {
    Outcome outcome = Outcome::Failed;
    std::string look;   // the edited look, when applied
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

// The spike's look format.
std::string format_look(const ExposureParams& params);
bool parse_look(const std::string& look, ExposureParams& params);

}  // namespace dyeline::designer
