#pragma once

#include <cydaw/engine/NativeDspGraph.h>

#include <cstdint>

// Containers, as Bitwig has them: a device that holds chains of devices (a
// layer each, one of them chosen, the mid and the side, the bands, the four
// corners of a pad…) and mixes what they give back. In the track's graph a
// container is its head (the card, every setting of it: fxLayer and the
// others), the chains its head's branch outputs feed, and its end (a
// containerEnd node) that their ends feed; the head passes its input to the
// end as the dry sound. The engine gives each branch its part of the sound
// (a containerTap the compiler adds, reading what the head split off) and the
// end mixes them as the head's settings say. Being plain nodes, the chains'
// devices keep everything a card has: automation, modulators, latency
// compensation, export.

namespace cydaw::engine::container {

// The head's ports: its input, the dry sound out (to its end), then one
// output per branch. The end's: the dry sound in, one input per branch, out.
inline constexpr std::uint64_t headInputPort = 1;
inline constexpr std::uint64_t headDryPort = 2;
inline constexpr std::uint64_t endDryPort = 1;
inline constexpr std::uint64_t endOutputPort = 2;
inline constexpr std::uint64_t branchPortBase = 900;
inline constexpr int maximumBranches = 8;
// A container with a chain after its mix (XY FX's Post FX) has two ends: the
// first mixes the branches (no dry sound, no Mix), the post chain follows,
// and the last takes it on this port, the dry sound beside it, and mixes.
inline constexpr std::uint64_t postPort = 950;
[[nodiscard]] constexpr bool hasPost(const NativeDspNodeKind kind) noexcept
{
    return kind == NativeDspNodeKind::xyFx;
}

[[nodiscard]] constexpr std::uint64_t branchPort(const int branch) noexcept
{
    return branchPortBase + static_cast<std::uint64_t>(branch);
}
[[nodiscard]] constexpr int branchOfPort(const std::uint64_t port) noexcept
{
    return port >= branchPortBase && port < branchPortBase + maximumBranches ? static_cast<int>(port - branchPortBase) : -1;
}

// A container's head.
[[nodiscard]] constexpr bool isHead(const NativeDspNodeKind kind) noexcept
{
    return kind >= NativeDspNodeKind::fxChain && kind <= NativeDspNodeKind::replacer;
}

// Its branches: fixed, or as many as its "chains" setting says (layers, a
// selector's chains).
[[nodiscard]] constexpr bool variableBranches(const NativeDspNodeKind kind) noexcept
{
    return kind == NativeDspNodeKind::fxLayer || kind == NativeDspNodeKind::fxSelector;
}
[[nodiscard]] constexpr int fixedBranches(const NativeDspNodeKind kind) noexcept
{
    switch (kind) {
    case NativeDspNodeKind::fxChain:
    case NativeDspNodeKind::replacer: return 1;
    case NativeDspNodeKind::midSideSplit:
    case NativeDspNodeKind::stereoSplit:
    case NativeDspNodeKind::multiband2: return 2;
    case NativeDspNodeKind::multiband3: return 3;
    case NativeDspNodeKind::xyFx: return 4;
    default: return 2;
    }
}

// What a branch hears of the container's input.
enum class Part : std::uint8_t { whole, mid, side, left, right, low, middle, high, generated };
[[nodiscard]] constexpr Part partOf(const NativeDspNodeKind kind, const int branch) noexcept
{
    switch (kind) {
    case NativeDspNodeKind::midSideSplit: return branch == 0 ? Part::mid : Part::side;
    case NativeDspNodeKind::stereoSplit: return branch == 0 ? Part::left : Part::right;
    case NativeDspNodeKind::multiband2: return branch == 0 ? Part::low : Part::high;
    case NativeDspNodeKind::multiband3: return branch == 0 ? Part::low : branch == 1 ? Part::middle : Part::high;
    case NativeDspNodeKind::replacer: return Part::generated;
    default: return Part::whole;
    }
}

// A branch's name, English (a module's text).
[[nodiscard]] constexpr const char* branchWord(const NativeDspNodeKind kind) noexcept
{
    switch (kind) {
    case NativeDspNodeKind::fxLayer: return "Layer";
    case NativeDspNodeKind::fxSelector: return "Chain";
    default: return nullptr;
    }
}
[[nodiscard]] constexpr const char* fixedBranchName(const NativeDspNodeKind kind, const int branch) noexcept
{
    constexpr const char* corners[] { "A", "B", "C", "D" };
    switch (kind) {
    case NativeDspNodeKind::fxChain: return "Chain";
    case NativeDspNodeKind::replacer: return "Replacement";
    case NativeDspNodeKind::midSideSplit: return branch == 0 ? "Mid" : "Side";
    case NativeDspNodeKind::stereoSplit: return branch == 0 ? "Left" : "Right";
    case NativeDspNodeKind::multiband2: return branch == 0 ? "Low" : "High";
    case NativeDspNodeKind::multiband3: return branch == 0 ? "Low" : branch == 1 ? "Mid" : "High";
    case NativeDspNodeKind::xyFx: return branch >= 0 && branch < 4 ? corners[branch] : branch == 4 ? "Post FX" : "?";
    default: return nullptr;
    }
}

// Where a branch's own settings sit in the head's eqBands (layers, a
// selector's chains, bands): its volume (dB), pan, mute, solo.
inline constexpr int volumeSlot = 0;
inline constexpr int panSlot = 8;
inline constexpr int muteSlot = 16;
inline constexpr int soloSlot = 24;

// The replacer's sounds.
inline constexpr int generatorCount = 4;
[[nodiscard]] constexpr const char* generatorName(const int generator) noexcept
{
    constexpr const char* names[generatorCount] { "Kick", "Tone", "Noise", "Click" };
    return generator >= 0 && generator < generatorCount ? names[generator] : "?";
}

} // namespace cydaw::engine::container
