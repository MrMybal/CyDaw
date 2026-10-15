#include "CardFiles.h"

#include "Widgets.h"

#include <imgui.h>

#include <cmath>
#include <map>
#include <numbers>

namespace cydaw::v2 {

namespace {

std::unique_ptr<juce::FileChooser> chooser;

// A response's curve, made once (it never changes: a new one is a new object).
const std::vector<float>& curveOf(const engine::ImpulseResponseRef& response)
{
    static std::map<const engine::ImpulseResponse*, std::pair<engine::ImpulseResponseRef, std::vector<float>>> made;
    if (const auto found = made.find(response.get()); found != made.end())
        return found->second.second;
    if (made.size() > 16)
        made.clear();
    std::vector<float> curve(static_cast<std::size_t>(visual::cardFileCurvePoints), 0.0f);
    const auto& samples = response->channels.front();
    // Its first 16384 samples, a direct sum at each frequency.
    const auto count = std::min<std::size_t>(samples.size(), 16'384);
    for (std::size_t point = 0; point < curve.size(); ++point) {
        const auto hz = 20.0 * std::pow(1'000.0, static_cast<double>(point) / static_cast<double>(curve.size() - 1));
        const auto omega = 2.0 * std::numbers::pi * hz / response->sampleRate;
        auto real = 0.0;
        auto imaginary = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            real += samples[i] * std::cos(omega * static_cast<double>(i));
            imaginary -= samples[i] * std::sin(omega * static_cast<double>(i));
        }
        curve[point] = static_cast<float>(10.0 * std::log10(std::max(1.0e-20, real * real + imaginary * imaginary)));
    }
    // Its middle (200 Hz to 4 kHz) at 0 dB.
    auto sum = 0.0;
    auto n = 0;
    for (std::size_t point = 0; point < curve.size(); ++point) {
        const auto hz = 20.0 * std::pow(1'000.0, static_cast<double>(point) / static_cast<double>(curve.size() - 1));
        if (hz >= 200.0 && hz <= 4'000.0) {
            sum += curve[point];
            ++n;
        }
    }
    const auto middle = n > 0 ? static_cast<float>(sum / n) : 0.0f;
    for (auto& value : curve)
        value -= middle;
    return made.emplace(response.get(), std::make_pair(response, std::move(curve))).first->second.second;
}

} // namespace

CardFileScope::CardFileScope(DeviceHost& studio, UiState& state, const TrackRef& track, app::NodeGraphComponent& graph,
                             const std::uint64_t nodeId, const juce::String& typeKey)
{
    if (typeKey != "cab-sim" && typeKey != "empreinte")
        return;
    const auto room = typeKey == "empreinte";
    if (const auto response = graph.nodeImpulse(nodeId); response != nullptr && !response->channels.empty()) {
        file_.name = juce::String(response->name);
        file_.curveDb = &curveOf(response);
    }
    auto* studioPointer = &studio;
    auto* statePointer = &state;
    file_.load = [studioPointer, statePointer, track, nodeId, room] {
        chooser = std::make_unique<juce::FileChooser>(room ? ui::Ls("La réponse impulsionnelle d'un lieu", "A place's impulse response")
                                                           : ui::Ls("Une réponse impulsionnelle de baffle", "A cabinet impulse response"),
                                                      juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [studioPointer, statePointer, track, nodeId](const juce::FileChooser& picked) {
                                 const auto file = picked.getResult();
                                 if (file == juce::File())
                                     return;
                                 juce::String error;
                                 const auto done = studioPointer->loadCabImpulse(track, nodeId, file, error);
                                 statePointer->status = done ? ui::Ls("Réponse chargée : ", "Response loaded: ") + file.getFileName() : error;
                                 statePointer->statusIsError = !done;
                                 statePointer->statusTime = ImGui::GetTime();
                             });
    };
    file_.clear = [studioPointer, track, nodeId] { (void) studioPointer->clearCabImpulse(track, nodeId); };
    visual::setCardFile(&file_);
    shown_ = true;
}

CardFileScope::~CardFileScope()
{
    if (shown_)
        visual::setCardFile(nullptr);
}

} // namespace cydaw::v2
