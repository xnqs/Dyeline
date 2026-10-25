#pragma once

// EffectCraft's control channel (`effectcraft --control <port>`): TCP on 127.0.0.1, one JSON
// request per line, one JSON reply per line. EffectCraft only renders an effect again when one
// of its values changes, so after the designer saves a look file it uses this channel to add
// one to Reload Look File on every Dyeline effect that renders that file.

#include <string>

namespace dyeline::effectcraft {

// The control port EffectCraft was started with: `--control <port>` or `--control=<port>` on
// this process's command line, else the EFFECTCRAFT_CONTROL_PORT variable. 0 when neither is
// set (the channel is off), or the value isn't a port.
int control_port();

// Reads a port from the EFFECTCRAFT_CONTROL_PORT variable alone. 0 when unset or invalid.
int control_port_from_environment();

// A port number from text, or 0 when it isn't one from 1 to 65535.
int parse_port(const std::string& text);

struct RefreshResult {
    int refreshed = 0;  // Dyeline effects whose Reload Look File was bumped
    std::string error;  // why the channel couldn't be used, or empty
};

// Finds every Dyeline effect in the open project whose Look File is `look_path` and adds one
// to its Reload Look File, so EffectCraft renders it again. Paths are compared after "~" is
// expanded and the path is made absolute.
RefreshResult refresh_look_file(int port, const std::string& look_path);

}  // namespace dyeline::effectcraft
