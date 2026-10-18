#pragma once

// What CyDAW takes of the computer, for the top bar: its share of the
// processors (all cores together) and the memory it holds, read again
// every half second at most.

namespace cydaw::v2::system_load {

struct Reading {
    // Percent of the whole machine's processors (-1: unknown here).
    double cpuPercent { -1.0 };
    // Bytes of memory it holds (its working set; -1: unknown here).
    double memoryBytes { -1.0 };
};

[[nodiscard]] Reading read();

} // namespace cydaw::v2::system_load
