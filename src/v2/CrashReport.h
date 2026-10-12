#pragma once

namespace cydaw::v2 {

// Writes the stack of an unhandled crash to %TEMP%\cydaw2-crash.txt.
void installCrashReport();

} // namespace cydaw::v2
