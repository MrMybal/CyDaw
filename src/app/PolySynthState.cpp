#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include <algorithm>
#include <cmath>
#include <cydaw/document/PolySynthState.h>
#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/MidiEffects.h>
namespace cydaw::app {
cydaw::engine::PolySynthGraphPlan
sanitisePolySynthGraphPlan(cydaw::engine::PolySynthGraphPlan plan) {
  plan.voiceCount = cydaw::base::jlimit(1, 32, plan.voiceCount);
  plan.voiceMode = cydaw::base::jlimit(0, 2, plan.voiceMode);
  plan.glideSeconds = cydaw::base::jlimit(0.0f, 2.0f, plan.glideSeconds);
  plan.oscillatorLevel = cydaw::base::jlimit(
      0.0f, cydaw::engine::playsRecordingLevel(plan.waveform) ? 1.0f : 0.5f,
      plan.oscillatorLevel);
  plan.attackSeconds = cydaw::base::jlimit(0.0001f, 5.0f, plan.attackSeconds);
  plan.decaySeconds = cydaw::base::jlimit(0.0001f, 5.0f, plan.decaySeconds);
  plan.sustainLevel = cydaw::base::jlimit(0.0f, 1.0f, plan.sustainLevel);
  plan.releaseSeconds =
      cydaw::base::jlimit(0.0001f, 10.0f, plan.releaseSeconds);
  plan.wavetablePosition =
      cydaw::base::jlimit(0.0f, 1.0f, plan.wavetablePosition);
  plan.wavetableDetuneCents =
      cydaw::base::jlimit(0.0f, 50.0f, plan.wavetableDetuneCents);
  plan.stringDamping = cydaw::base::jlimit(0.0f, 1.0f, plan.stringDamping);
  plan.stringDecaySeconds =
      cydaw::base::jlimit(0.05f, 20.0f, plan.stringDecaySeconds);
  plan.filterCutoffHz =
      cydaw::base::jlimit(20.0f, 20'000.0f, plan.filterCutoffHz);
  plan.filterResonance = cydaw::base::jlimit(0.1f, 20.0f, plan.filterResonance);
  plan.minimumMidiNote = cydaw::base::jlimit(0, 127, plan.minimumMidiNote);
  plan.maximumMidiNote =
      cydaw::base::jlimit(plan.minimumMidiNote, 127, plan.maximumMidiNote);
  plan.velocityScale = cydaw::base::jlimit(0.0f, 2.0f, plan.velocityScale);
  return plan;
}

cydaw::base::Value
serialisePolySynthGraphPlan(const cydaw::engine::PolySynthGraphPlan &plan) {
  auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
  root->setProperty("schema", 1);
  root->setProperty("enabled", plan.enabled);
  root->setProperty("graphNodeCount",
                    static_cast<cydaw::base::int64>(plan.graphNodeCount));
  root->setProperty("voiceCount", plan.voiceCount);
  root->setProperty("voiceMode", plan.voiceMode);
  root->setProperty("glideSeconds", plan.glideSeconds);
  root->setProperty("waveform", static_cast<int>(plan.waveform));
  root->setProperty("wavetablePosition", plan.wavetablePosition);
  root->setProperty("wavetableDetune", plan.wavetableDetuneCents);
  root->setProperty("stringDamping", plan.stringDamping);
  root->setProperty("stringDecay", plan.stringDecaySeconds);
  root->setProperty("oscillatorLevel", plan.oscillatorLevel);
  root->setProperty("attackSeconds", plan.attackSeconds);
  root->setProperty("decaySeconds", plan.decaySeconds);
  root->setProperty("sustainLevel", plan.sustainLevel);
  root->setProperty("releaseSeconds", plan.releaseSeconds);
  root->setProperty("filterCutoffHz", plan.filterCutoffHz);
  root->setProperty("filterResonance", plan.filterResonance);
  root->setProperty("transposeSemitones", plan.transposeSemitones);
  root->setProperty("minimumMidiNote", plan.minimumMidiNote);
  root->setProperty("maximumMidiNote", plan.maximumMidiNote);
  root->setProperty("velocityScale", plan.velocityScale);
  // A sound model's settings (its data rides with the graph's JSON).
  cydaw::base::Array<cydaw::base::Value> modelValues;
  for (const auto value : plan.soundModelValues)
    modelValues.add(value);
  root->setProperty("soundModelValues", modelValues);
  // A sampler's settings (its zones ride with the graph's JSON).
  cydaw::base::Array<cydaw::base::Value> samplerValues;
  for (const auto value : plan.samplerValues)
    samplerValues.add(value);
  root->setProperty("samplerValues", samplerValues);
  // Its card's controllers, those it sets.
  {
    auto *controllers = new cydaw::base::Object();
    const cydaw::base::Value controllersVar(controllers);
    for (std::size_t number = 0; number < plan.samplerControllers.size();
         ++number)
      if (plan.samplerControllers[number] >= 0.0f)
        controllers->setProperty(cydaw::base::Identifier(cydaw::base::Text(
                                     static_cast<int>(number))),
                                 plan.samplerControllers[number]);
    if (!controllers->getProperties().isEmpty())
      root->setProperty("samplerControllers", controllersVar);
  }
  // The MIDI effects: each its kind and its values.
  if (plan.midiEffects != nullptr && plan.midiEffects->count > 0) {
    cydaw::base::Array<cydaw::base::Value> effects;
    for (std::size_t index = 0; index < plan.midiEffects->count; ++index) {
      const auto &effect = plan.midiEffects->effects[index];
      auto *item = new cydaw::base::Object();
      item->setProperty("kind", static_cast<int>(effect.kind));
      cydaw::base::Array<cydaw::base::Value> values;
      for (const auto value : effect.values)
        values.add(value);
      item->setProperty("values", values);
      effects.add(cydaw::base::Value(item));
    }
    root->setProperty("midiEffects", effects);
  }
  if (plan.drumKit) {
    root->setProperty("drumKit", true);
    cydaw::base::Array<cydaw::base::Value> drumValues;
    for (int index = 0; index < cydaw::engine::drum::valueCount; ++index)
      drumValues.add(plan.drumValues[static_cast<std::size_t>(index)]);
    root->setProperty("drumValues", drumValues);
  }
  // Prisme's settings (its own tables, imported waves, ride with the
  // graph's JSON on its card; attachVoiceAssets puts them back).
  if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism) {
    cydaw::base::Array<cydaw::base::Value> prismValues;
    for (const auto value : plan.prismValues)
      prismValues.add(value);
    root->setProperty("prismValues", prismValues);
  }
  if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::kick) {
    cydaw::base::Array<cydaw::base::Value> kickValues;
    for (const auto value : plan.kickValues)
      kickValues.add(value);
    root->setProperty("kickValues", kickValues);
  }
  // A synth of the family: its model and its settings, in its order.
  if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::family) {
    root->setProperty("synthModel", static_cast<int>(plan.synthModel));
    cydaw::base::Array<cydaw::base::Value> synthValues;
    for (int index = 0;
         index < cydaw::engine::synth::parameterCount(plan.synthModel); ++index)
      synthValues.add(plan.synthValues[static_cast<std::size_t>(index)]);
    root->setProperty("synthValues", synthValues);
  }
  return cydaw::base::Value(root.get());
}

void attachReplacerInstruments(cydaw::engine::NativeDspGraph &graph,
                               const cydaw::base::Value &runtimeState,
                               const bool waitForSamples) {
  const auto *replacers =
      runtimeState.getProperty("replacers", cydaw::base::Value()).getArray();
  if (replacers == nullptr)
    return;
  for (const auto &entry : *replacers) {
    const auto id = static_cast<std::uint64_t>(
        static_cast<cydaw::base::int64>(entry.getProperty("node", 0)));
    cydaw::base::Text error;
    auto plan = restorePolySynthGraphPlan(
        entry.getProperty("polySynth", cydaw::base::Value()), error);
    if (!plan)
      continue;
    attachVoiceAssets(*plan, entry.getProperty("editor", cydaw::base::Value()));
    if (waitForSamples && plan->sampler != nullptr)
      (void)SampleStore::instance().waitUntilReady(*plan->sampler, 60'000);
    for (auto &node : graph.nodes)
      if (node.id == id)
        node.instrument =
            std::make_shared<const cydaw::engine::PolySynthGraphPlan>(*plan);
  }
}

void attachVoiceAssets(cydaw::engine::PolySynthGraphPlan &plan,
                       const cydaw::base::Value &graphState) {
  const auto assets = voiceAssetsFromGraphState(graphState);
  plan.soundModel = assets.soundModel;
  plan.sampler = assets.sampler;
  if (assets.wavetable != nullptr)
    plan.wavetable = assets.wavetable;
  if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism &&
      assets.prismTables != nullptr)
    plan.prismTables = cydaw::engine::prism::resolveTables(plan.prismValues,
                                                           assets.prismTables);
}

std::optional<cydaw::engine::PolySynthGraphPlan>
restorePolySynthGraphPlan(const cydaw::base::Value &state,
                          cydaw::base::Text &error) {
  const auto *root = state.getDynamicObject();
  if (root == nullptr || static_cast<int>(root->getProperty("schema")) != 1) {
    error = "Unsupported polyphonic instrument Graph state";
    return {};
  }
  cydaw::engine::PolySynthGraphPlan plan;
  plan.enabled = static_cast<bool>(root->getProperty("enabled"));
  plan.graphNodeCount = static_cast<std::size_t>(
      static_cast<cydaw::base::int64>(root->getProperty("graphNodeCount")));
  plan.voiceCount = static_cast<int>(root->getProperty("voiceCount"));
  plan.voiceMode = static_cast<int>(root->getProperty("voiceMode"));
  plan.glideSeconds = static_cast<float>(
      static_cast<double>(root->getProperty("glideSeconds")));
  plan.waveform =
      static_cast<cydaw::engine::PolyOscillatorWaveform>(cydaw::base::jlimit(
          0, static_cast<int>(cydaw::engine::PolyOscillatorWaveform::family),
          static_cast<int>(root->getProperty("waveform"))));
  if (const auto *values = root->getProperty("prismValues").getArray()) {
    for (int index = 0;
         index < values->size() && index < cydaw::engine::prism::parameterCount;
         ++index) {
      const auto value =
          static_cast<float>(static_cast<double>((*values)[index]));
      if (std::isfinite(value))
        plan.prismValues[static_cast<std::size_t>(index)] =
            cydaw::engine::prism::clampValue(index, value);
    }
  }
  if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism)
    plan.prismTables = cydaw::engine::prism::resolveTables(plan.prismValues);
  plan.synthModel = cydaw::engine::synth::modelOf(
      static_cast<float>(static_cast<int>(root->getProperty("synthModel"))));
  if (plan.synthModel != cydaw::engine::synth::Model::none) {
    cydaw::engine::synth::warmUp(plan.synthModel);
    plan.synthValues = cydaw::engine::synth::defaultValues(plan.synthModel);
    if (const auto *values = root->getProperty("synthValues").getArray())
      for (int index = 0;
           index < values->size() &&
           index < cydaw::engine::synth::parameterCount(plan.synthModel);
           ++index) {
        const auto value =
            static_cast<float>(static_cast<double>((*values)[index]));
        if (std::isfinite(value))
          plan.synthValues[static_cast<std::size_t>(index)] =
              cydaw::engine::synth::clampValue(plan.synthModel, index, value);
      }
  }
  if (const auto *values = root->getProperty("kickValues").getArray()) {
    for (int index = 0;
         index < values->size() && index < cydaw::engine::kick::parameterCount;
         ++index) {
      const auto value =
          static_cast<float>(static_cast<double>((*values)[index]));
      if (std::isfinite(value))
        plan.kickValues[static_cast<std::size_t>(index)] =
            cydaw::engine::kick::clampValue(index, value);
    }
  }
  if (const auto *effects = root->getProperty("midiEffects").getArray();
      effects != nullptr && !effects->isEmpty()) {
    auto read = std::make_shared<cydaw::engine::MidiEffectsPlan>();
    for (const auto &item : *effects) {
      const auto kind = static_cast<int>(item.getProperty("kind", 0));
      if (read->count >= cydaw::engine::midiEffectMost || kind < 0 ||
          kind > static_cast<int>(cydaw::engine::lastNativeDspNodeKind))
        continue;
      auto &effect = read->effects[read->count];
      effect.kind = static_cast<cydaw::engine::NativeDspNodeKind>(kind);
      const auto *descriptor = cydaw::engine::effectForKind(effect.kind);
      if (descriptor == nullptr || !descriptor->midi)
        continue;
      if (const auto *values = item.getProperty("values", {}).getArray())
        for (int index = 0; index < values->size() &&
                            index < static_cast<int>(effect.values.size());
             ++index)
          effect.values[static_cast<std::size_t>(index)] =
              static_cast<float>(static_cast<double>((*values)[index]));
      ++read->count;
    }
    if (read->count > 0)
      plan.midiEffects = std::move(read);
  }
  plan.drumKit = static_cast<bool>(root->getProperty("drumKit"));
  if (plan.drumKit) {
    plan.drumValues = cydaw::engine::drum::defaultValues();
    if (const auto *values = root->getProperty("drumValues").getArray())
      for (int index = 0;
           index < values->size() && index < cydaw::engine::drum::valueCount;
           ++index) {
        const auto value =
            static_cast<float>(static_cast<double>((*values)[index]));
        plan.drumValues[static_cast<std::size_t>(index)] =
            cydaw::engine::drum::clampValue(index, value);
      }
  }
  if (const auto *values = root->getProperty("samplerValues").getArray())
    for (int index = 0; index < values->size() &&
                        index < static_cast<int>(plan.samplerValues.size());
         ++index) {
      auto value = static_cast<float>(static_cast<double>((*values)[index]));
      if (!std::isfinite(value))
        continue;
      if (index < static_cast<int>(cydaw::engine::samplerSettingCount)) {
        const auto &setting =
            cydaw::engine::samplerSettings[static_cast<std::size_t>(index)];
        value = cydaw::base::jlimit(setting.minimum, setting.maximum, value);
      }
      plan.samplerValues[static_cast<std::size_t>(index)] = value;
    }
  if (const auto *controllers =
          root->getProperty("samplerControllers").getDynamicObject())
    for (const auto &property : controllers->getProperties()) {
      const auto number = property.name.toString().getIntValue();
      const auto value =
          static_cast<float>(static_cast<double>(property.value));
      if (number >= 0 && number < 128 && std::isfinite(value))
        plan.samplerControllers[static_cast<std::size_t>(number)] =
            cydaw::base::jlimit(0.0f, 127.0f, value);
    }
  if (const auto *values = root->getProperty("soundModelValues").getArray())
    for (int index = 0; index < values->size() &&
                        index < static_cast<int>(plan.soundModelValues.size());
         ++index) {
      auto value = static_cast<float>(static_cast<double>((*values)[index]));
      if (!std::isfinite(value))
        continue;
      if (index < static_cast<int>(cydaw::engine::soundModelSettingCount)) {
        const auto &setting =
            cydaw::engine::soundModelSettings[static_cast<std::size_t>(index)];
        value = cydaw::base::jlimit(setting.minimum, setting.maximum, value);
      }
      plan.soundModelValues[static_cast<std::size_t>(index)] = value;
    }
  if (root->hasProperty(cydaw::base::Identifier("wavetablePosition")))
    plan.wavetablePosition = static_cast<float>(
        static_cast<double>(root->getProperty("wavetablePosition")));
  if (root->hasProperty(cydaw::base::Identifier("wavetableDetune")))
    plan.wavetableDetuneCents = static_cast<float>(
        static_cast<double>(root->getProperty("wavetableDetune")));
  if (root->hasProperty(cydaw::base::Identifier("stringDamping")))
    plan.stringDamping = static_cast<float>(
        static_cast<double>(root->getProperty("stringDamping")));
  if (root->hasProperty(cydaw::base::Identifier("stringDecay")))
    plan.stringDecaySeconds = static_cast<float>(
        static_cast<double>(root->getProperty("stringDecay")));
#define CYDAW_READ_PLAN(name)                                                  \
  plan.name = static_cast<float>(static_cast<double>(root->getProperty(#name)))
  CYDAW_READ_PLAN(oscillatorLevel);
  CYDAW_READ_PLAN(attackSeconds);
  CYDAW_READ_PLAN(decaySeconds);
  CYDAW_READ_PLAN(sustainLevel);
  CYDAW_READ_PLAN(releaseSeconds);
  CYDAW_READ_PLAN(filterCutoffHz);
  if (root->hasProperty("filterResonance"))
    CYDAW_READ_PLAN(filterResonance);
  CYDAW_READ_PLAN(velocityScale);
#undef CYDAW_READ_PLAN
  plan.transposeSemitones =
      static_cast<int>(root->getProperty("transposeSemitones"));
  plan.minimumMidiNote = static_cast<int>(root->getProperty("minimumMidiNote"));
  plan.maximumMidiNote = static_cast<int>(root->getProperty("maximumMidiNote"));
  error.clear();
  // The samples its card's controllers reach, asked for now.
  if (plan.sampler != nullptr)
    SampleStore::instance().want(
        *plan.sampler,
        {&plan.samplerControllers, nullptr,
         cydaw::engine::samplerValue(
             plan.samplerValues, cydaw::engine::SamplerSetting::articulation)});
  return sanitisePolySynthGraphPlan(plan);
}

} // namespace cydaw::app
