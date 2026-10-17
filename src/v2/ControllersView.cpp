#include "ControllersImpl.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Studio.h"
#include "Theme.h"
#include "UiState.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <cmath>

// The controllers' window: RAW devices on the left, the virtual
// controllers made from them in the middle, what they move on the right,
// the monitor under them. And learn mode over the whole interface.

namespace cydaw::v2 {
namespace {

using ui::dp;
using ui::L;
using Kind = control::ModifierKind;

double secondsNow()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

const char* kindLabel(const control::ControlKind kind)
{
    switch (kind) {
    case control::ControlKind::knob: return L("Potard", "Knob");
    case control::ControlKind::fader: return L("Fader", "Fader");
    case control::ControlKind::encoder: return L("Encodeur", "Encoder");
    case control::ControlKind::button: return L("Bouton", "Button");
    case control::ControlKind::toggle: return L("Interrupteur", "Toggle");
    case control::ControlKind::pad: return L("Pad", "Pad");
    case control::ControlKind::axis: return L("Axe", "Axis");
    }
    return "";
}

const char* modifierLabel(const Kind kind)
{
    switch (kind) {
    case Kind::range: return L("Plage d'entrée", "Input range");
    case Kind::invert: return L("Inverser", "Invert");
    case Kind::curve: return L("Courbe", "Curve");
    case Kind::relative: return L("Relatif (encodeur)", "Relative (encoder)");
    case Kind::toggle: return L("Bascule", "Toggle");
    case Kind::threshold: return L("Seuil (bouton)", "Threshold (button)");
    case Kind::deadzone: return L("Zone morte", "Dead zone");
    case Kind::steps: return L("Pas", "Steps");
    case Kind::scale: return L("Plage de sortie", "Output range");
    }
    return "";
}

const char* modifierHelp(const Kind kind)
{
    switch (kind) {
    case Kind::range: return L("Seule cette partie de la course du contrôle compte, étalée de 0 à 1.",
                               "Only this part of the control's travel counts, spread from 0 to 1.");
    case Kind::invert: return L("Le haut devient le bas.", "Top becomes bottom.");
    case Kind::curve: return L("Négatif : lent au début ; positif : rapide au début.", "Negative: slow at first; positive: fast at first.");
    case Kind::relative: return L("Un encodeur sans fin : chaque cran avance ou recule depuis la valeur actuelle.",
                                  "An endless encoder: each tick moves up or down from where the value is.");
    case Kind::toggle: return L("Un appui allume, le suivant éteint.", "One press turns on, the next turns off.");
    case Kind::threshold: return L("Au-dessus du seuil : 1, en dessous : 0.", "Over the threshold: 1, under it: 0.");
    case Kind::deadzone: return L("Un stick ou une pédale : le milieu reste immobile.", "A stick or a pedal: the middle stays still.");
    case Kind::steps: return L("La valeur se pose sur ce nombre de crans.", "The value lands on this many steps.");
    case Kind::scale: return L("Le contrôle ne couvre que cette partie de la cible.", "The control only covers this part of the target.");
    }
    return "";
}

const char* rawKindLabel(const Controllers::Impl::RawKind kind)
{
    using RawKind = Controllers::Impl::RawKind;
    switch (kind) {
    case RawKind::midiIn: return L("Entrées MIDI", "MIDI inputs");
    case RawKind::midiOut: return L("Sorties MIDI", "MIDI outputs");
    case RawKind::osc: return "OSC";
    case RawKind::plugin: return L("Plug-ins d'application", "Application plug-ins");
    }
    return "";
}

// A small light: lit a moment after a message.
void activityDot(const double activity)
{
    const auto& p = theme::palette();
    const auto lit = secondsNow() - activity < 0.25;
    const auto at = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + dp(6.0f), at.y + size * 0.5f), dp(3.5f), lit ? p.play : p.bg4, 12);
    ImGui::Dummy(ImVec2(dp(12.0f), size));
}

bool inputText(const char* id, std::string& value, const float width)
{
    std::array<char, 256> buffer {};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

} // namespace

// ---- The toolbar's button ------------------------------------------------------------------------

void Controllers::Impl::drawButton(bool& visible)
{
    const auto& p = theme::palette();
    if (learnMode)
        ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(p.accent));
    if (ImGui::Button(learnMode ? L("CTRL · apprendre", "CTRL · learn") : "CTRL"))
        visible = !visible;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        learnMode = !learnMode;
        ui::setLearnMode(learnMode);
        learnTarget.clear();
    }
    if (learnMode)
        ImGui::PopStyleColor();
    ui::tooltip(L("Contrôleurs MIDI / OSC (clic droit ou Ctrl+M : apprendre)", "MIDI / OSC controllers (right click or Ctrl+M: learn)"));
}

// ---- The window ---------------------------------------------------------------------------------

void Controllers::Impl::draw(bool& visible)
{
    if (!visible)
        return;
    const auto& p = theme::palette();
    ImGui::SetNextWindowSize(ImVec2(dp(1240.0f), dp(760.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(L("Contrôleurs MIDI / OSC###controllers", "MIDI / OSC controllers###controllers"), &visible)) {
        ImGui::End();
        return;
    }
    auto learning = learnMode;
    if (ui::toggle(L("Apprendre", "Learn"), &learning, p.accent, ImVec2(dp(96.0f), 0.0f),
                   L("Cliquez une commande de CyDAW, puis bougez un contrôle (Ctrl+M)",
                     "Click a CyDAW control, then move a control (Ctrl+M)"))) {
        learnMode = learning;
        ui::setLearnMode(learnMode);
        learnTarget.clear();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (learnStatus.isNotEmpty())
        ImGui::TextColored(theme::vec(p.play), "%s", learnStatus.toRawUTF8());
    else
        ImGui::TextDisabled("%s", L("Appareils RAW → appareils virtuels (modifications) → assignations dans CyDAW, dans les deux sens.",
                                    "RAW devices → virtual devices (modifiers) → assignments in CyDAW, both ways."));
    const auto monitorHeight = dp(190.0f);
    const auto top = std::max(dp(220.0f), ImGui::GetContentRegionAvail().y - monitorHeight - ImGui::GetStyle().ItemSpacing.y);
    if (ImGui::BeginTable("##controllersColumns", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, top))) {
        ImGui::TableSetupColumn("raw", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("virtual", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("assignments", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##raw", ImVec2(0.0f, top - dp(4.0f))))
            drawRaw();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##virtual", ImVec2(0.0f, top - dp(4.0f))))
            drawVirtual();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##assignments", ImVec2(0.0f, top - dp(4.0f))))
            drawAssignments();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if (ImGui::BeginChild("##monitor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        drawMonitor();
    ImGui::EndChild();
    ImGui::End();
}

void Controllers::Impl::drawRaw()
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Appareils RAW", "RAW devices"));
    for (const auto kind : { RawKind::midiIn, RawKind::midiOut, RawKind::osc, RawKind::plugin }) {
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textDim), "%s", rawKindLabel(kind));
        auto any = false;
        for (auto& owned : raw) {
            auto& device = *owned;
            if (device.kind != kind)
                continue;
            any = true;
            ImGui::PushID(device.id.c_str());
            activityDot(device.activity);
            ImGui::SameLine(0.0f, dp(2.0f));
            auto enabled = device.enabled;
            if (ImGui::Checkbox("##on", &enabled)) {
                device.enabled = enabled;
                applyRaw(device);
                changed(true, false);
            }
            ui::tooltip(L("Ouvert ou fermé", "Open or closed"));
            ImGui::SameLine();
            ImGui::BeginDisabled(!device.present);
            auto label = device.name;
            if (!device.present)
                label << (kind == RawKind::osc ? ui::Ls(" (fermé)", " (closed)") : ui::Ls(" (débranché)", " (unplugged)"));
            if (ImGui::Selectable(label.toRawUTF8(), selectedRaw == device.id, 0, ImVec2(ImGui::GetContentRegionAvail().x - dp(60.0f), 0.0f)))
                selectedRaw = device.id;
            ImGui::EndDisabled();
            if (kind == RawKind::midiIn) {
                ImGui::SameLine();
                auto notes = device.notes;
                if (ui::toggle(L("Notes", "Notes"), &notes, p.play, ImVec2(dp(52.0f), 0.0f),
                               L("Ses notes et contrôleurs jouent aussi l'instrument sélectionné (sauf ceux qu'une assignation prend)",
                                 "Its notes and controllers also play the selected instrument (but those an assignment takes)"))) {
                    device.notes = notes;
                    changed(true, false);
                }
            }
            if (device.error.isNotEmpty())
                ImGui::TextColored(theme::vec(p.record), "  %s", device.error.toRawUTF8());
            // An OSC socket's settings, when picked.
            if (kind == RawKind::osc && selectedRaw == device.id) {
                ImGui::Indent(dp(16.0f));
                auto edited = false;
                auto port = device.listenPort;
                ImGui::SetNextItemWidth(dp(90.0f));
                edited |= ImGui::InputInt(L("Écoute (port UDP)", "Listen (UDP port)"), &port, 0);
                std::string hostName = device.sendHost.toStdString();
                if (inputText(L("##sendHost", "##sendHost"), hostName, dp(140.0f)))
                    device.sendHost = juce::String(hostName), edited = true;
                ImGui::SameLine();
                auto sendPort = device.sendPort;
                ImGui::SetNextItemWidth(dp(90.0f));
                edited |= ImGui::InputInt(L("Réponse (hôte, port)", "Answer (host, port)"), &sendPort, 0);
                auto own = device.cydawAddresses;
                if (ImGui::Checkbox(L("Adresses /cydaw/… directes", "Direct /cydaw/… addresses"), &own))
                    device.cydawAddresses = own, edited = true;
                ui::tooltip(L("/cydaw/transport/play, /cydaw/track/selected/volume 0.8, /cydaw/dj/A/high… sans contrôle virtuel",
                              "/cydaw/transport/play, /cydaw/track/selected/volume 0.8, /cydaw/dj/A/high… without a virtual control"));
                if (edited) {
                    device.listenPort = std::clamp(port, 0, 65'535);
                    device.sendPort = std::clamp(sendPort, 0, 65'535);
                }
                if (ImGui::Button(L("Appliquer", "Apply"))) {
                    applyRaw(device);
                    changed(true, false);
                }
                ImGui::SameLine();
                if (ImGui::Button(L("Retirer", "Remove"))) {
                    ImGui::Unindent(dp(16.0f));
                    ImGui::PopID();
                    removeRaw(device.id);
                    return;
                }
                ImGui::Unindent(dp(16.0f));
            }
            ImGui::PopID();
        }
        if (!any)
            ImGui::TextDisabled("  %s", kind == RawKind::plugin ? L("aucun", "none") : L("aucune", "none"));
        if (kind == RawKind::osc) {
            ImGui::PushID("newOsc");
            std::string name = newOscName.toStdString();
            if (inputText("##name", name, dp(110.0f)))
                newOscName = juce::String(name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dp(80.0f));
            ImGui::InputInt("##port", &newOscPort, 0);
            ImGui::SameLine();
            if (ImGui::Button(L("+ Point OSC", "+ OSC endpoint"))) {
                selectedRaw = addOsc(newOscName, std::clamp(newOscPort, 1, 65'535), "127.0.0.1", 0).id;
                ++newOscPort;
            }
            ui::tooltip(L("Écoute ce port UDP, sur cette machine et le réseau local", "Listens on this UDP port, on this machine and the local network"));
            ImGui::PopID();
        }
        if (kind == RawKind::plugin) {
            for (const auto& plugin : plugins)
                ImGui::TextColored(theme::vec(plugin->started ? p.textDim : p.record), "  %s%s%s", plugin->name.toRawUTF8(),
                                   plugin->error.isNotEmpty() ? " · " : "", plugin->error.toRawUTF8());
            ImGui::TextDisabled("%s", L("  .cyctl dans ControlPlugins (à côté de CyDAW) ou Documents/CyDAW/Control Plugins",
                                        "  .cyctl in ControlPlugins (beside CyDAW) or Documents/CyDAW/Control Plugins"));
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(L("Actualiser", "Refresh")))
        scanMidi();
    if (const auto* device = rawDevice(selectedRaw); device != nullptr && device->kind != RawKind::midiOut) {
        ImGui::SameLine();
        if (ImGui::Button(L("Appareil virtuel depuis celui-ci", "Virtual device from this one"))) {
            selectedDevice = virtualDeviceFor(*device);
            changed(true, false);
        }
    }
}

void Controllers::Impl::drawVirtual()
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Appareils virtuels", "Virtual devices"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - dp(24.0f));
    if (ui::iconButton(icon::add, L("Nouvel appareil virtuel", "New virtual device")))
        selectedDevice = addVirtualDevice({});
    for (const auto& device : router.devices) {
        ImGui::PushID(device.id.c_str());
        auto label = juce::String(device.name) + juce::String::fromUTF8("  \xc2\xb7  ") + juce::String(static_cast<int>(device.controls.size()))
                     + ui::Ls(" contrôle(s)", " control(s)");
        if (ImGui::Selectable(label.toRawUTF8(), selectedDevice == device.id))
            selectedDevice = device.id;
        ImGui::PopID();
    }
    auto* device = router.findDevice(selectedDevice);
    if (device == nullptr) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("Un appareil virtuel se crée seul en apprenant, ou avec + ; il regroupe des contrôles venus des appareils RAW.",
                                    "A virtual device is made by learning, or with +; it gathers controls fed by RAW devices."));
        return;
    }
    ImGui::Separator();
    ImGui::PushID(device->id.c_str());
    if (inputText("##deviceName", device->name, dp(220.0f)))
        changed(true, false);
    ImGui::SameLine();
    if (ImGui::Button(L("+ Contrôle", "+ Control")))
        selectedControl = addControl(device->id, {}, control::ControlKind::knob, {}, {});
    ImGui::SameLine();
    if (ImGui::Button(L("Supprimer l'appareil", "Delete the device"))) {
        ImGui::PopID();
        removeVirtualDevice(device->id);
        selectedDevice.clear();
        return;
    }
    if (ImGui::BeginTable("##controls", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, dp(170.0f)))) {
        ImGui::TableSetupColumn(L("Contrôle", "Control"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(L("Type", "Kind"), ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn(L("Source", "Source"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(L("Valeur", "Value"), ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& control : device->controls) {
            ImGui::PushID(control.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(control.name.c_str(), selectedControl == control.id, ImGuiSelectableFlags_SpanAllColumns))
                selectedControl = control.id;
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", kindLabel(control.kind));
            ImGui::TableNextColumn();
            const auto* source = rawDevice(control.sourceDevice);
            ImGui::TextDisabled("%s · %s", source != nullptr ? source->name.toRawUTF8() : L("aucune", "none"), control.sourceAddress.c_str());
            ImGui::TableNextColumn();
            ImGui::ProgressBar(static_cast<float>(control.value), ImVec2(-1.0f, dp(10.0f)), "");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    auto* control = router.findControl(device->id, selectedControl);
    if (control == nullptr) {
        ImGui::PopID();
        return;
    }
    // The control: its source, its way back, its modifiers.
    ImGui::Separator();
    ImGui::PushID(control->id.c_str());
    if (inputText("##controlName", control->name, dp(200.0f)))
        changed(true, false);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(130.0f));
    if (ImGui::BeginCombo("##kind", kindLabel(control->kind))) {
        for (int index = 0; index < control::controlKindCount; ++index) {
            const auto kind = static_cast<control::ControlKind>(index);
            if (ImGui::Selectable(kindLabel(kind), kind == control->kind)) {
                control->kind = kind;
                control->chain = control::defaultChain(kind);
                changed(true, false);
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Le type remet la chaîne de modifications qui lui va", "The kind sets the chain of modifiers that suits it"));
    ImGui::SameLine();
    if (ImGui::Button(L("Supprimer", "Delete"))) {
        ImGui::PopID();
        ImGui::PopID();
        removeControl(device->id, control->id);
        selectedControl.clear();
        return;
    }
    // Source: a raw input and one of its addresses.
    const auto deviceCombo = [&](const char* id, std::string& value, const bool outputs) {
        const auto* current = rawDevice(value);
        ImGui::SetNextItemWidth(dp(150.0f));
        auto changedHere = false;
        if (ImGui::BeginCombo(id, current != nullptr ? current->name.toRawUTF8() : L("aucun", "none"))) {
            if (ImGui::Selectable(L("aucun", "none"), value.empty()))
                value.clear(), changedHere = true;
            for (const auto& owned : raw) {
                const auto isOutput = owned->kind == RawKind::midiOut || owned->kind == RawKind::osc || owned->kind == RawKind::plugin;
                const auto isInput = owned->kind == RawKind::midiIn || owned->kind == RawKind::osc || owned->kind == RawKind::plugin;
                if (outputs ? !isOutput : !isInput)
                    continue;
                if (ImGui::Selectable((owned->name + "##" + juce::String(owned->id)).toRawUTF8(), owned->id == value))
                    value = owned->id, changedHere = true;
            }
            ImGui::EndCombo();
        }
        return changedHere;
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Source", "Source"));
    ImGui::SameLine(dp(70.0f));
    if (deviceCombo("##source", control->sourceDevice, false))
        changed(true, false);
    ImGui::SameLine();
    if (inputText("##sourceAddress", control->sourceAddress, dp(100.0f)))
        changed(true, false);
    ImGui::SameLine();
    const auto listening = learnSource && learnSource->first == device->id && learnSource->second == control->id;
    if (ImGui::Button(listening ? L("Bougez-le…", "Move it…") : L("Apprendre", "Learn")))
        learnSource = listening ? std::nullopt : std::optional<std::pair<std::string, std::string>> { { device->id, control->id } };
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Retour", "Feedback"));
    ImGui::SameLine(dp(70.0f));
    if (deviceCombo("##feedback", control->feedbackDevice, true))
        changed(true, false);
    ImGui::SameLine();
    if (inputText("##feedbackAddress", control->feedbackAddress, dp(100.0f)))
        changed(true, false);
    ui::tooltip(L("Où la valeur repart : LED, fader motorisé, écran d'une tablette", "Where the value goes back: LEDs, a motorised fader, a tablet"));
    // The chain, raw to virtual.
    ImGui::Spacing();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Modifications (RAW → virtuel, et retour)", "Modifiers (RAW → virtual, and back)"));
    for (std::size_t index = 0; index < control->chain.size(); ++index) {
        auto& modifier = control->chain[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Bullet();
        ImGui::TextUnformatted(modifierLabel(modifier.kind));
        ui::tooltip(modifierHelp(modifier.kind));
        ImGui::SameLine(dp(170.0f));
        auto a = static_cast<float>(modifier.a);
        auto b = static_cast<float>(modifier.b);
        auto edited = false;
        ImGui::SetNextItemWidth(dp(80.0f));
        switch (modifier.kind) {
        case Kind::range:
        case Kind::scale:
            edited |= ImGui::DragFloat("##a", &a, 0.005f, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dp(80.0f));
            edited |= ImGui::DragFloat("##b", &b, 0.005f, 0.0f, 1.0f, "%.2f");
            break;
        case Kind::curve:
            edited |= ImGui::DragFloat("##a", &a, 0.01f, -1.0f, 1.0f, "%+.2f");
            break;
        case Kind::relative: {
            static const char* encodings[3] { "64 ±", "64 =", "±64" };
            auto encoding = std::clamp(static_cast<int>(std::lround(a)), 0, 2);
            if (ImGui::Combo("##a", &encoding, encodings, 3))
                a = static_cast<float>(encoding), edited = true;
            ui::tooltip(L("Codage des crans : complément à deux, décalé de 64, bit de signe", "Tick encoding: two's complement, offset from 64, sign bit"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dp(80.0f));
            edited |= ImGui::DragFloat("##b", &b, 0.0005f, 0.0005f, 0.25f, "%.4f");
            ui::tooltip(L("Ce qu'un cran avance", "How far a tick moves"));
            break;
        }
        case Kind::threshold:
        case Kind::deadzone:
            edited |= ImGui::DragFloat("##a", &a, 0.005f, 0.0f, modifier.kind == Kind::deadzone ? 0.45f : 1.0f, "%.2f");
            break;
        case Kind::steps:
            edited |= ImGui::DragFloat("##a", &a, 0.1f, 2.0f, 128.0f, "%.0f");
            break;
        case Kind::invert:
        case Kind::toggle:
            ImGui::Dummy(ImVec2(dp(80.0f), 0.0f));
            break;
        }
        if (edited) {
            modifier.a = a;
            modifier.b = b;
            changed(true, false);
        }
        ImGui::SameLine();
        auto removed = false;
        if (ImGui::SmallButton("x"))
            removed = true;
        if (index > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("^")) {
                std::swap(control->chain[index], control->chain[index - 1]);
                changed(true, false);
            }
        }
        ImGui::PopID();
        if (removed) {
            control->chain.erase(control->chain.begin() + static_cast<std::ptrdiff_t>(index));
            changed(true, false);
            break;
        }
    }
    if (ImGui::BeginCombo("##addModifier", L("+ Modification", "+ Modifier"), ImGuiComboFlags_NoArrowButton)) {
        for (int index = 0; index < control::modifierKindCount; ++index) {
            const auto kind = static_cast<Kind>(index);
            if (ImGui::Selectable(modifierLabel(kind))) {
                control->chain.push_back(control::defaultModifier(kind));
                changed(true, false);
            }
            ui::tooltip(modifierHelp(kind));
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ImGui::PopID();
}

void Controllers::Impl::drawAssignments()
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Assignations", "Assignments"));
    if (router.assignments.empty())
        ImGui::TextDisabled("%s", L("Aucune. « Apprendre », puis cliquez une commande de CyDAW et bougez un contrôle.",
                                    "None. « Learn », then click a CyDAW control and move a control."));
    std::optional<std::size_t> removed;
    if (!router.assignments.empty()
        && ImGui::BeginTable("##assigned", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                             ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.6f))) {
        ImGui::TableSetupColumn(L("Contrôle", "Control"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Cible", "Target"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(L("Plage", "Range"), ImGuiTableColumnFlags_WidthFixed, dp(110.0f));
        ImGui::TableSetupColumn(L("Reprise", "Pickup"), ImGuiTableColumnFlags_WidthFixed, dp(50.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, dp(24.0f));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < router.assignments.size(); ++index) {
            auto& assignment = router.assignments[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto* device = router.findDevice(assignment.device);
            const auto* control = router.findControl(assignment.device, assignment.control);
            ImGui::TextUnformatted(((device != nullptr ? juce::String(device->name) : juce::String("?")) + juce::String::fromUTF8(" \xc2\xb7 ")
                                    + (control != nullptr ? juce::String(control->name) : juce::String("?"))).toRawUTF8());
            ImGui::TableNextColumn();
            const auto info = targets.describe(assignment.target);
            if (info)
                ImGui::TextUnformatted(info->label.toRawUTF8());
            else
                ImGui::TextColored(theme::vec(p.record), "%s", assignment.target.c_str());
            ui::tooltip((juce::String(assignment.target) + (assignment.project ? ui::Ls("  ·  gardée avec le projet", "  ·  kept with the project")
                                                                              : ui::Ls("  ·  pour tous les projets", "  ·  for every project")))
                            .toRawUTF8());
            ImGui::TableNextColumn();
            auto low = static_cast<float>(assignment.minimum);
            auto high = static_cast<float>(assignment.maximum);
            ImGui::SetNextItemWidth(dp(50.0f));
            auto edited = ImGui::DragFloat("##low", &low, 0.005f, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine(0.0f, dp(2.0f));
            ImGui::SetNextItemWidth(dp(50.0f));
            edited |= ImGui::DragFloat("##high", &high, 0.005f, 0.0f, 1.0f, "%.2f");
            ImGui::TableNextColumn();
            auto takeover = assignment.takeover;
            if (ImGui::Checkbox("##takeover", &takeover)) {
                assignment.takeover = takeover;
                assignment.caught = !takeover;
                edited = true;
            }
            ui::tooltip(L("Reprise : le contrôle ne bouge la cible qu'une fois arrivé à sa valeur (pas de saut)",
                          "Pickup: the control moves the target only once it has reached its value (no jump)"));
            if (edited) {
                assignment.minimum = low;
                assignment.maximum = high;
                changed(!assignment.project, assignment.project);
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x"))
                removed = index;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed)
        unassign(*removed);
    // By hand: the picked control onto a target found by name.
    const auto* control = router.findControl(selectedDevice, selectedControl);
    ImGui::Spacing();
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       control != nullptr ? (ui::Ls("Assigner « ", "Assign « ") + juce::String(control->name) + ui::Ls(" » :", " »:")).toRawUTF8()
                                          : L("Choisissez un contrôle virtuel pour l'assigner à la main.", "Pick a virtual control to assign it by hand."));
    if (control == nullptr)
        return;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##targetFilter", L("Chercher une cible : volume, cutoff, transport…", "Find a target: volume, cutoff, transport…"),
                             targetFilter, sizeof(targetFilter));
    if (ImGui::BeginChild("##targetList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        const auto filter = juce::String::fromUTF8(targetFilter);
        auto shown = 0;
        for (const auto& key : targets.list()) {
            const auto info = targets.describe(key);
            if (!info || (filter.isNotEmpty() && !info->label.containsIgnoreCase(filter) && !juce::String(key).containsIgnoreCase(filter)))
                continue;
            if (++shown > 200)
                break;
            if (ImGui::Selectable((info->label + "##" + juce::String(key)).toRawUTF8()))
                assign(selectedDevice, selectedControl, key, false);
        }
    }
    ImGui::EndChild();
}

void Controllers::Impl::drawMonitor()
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Moniteur", "Monitor"));
    ImGui::SameLine();
    ImGui::Checkbox(L("Pause", "Pause"), &monitorPaused);
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Effacer", "Clear")))
        monitor.clear();
    if (ImGui::BeginTable("##seen", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, dp(60.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, dp(20.0f));
        ImGui::TableSetupColumn(L("Appareil", "Device"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Adresse", "Address"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Valeur", "Value"), ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        const auto now = secondsNow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(monitor.size()));
        while (clipper.Step())
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& seen = monitor[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-%.1fs", now - seen.time);
                ImGui::TableNextColumn();
                ImGui::TextColored(theme::vec(seen.out ? p.accent : p.play), "%s", seen.out ? "<" : ">");
                ImGui::TableNextColumn();
                const auto* device = rawDevice(seen.device);
                ImGui::TextUnformatted(device != nullptr ? device->name.toRawUTF8() : seen.device.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(seen.message.address.c_str());
                ImGui::TableNextColumn();
                const auto midi = seen.message.address.find('/') != std::string::npos && seen.message.address[0] != '/';
                if (midi)
                    ImGui::Text("%d  (%.3f)", control::midi7(seen.message.value), seen.message.value);
                else
                    ImGui::Text("%.3f", seen.message.value);
            }
        if (!monitorPaused && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - dp(20.0f))
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }
}

// ---- Learn mode over the whole interface --------------------------------------------------------------

void Controllers::Impl::drawOverlay()
{
    const auto spots = ui::takeLearnSpots();
    if (!learnMode)
        return;
    const auto& p = theme::palette();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (!learnTarget.empty())
            learnTarget.clear();
        else {
            learnMode = false;
            ui::setLearnMode(false);
        }
        host.wake(3);
        return;
    }
    // What each target is moved by now.
    std::map<std::string, juce::String> assigned;
    for (const auto& assignment : router.assignments)
        if (const auto* control = router.findControl(assignment.device, assignment.control))
            assigned[assignment.target] = juce::String(control->name);
    std::map<ImGuiID, std::vector<const ui::LearnSpot*>> byViewport;
    byViewport[ImGui::GetMainViewport()->ID];
    for (const auto& spot : spots)
        byViewport[spot.viewport].push_back(&spot);
    const auto pulse = 0.5f + 0.5f * std::sin(static_cast<float>(secondsNow()) * 6.0f);
    for (const auto& [id, list] : byViewport) {
        auto* viewport = ImGui::FindViewportByID(id);
        if (viewport == nullptr)
            continue;
        ImGui::SetNextWindowViewport(id);
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const auto name = "##learnGlass" + std::to_string(id);
        ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav
                         | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::PopStyleVar(2);
        // A glass over everything: the interface under it does not move.
        ImGui::SetCursorScreenPos(viewport->Pos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##glass", viewport->Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const auto clickedLeft = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const auto clickedRight = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                            IM_COL32(0, 0, 0, 70));
        const auto mouse = ImGui::GetIO().MousePos;
        const ui::LearnSpot* hovered = nullptr;
        for (const auto* spot : list) {
            const auto over = mouse.x >= spot->min.x && mouse.x <= spot->max.x && mouse.y >= spot->min.y && mouse.y <= spot->max.y;
            if (over)
                hovered = spot;
            const auto picked = spot->key == learnTarget;
            const auto has = assigned.contains(spot->key);
            const auto colour = picked ? theme::alpha(p.accent, 0.35f + 0.35f * pulse)
                                : has  ? theme::alpha(p.play, over ? 0.45f : 0.28f)
                                       : theme::alpha(p.accent, over ? 0.40f : 0.18f);
            draw->AddRectFilled(spot->min, spot->max, colour, dp(3.0f));
            draw->AddRect(spot->min, spot->max, picked ? p.accent : has ? p.play : theme::alpha(p.accent, 0.7f), dp(3.0f),
                          dp(picked ? 2.0f : 1.0f));
            if (has) {
                const auto& text = assigned[spot->key];
                ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.75f);
                draw->AddText(ImVec2(spot->min.x + dp(2.0f), spot->min.y), p.text, text.toRawUTF8());
                ImGui::PopFont();
            }
        }
        if (hovered != nullptr) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(hovered->label.c_str());
            if (assigned.contains(hovered->key))
                ImGui::TextColored(theme::vec(p.play), "← %s", assigned[hovered->key].toRawUTF8());
            ImGui::TextDisabled("%s", L("Clic : apprendre · clic droit : retirer l'assignation", "Click: learn · right click: remove the assignment"));
            ImGui::EndTooltip();
            if (clickedLeft) {
                learnTarget = hovered->key;
                const auto info = targets.describe(hovered->key);
                learnTargetLabel = info ? info->label : juce::String(hovered->label);
                learnStatus.clear();
            }
            if (clickedRight)
                for (std::size_t index = router.assignments.size(); index-- > 0;)
                    if (router.assignments[index].target == hovered->key)
                        unassign(index);
        }
        // The banner, on the main window.
        if (id == ImGui::GetMainViewport()->ID) {
            const auto message = !learnTarget.empty()
                                     ? ui::Ls("« ", "« ") + learnTargetLabel + ui::Ls(" » : bougez un contrôle de votre appareil…", " »: move a control on your device…")
                                 : learnStatus.isNotEmpty() ? learnStatus + ui::Ls("  ·  cliquez la commande suivante", "  ·  click the next control")
                                                            : ui::Ls("Apprendre : cliquez une commande de CyDAW, puis bougez un contrôle. Échap pour terminer.",
                                                                     "Learn: click a CyDAW control, then move a control. Escape to finish.");
            const auto size = ImGui::CalcTextSize(message.toRawUTF8());
            const auto width = size.x + dp(130.0f);
            const ImVec2 at(viewport->Pos.x + (viewport->Size.x - width) * 0.5f, viewport->Pos.y + dp(48.0f));
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + dp(34.0f)), theme::alpha(p.bg1, 0.96f), dp(6.0f));
            draw->AddRect(at, ImVec2(at.x + width, at.y + dp(34.0f)), p.accent, dp(6.0f), dp(1.5f));
            draw->AddText(ImVec2(at.x + dp(12.0f), at.y + (dp(34.0f) - size.y) * 0.5f), p.text, message.toRawUTF8());
            ImGui::SetCursorScreenPos(ImVec2(at.x + width - dp(104.0f), at.y + dp(5.0f)));
            if (ImGui::Button(L("Terminer", "Done"), ImVec2(dp(94.0f), dp(24.0f)))) {
                learnMode = false;
                learnTarget.clear();
                ui::setLearnMode(false);
            }
        }
        ImGui::End();
    }
    host.wake(2);
}

} // namespace cydaw::v2
