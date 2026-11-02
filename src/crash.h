// Minimal crash reporting: writes a minidump next to the state file so a crash
// can be diagnosed instead of guessed at.
#pragma once

namespace crash {

void Install();

}  // namespace crash
