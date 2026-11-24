#pragma once

// dyeline-cube: bakes looks into .cube 3D LUTs from the command line, many at once, with the
// same baking as the designer's Export .cube (see cube_export.h).

#include <ostream>
#include <string>
#include <vector>

namespace dyeline::cli {

// Runs the command on its arguments (without the program name). Progress and what each LUT
// leaves out go to `out`, problems to `err`. Returns the exit code: 0 when every look was
// baked, 1 when any failed, 2 for a usage error.
int run_cube(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace dyeline::cli
