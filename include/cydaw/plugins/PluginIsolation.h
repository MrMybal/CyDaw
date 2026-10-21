#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace cydaw::plugins {

inline constexpr std::uint32_t workerProtocolMagic = 0x43594457; // "CYDW"
inline constexpr std::uint16_t workerProtocolVersion = 6;
inline constexpr std::uint32_t maximumSharedAudioChannels = 8;
inline constexpr std::uint32_t maximumSharedAudioFrames = 2'048;
inline constexpr std::uint32_t maximumSharedPluginParameters = 512;
inline constexpr std::uint32_t maximumSharedAutomationParameters = 64;
inline constexpr std::uint32_t maximumSharedPluginStateBytes = 4 * 1'024 * 1'024;

enum class PluginRuntime : std::uint8_t {
    native,
    windowsViaWine
};

enum class IsolationPolicy : std::uint8_t {
    // Default and safest: one crash can only take down one plug-in instance.
    perInstance,
    // Optional optimisation for plug-ins which deliberately share resources.
    perPluginBinary
};

enum class WorkerState : std::uint8_t {
    stopped,
    starting,
    running,
    unresponsive,
    crashed,
    restarting,
    quarantined
};

struct PluginWorkerConfig {
    std::uint64_t instanceId {};
    PluginRuntime runtime { PluginRuntime::native };
    IsolationPolicy isolation { IsolationPolicy::perInstance };
    double sampleRate { 48'000.0 };
    std::uint32_t maximumBlockSize { 512 };
    std::uint16_t maximumInputChannels { 2 };
    std::uint16_t maximumOutputChannels { 2 };
};

// Header placed before planar audio and event storage in a shared-memory slot.
// It deliberately contains only fixed-width POD fields so native and Wine
// workers can share the same versioned wire format.
struct AudioBlockHeader {
    std::uint32_t magic { workerProtocolMagic };
    std::uint16_t protocolVersion { workerProtocolVersion };
    std::uint16_t flags {};
    std::uint64_t sequence {};
    std::int64_t projectSamplePosition {};
    std::uint32_t frameCount {};
    std::uint16_t inputChannels {};
    std::uint16_t outputChannels {};
    std::uint32_t eventBytes {};
};

enum class SharedPluginState : std::uint32_t {
    empty,
    loading,
    ready,
    error,
    stopping
};

enum class SharedPluginCommand : std::uint32_t {
    none,
    refreshParameters,
    setParameter,
    getState,
    setState,
    showEditor,
    hideEditor,
    // The plug-in's own programs: their names in stateData, one per line,
    // their count in commandParameterValue, the current one in
    // commandParameterIndex. setProgram takes commandParameterIndex.
    getPrograms,
    setProgram
};

enum SharedPluginParameterFlags : std::uint32_t {
    parameterAutomatable = 1u << 0u,
    parameterDiscrete = 1u << 1u,
    parameterBoolean = 1u << 2u
};

struct SharedPluginParameter {
    std::uint32_t index {};
    std::uint32_t flags {};
    float value {};
    char name[96] {};
    char label[32] {};
    // The value as the plug-in shows it ("-6.0 dB", "Hall", "On").
    char text[32] {};
};

struct SharedPluginAutomationValue {
    std::uint32_t index {};
    float value {};
};

// Single-slot, one-block-pipelined transport shared by a DAW instance and one
// plugin worker. Synchronisation uses atomic_ref on the aligned sequence/state
// fields so the mapped structure itself stays fixed-width and POD.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line padding for shared audio slots.
#endif
struct alignas(64) SharedPluginAudio {
    std::uint32_t magic { workerProtocolMagic };
    std::uint16_t protocolVersion { workerProtocolVersion };
    std::uint16_t reserved {};
    alignas(8) std::uint64_t requestSequence {};
    alignas(8) std::uint64_t responseSequence {};
    alignas(8) std::uint64_t heartbeat {};
    alignas(8) std::uint64_t commandSequence {};
    alignas(8) std::uint64_t commandResponseSequence {};
    alignas(8) std::uint64_t automationSequence {};
    alignas(4) std::uint32_t state { static_cast<std::uint32_t>(SharedPluginState::empty) };
    alignas(4) std::uint32_t shutdownRequested {};
    std::uint32_t frameCount {};
    std::uint16_t inputChannels {};
    std::uint16_t outputChannels {};
    std::uint32_t outputFrameCount {};
    std::uint32_t latencySamples {};
    std::uint32_t command { static_cast<std::uint32_t>(SharedPluginCommand::none) };
    std::int32_t commandResult {};
    std::uint32_t commandParameterIndex {};
    float commandParameterValue {};
    std::uint32_t stateBytes {};
    std::uint32_t parameterCount {};
    std::uint32_t automationCount {};
    char statusMessage[512] {};
    char commandStatus[256] {};
    SharedPluginParameter parameters[maximumSharedPluginParameters] {};
    SharedPluginAutomationValue automation[maximumSharedAutomationParameters] {};
    alignas(64) float input[maximumSharedAudioChannels * maximumSharedAudioFrames] {};
    alignas(64) float output[maximumSharedAudioChannels * maximumSharedAudioFrames] {};
    alignas(64) std::uint8_t stateData[maximumSharedPluginStateBytes] {};
    // Appended in protocol 6: one transport snapshot per submitted audio block.
    double transportSeconds {};
    double transportTempo {120.0};
    double transportQuarterNotes {};
    std::uint32_t transportPlaying {};
    std::uint32_t transportNumerator {4};
    std::uint32_t transportDenominator {4};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

[[nodiscard]] constexpr std::size_t sharedAudioIndex(const std::uint32_t channel,
                                                      const std::uint32_t frame) noexcept
{
    return static_cast<std::size_t>(channel) * maximumSharedAudioFrames + frame;
}

// An atomic view of one field of the shared structure: std::atomic_ref where
// the standard library has it (MSVC, libstdc++); elsewhere (Apple's libc++,
// Xcode 15 and 16) the same operations on the compiler's atomic built-ins.
#if defined(__cpp_lib_atomic_ref)
template <typename T>
using SharedAtomic = std::atomic_ref<T>;
#else
template <typename T>
class SharedAtomic {
public:
    static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), 0);

    explicit SharedAtomic(T& value) noexcept : value_(&value) {}

    [[nodiscard]] T load(const std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return __atomic_load_n(value_, builtinOrder(order));
    }
    void store(const T desired, const std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        __atomic_store_n(value_, desired, builtinOrder(order));
    }
    T fetch_add(const T operand, const std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return __atomic_fetch_add(value_, operand, builtinOrder(order));
    }

private:
    static constexpr int builtinOrder(const std::memory_order order) noexcept
    {
        switch (order) {
        case std::memory_order_relaxed: return __ATOMIC_RELAXED;
        case std::memory_order_consume: return __ATOMIC_CONSUME;
        case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
        case std::memory_order_release: return __ATOMIC_RELEASE;
        case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
        case std::memory_order_seq_cst: break;
        }
        return __ATOMIC_SEQ_CST;
    }
    T* value_;
};
#endif

template <typename T>
[[nodiscard]] SharedAtomic<T> sharedAtomic(T& value) noexcept
{
    return SharedAtomic<T>(value);
}

static_assert(std::is_trivially_copyable_v<SharedPluginAudio>);
static_assert(SharedAtomic<std::uint32_t>::is_always_lock_free);
static_assert(SharedAtomic<std::uint64_t>::is_always_lock_free);
// This layout is a cross-compiler wire ABI shared by GCC/Clang Linux hosts and
// the MSVC worker launched through Wine/Proton. Any change must bump the
// protocol version and preserve explicit verification on every platform.
static_assert(sizeof(SharedPluginParameter) == 172);
static_assert(sizeof(SharedPluginAutomationValue) == 8);
static_assert(offsetof(SharedPluginAudio, requestSequence) == 8);
static_assert(offsetof(SharedPluginAudio, automationSequence) == 48);
static_assert(offsetof(SharedPluginAudio, state) == 56);
static_assert(offsetof(SharedPluginAudio, statusMessage) == 108);
static_assert(offsetof(SharedPluginAudio, parameters) == 876);
static_assert(offsetof(SharedPluginAudio, automation) == 88'940);
static_assert(offsetof(SharedPluginAudio, input) == 89'472);
static_assert(offsetof(SharedPluginAudio, output) == 155'008);
static_assert(offsetof(SharedPluginAudio, stateData) == 220'544);
static_assert(sizeof(SharedPluginAudio) == 4'414'912);

struct CrashPolicy {
    std::uint32_t automaticRestartLimit { 3 };
    std::uint32_t restartWindowSeconds { 60 };
    std::uint32_t heartbeatTimeoutMilliseconds { 2'000 };
};

class PluginWorkerState final {
public:
    explicit PluginWorkerState(CrashPolicy policy = {});

    void beginStart() noexcept;
    void markRunning() noexcept;
    void markUnresponsive() noexcept;
    void markCrashed() noexcept;
    [[nodiscard]] bool beginAutomaticRestart() noexcept;
    void stop() noexcept;

    [[nodiscard]] WorkerState state() const noexcept;
    [[nodiscard]] std::uint32_t consecutiveCrashes() const noexcept;

private:
    CrashPolicy policy_;
    WorkerState state_ { WorkerState::stopped };
    std::uint32_t consecutiveCrashes_ {};
};

} // namespace cydaw::plugins
