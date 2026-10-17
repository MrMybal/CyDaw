#pragma once
#include "NativeRuntime.h"
#include "PluginDefinition.h"
#include <array>
#include <atomic>
#include <cydaw/engine/MidiEffects.h>
#include <cydaw/engine/PolyVoice.h>
#include <cydaw/engine/VoiceAssetsFile.h>
#include <functional>
#include <span>
namespace cydaw::native
{
class EditorModel
{
  public:
    virtual ~EditorModel() = default;
    virtual const Definition &definition() const = 0;
    virtual double value(const char *) const = 0;
    virtual void edit(const char *, double) = 0;
    virtual bool analysis(uint64_t, bool, engine::AudioAnalysisFrame &) const = 0;
};
struct NativeBlock
{
    struct Bus
    {
        float *const *channels{};
        int count{};
    };
    std::span<const Bus> inputs;
    float *const *output{};
    int channels{}, frames{};
    std::span<const engine::MidiEffectEvent> midi;
    engine::MidiEffectEvent *midiOutput{};
    size_t midiCapacity{}, midiCount{};
    engine::MidiEffectClock clock;
    double seconds{};
};
class NativeProcessor final : public EditorModel
{
  public:
    NativeProcessor();
    ~NativeProcessor();
    const Definition &definition() const override { return definition_; }
    double value(const char *) const override;
    void edit(const char *, double) override;
    bool analysis(uint64_t node, bool input, engine::AudioAnalysisFrame &frame) const override
    {
        return runtime_.readTap(node ? node : graph_.outputNodeId, input, frame);
    }
    std::function<void(uint32_t, double)> edited;
    bool prepare(double, int);
    void reset();
    bool process(NativeBlock &);
    int latency() const { return std::max(0, runtime_.latencySamples()); }
    int parameterIndex(uint32_t) const;
    double parameterValue(uint32_t) const;
    void setParameter(uint32_t, double);
    void applyProgram(int);
    int program() const { return program_.load(); }
    std::vector<uint8_t> save() const;
    bool load(std::span<const uint8_t>);
    static constexpr uint32_t programId = 0x7fffffffu;

  private:
    void applyValues();
    void renderVoices(float *const *, int, int, int);
    void midi(const engine::MidiEffectEvent &);
    void noteOn(int channel, int note, float velocity);
    void loadVoice();
    std::atomic<int> program_{};
    struct Target;
    Definition definition_;
    std::unique_ptr<std::atomic<double>[]> values_;
    std::vector<Target> targets_;
    engine::NativeDspGraph graph_;
    engine::PolySynthGraphPlan instrument_;
    NativeDspRuntime runtime_;
    std::vector<std::unique_ptr<engine::PolyVoice>> voices_;
    std::vector<int> notes_, channels_;
    std::vector<uint64_t> ages_;
    uint64_t age_{};
    engine::prism::Context prismContext_;
    engine::MidiEffectChain midiEffects_;
    std::array<engine::MidiEffectEvent, engine::MidiEffectChain::capacity> midiIn_, midiOut_;
    std::vector<float> scratch_;
    std::array<float *, 8> pointers_{};
    double rate_{48000};
    int maximumBlock_{512};
    int preparedChannels_{2};
    std::array<float, 16> bends_{};
    bool sustain_[16]{}, deferred_[128][16]{};
    // The bundle's voice data (a sampler's samples, a sound model), shared
    // with the other instances that read the same file.
    std::shared_ptr<const engine::VoiceAssets> voice_;
    bool voiceTried_{};
    // Every key down now (a sampler's first and legato zones).
    std::array<bool, 128> down_{};
    int downCount_{};
    // The notes as the graph's modulators hear them.
    engine::modulator::ControlNotes controlNotes_;
};
class EditorSurface
{
  public:
    virtual ~EditorSurface() = default;
    virtual bool attach(void *) = 0;
    virtual void *handle() const = 0;
    virtual void setVisible(bool) = 0;
    virtual void idle() {}
};
std::unique_ptr<EditorSurface> makeEditor(EditorModel &);
} // namespace cydaw::native
