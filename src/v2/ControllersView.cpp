#include "ControllersImpl.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Studio.h"
#include "Theme.h"
#include "UiState.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <cmath>

// The controllers' window, made around devices. A device is a piece of
// hardware CyDAW listens to (a MIDI keyboard, a gamepad, a tablet over OSC,
// the computer's keyboard): its input and the output its values go back
// to, then its rules, learnt by moving its controls one by one, each with
// its chain of modifiers and what it moves in CyDAW. The hardware CyDAW
// sees and every assignment have tabs of their own; the monitor shows what
// comes and goes. And learn mode over the whole interface.

namespace cydaw::v2 {
namespace {

using ui::dp;
using ui::L;
using Kind = control::ModifierKind;
using RawKind = Controllers::Impl::RawKind;

double secondsNow()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

juce::String middleDot()
{
    return juce::String::fromUTF8(" \xc2\xb7 ");
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

const char* rawKindLabel(const RawKind kind)
{
    switch (kind) {
    case RawKind::midiIn: return L("Entrées MIDI", "MIDI inputs");
    case RawKind::midiOut: return L("Sorties MIDI", "MIDI outputs");
    case RawKind::keyboard: return L("Cet ordinateur", "This computer");
    case RawKind::plugin: return L("Plug-ins d'application (manettes…)", "Application plug-ins (gamepads…)");
    case RawKind::osc: return L("OSC (réseau)", "OSC (network)");
    }
    return "";
}

// The order families are shown in.
int familyRank(const RawKind kind)
{
    switch (kind) {
    case RawKind::midiIn:
    case RawKind::midiOut: return 0;
    case RawKind::keyboard: return 1;
    case RawKind::plugin: return 2;
    case RawKind::osc: return 3;
    }
    return 4;
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

// A value as a thin bar, without a frame: the row's text keeps its line.
void valueBar(const double value)
{
    const auto& p = theme::palette();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto height = ImGui::GetTextLineHeight();
    const auto at = ImGui::GetCursorScreenPos();
    const auto top = at.y + height * 0.5f - dp(4.0f);
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + dp(8.0f)), p.bg4, dp(2.0f));
    list->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width * static_cast<float>(std::clamp(value, 0.0, 1.0)), top + dp(8.0f)), p.accent,
                        dp(2.0f));
    ImGui::Dummy(ImVec2(width, height));
}

// A label in the first column of a form.
void formLabel(const char* text, const float width)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(theme::palette().textDim), "%s", text);
    ImGui::SameLine(width);
}

// A line that pulses: something is waited for.
void waiting(const char* text)
{
    const auto& p = theme::palette();
    const auto pulse = 0.5f + 0.5f * std::sin(static_cast<float>(secondsNow()) * 6.0f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(theme::alpha(p.accent, 0.55f + 0.45f * pulse)), "%s", text);
    ImGui::PopTextWrapPos();
}

void centredLine(const char* text, const ImU32 colour)
{
    const auto width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    ImGui::TextColored(theme::vec(colour), "%s", text);
}

// The computer's keys: by name, and those a control can be.
ImGuiKey keyNamed(const std::string& name)
{
    static const auto names = [] {
        std::map<std::string, ImGuiKey> made;
        for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key)
            made[ImGui::GetKeyName(static_cast<ImGuiKey>(key))] = static_cast<ImGuiKey>(key);
        return made;
    }();
    const auto found = names.find(name);
    return found != names.end() ? found->second : ImGuiKey_None;
}

bool learnableKey(const ImGuiKey key)
{
    // Not the modifiers (shortcuts), not Escape (it stops learning).
    return key != ImGuiKey_Escape && !(key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper);
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
        pickTarget.reset();
    }
    if (learnMode)
        ImGui::PopStyleColor();
    ui::tooltip(L("Contrôleurs MIDI / OSC (clic droit ou Ctrl+M : apprendre)", "MIDI / OSC controllers (right click or Ctrl+M: learn)"));
}

// ---- The window ---------------------------------------------------------------------------------

void Controllers::Impl::draw(bool& visible)
{
    windowFocused = false;
    if (!visible)
        return;
    const auto& p = theme::palette();
    ImGui::SetNextWindowSize(ImVec2(dp(1240.0f), dp(780.0f)), ImGuiCond_FirstUseEver);
    if (std::exchange(centreWindow, false)) {
        // Over the main window, whatever the layout kept.
        const auto* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(main->ID);
        ImGui::SetNextWindowPos(ImVec2(main->WorkPos.x + main->WorkSize.x * 0.5f, main->WorkPos.y + main->WorkSize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(dp(1240.0f), main->WorkSize.x - dp(40.0f)), std::min(dp(780.0f), main->WorkSize.y - dp(40.0f))),
                                 ImGuiCond_Always);
    }
    if (!ImGui::Begin(L("Contrôleurs MIDI / OSC###controllers", "MIDI / OSC controllers###controllers"), &visible)) {
        ImGui::End();
        return;
    }
    windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (router.findDevice(selectedDevice) == nullptr) {
        selectedDevice = router.devices.empty() ? std::string() : router.devices.front().id;
        selectedControl.clear();
    }
    auto learning = learnMode;
    if (ui::toggle(L("Apprendre", "Learn"), &learning, p.accent, ImVec2(dp(96.0f), 0.0f),
                   L("Cliquez une commande de CyDAW, puis bougez un contrôle (Ctrl+M)",
                     "Click a CyDAW control, then move a control (Ctrl+M)"))) {
        learnMode = learning;
        ui::setLearnMode(learnMode);
        learnTarget.clear();
        pickTarget.reset();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (learnStatus.isNotEmpty())
        ImGui::TextColored(theme::vec(p.play), "%s", learnStatus.toRawUTF8());
    else
        ImGui::TextDisabled("%s", L("Un périphérique : son matériel, puis ses règles, apprises en bougeant ses contrôles.",
                                    "A device: its hardware, then its rules, learnt by moving its controls."));
    if (ImGui::BeginTabBar("##controllersTabs")) {
        const char* names[] { L("Périphériques###devices", "Devices###devices"), L("Matériel détecté###hardware", "Detected hardware###hardware"),
                              L("Toutes les assignations###assignments", "All assignments###assignments") };
        for (int index = 0; index < 3; ++index) {
            const auto flags = requestedTab == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (!ImGui::BeginTabItem(names[index], nullptr, flags))
                continue;
            tab = index;
            if (index == 0) {
                if (ImGui::BeginChild("##deviceList", ImVec2(dp(250.0f), 0.0f), ImGuiChildFlags_Borders))
                    drawDeviceList();
                ImGui::EndChild();
                ImGui::SameLine();
                if (ImGui::BeginChild("##device", ImVec2(0.0f, 0.0f))) {
                    if (auto* device = router.findDevice(selectedDevice)) {
                        drawDevice(*device);
                    } else {
                        // Nothing yet: what a device is, and the way to make one.
                        ImGui::Dummy(ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.3f));
                        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
                        centredLine(L("Aucun périphérique", "No device yet"), p.text);
                        ImGui::PopFont();
                        centredLine(L("Votre clavier MIDI, une manette, une tablette OSC, le clavier de l'ordinateur…",
                                      "Your MIDI keyboard, a gamepad, an OSC tablet, the computer's keyboard…"),
                                    p.textDim);
                        centredLine(L("Créez-le, bougez un de ses contrôles pour le reconnaître, puis apprenez ses règles.",
                                      "Make it, move one of its controls so CyDAW knows it, then learn its rules."),
                                    p.textDim);
                        ImGui::Spacing();
                        const auto width = dp(220.0f);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
                        if (ui::primaryButton(L("+ Nouveau périphérique", "+ New device"), ImVec2(width, dp(32.0f))))
                            newDevice();
                    }
                }
                ImGui::EndChild();
            } else if (index == 1) {
                if (ImGui::BeginChild("##raw", ImVec2(0.0f, 0.0f)))
                    drawRaw();
                ImGui::EndChild();
            } else {
                if (ImGui::BeginChild("##assignments", ImVec2(0.0f, 0.0f)))
                    drawAssignments();
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        requestedTab = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---- Devices ------------------------------------------------------------------------------------

void Controllers::Impl::newDevice()
{
    stopLearning();
    selectedDevice = addVirtualDevice({});
    selectedControl.clear();
    learnStatus.clear();
    // Its hardware found by moving something on it.
    learnHardware = selectedDevice;
    requestedTab = 0;
}

void Controllers::Impl::drawDeviceList()
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Mes périphériques", "My devices"));
    ImGui::Spacing();
    std::string duplicated;
    std::string removed;
    const auto line = ImGui::GetTextLineHeight();
    for (const auto& device : router.devices) {
        ImGui::PushID(device.id.c_str());
        const auto* input = rawDevice(device.input);
        const auto at = ImGui::GetCursorScreenPos();
        const auto width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##device", selectedDevice == device.id, ImGuiSelectableFlags_None, ImVec2(width, line * 2.0f + dp(8.0f)))) {
            if (selectedDevice != device.id)
                selectedControl.clear();
            selectedDevice = device.id;
        }
        if (ImGui::BeginPopupContextItem("##deviceMenu")) {
            if (ImGui::MenuItem(L("Dupliquer", "Duplicate")))
                duplicated = device.id;
            if (ImGui::MenuItem(L("Supprimer", "Delete")))
                removed = device.id;
            ImGui::EndPopup();
        }
        // Its input's light, its name; under it, its hardware and its rules.
        auto* list = ImGui::GetWindowDrawList();
        const auto lit = input != nullptr && secondsNow() - input->activity < 0.25;
        const auto plugged = input != nullptr && input->present;
        list->AddCircleFilled(ImVec2(at.x + dp(8.0f), at.y + dp(4.0f) + line * 0.5f), dp(3.5f), lit ? p.play : plugged ? p.bg5 : p.bg3, 12);
        ui::clippedText(list, ImVec2(at.x + dp(18.0f), at.y + dp(4.0f)), width - dp(22.0f), p.text, device.name.c_str());
        auto detail = device.input.empty() ? ui::Ls("sans matériel", "no hardware") : nameOf(device.input);
        if (input != nullptr && !input->present)
            detail << (input->kind == RawKind::osc ? ui::Ls(" (fermé)", " (closed)") : ui::Ls(" (débranché)", " (unplugged)"));
        detail << middleDot() << juce::String(static_cast<int>(device.controls.size())) << ui::Ls(" règle(s)", " rule(s)");
        ui::clippedText(list, ImVec2(at.x + dp(18.0f), at.y + dp(5.0f) + line), width - dp(22.0f), p.textDim, detail.toRawUTF8());
        ImGui::PopID();
    }
    if (!duplicated.empty()) {
        selectedDevice = duplicateVirtualDevice(duplicated);
        selectedControl.clear();
    }
    if (!removed.empty()) {
        removeVirtualDevice(removed);
        selectedControl.clear();
    }
    ImGui::Spacing();
    if (ui::primaryButton(L("+ Nouveau périphérique", "+ New device"), ImVec2(-1.0f, dp(30.0f))))
        newDevice();
    ui::tooltip(L("Un clavier MIDI, une manette, une tablette OSC, le clavier de l'ordinateur : bougez-en un contrôle pour le reconnaître",
                  "A MIDI keyboard, a gamepad, an OSC tablet, the computer's keyboard: move one of its controls so CyDAW knows it"));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Clic droit sur un périphérique : dupliquer, supprimer.", "Right click a device: duplicate, delete."));
    ImGui::PopTextWrapPos();
}

void Controllers::Impl::drawDevice(control::VirtualDevice& device)
{
    ImGui::PushID(device.id.c_str());
    // Its name, big; what can be done with it, on the right.
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
    if (inputText("##deviceName", device.name, dp(360.0f)))
        changed(true, false);
    ImGui::PopFont();
    ui::tooltip(L("Le nom du périphérique", "The device's name"));
    ImGui::SameLine();
    const auto buttons = ImGui::CalcTextSize(L("Dupliquer", "Duplicate")).x + ImGui::CalcTextSize(L("Supprimer", "Delete")).x
                         + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x + dp(4.0f);
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttons));
    if (ImGui::Button(L("Dupliquer", "Duplicate"))) {
        const auto id = device.id;
        ImGui::PopID();
        selectedDevice = duplicateVirtualDevice(id);
        selectedControl.clear();
        return;
    }
    ui::tooltip(L("Une copie de ses règles, pour un autre appareil du même modèle : elle attend son matériel",
                  "A copy of its rules, for another box of the same model: it waits for its hardware"));
    ImGui::SameLine();
    if (ImGui::Button(L("Supprimer", "Delete"))) {
        const auto id = device.id;
        ImGui::PopID();
        removeVirtualDevice(id);
        selectedControl.clear();
        return;
    }
    ImGui::Spacing();
    drawHardware(device);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // Its rules on the left, the picked one on the right, the monitor under them.
    const auto monitorHeight = dp(150.0f);
    const auto height = std::max(dp(260.0f), ImGui::GetContentRegionAvail().y - monitorHeight - ImGui::GetStyle().ItemSpacing.y);
    if (ImGui::BeginTable("##rulesColumns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("rules", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("rule", ImGuiTableColumnFlags_WidthStretch, 1.05f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##rules", ImVec2(0.0f, height - dp(4.0f))))
            drawControls(device);
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##rule", ImVec2(0.0f, height - dp(4.0f)))) {
            if (auto* control = router.findControl(device.id, selectedControl)) {
                drawControl(device, *control);
            } else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", L("Choisissez une règle : son contrôle, ses modifications et ce qu'elle pilote se règlent ici.",
                                            "Pick a rule: its control, its modifiers and what it moves are set here."));
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if (ImGui::BeginChild("##deviceMonitor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        if (const auto* still = router.findDevice(selectedDevice))
            drawMonitor(still);
    ImGui::EndChild();
    ImGui::PopID();
}

bool Controllers::Impl::rawCombo(const char* id, std::string& value, const bool outputs, const float width)
{
    const auto* current = rawDevice(value);
    auto preview = value.empty() ? (outputs ? ui::Ls("aucun", "none") : ui::Ls("aucune : choisissez-la ou détectez-la", "none: pick it or detect it"))
                                 : nameOf(value);
    if (current != nullptr)
        preview << middleDot() << familyOf(*current);
    if (current != nullptr && !current->present)
        preview << (current->kind == RawKind::osc ? ui::Ls(" (fermé)", " (closed)") : ui::Ls(" (débranché)", " (unplugged)"));
    ImGui::SetNextItemWidth(width);
    auto picked = false;
    if (ImGui::BeginCombo(id, preview.toRawUTF8(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(L("aucun", "none"), value.empty()))
            value.clear(), picked = true;
        // By family: MIDI, this computer, the plug-ins' devices (gamepads), OSC.
        std::vector<const RawDevice*> shown;
        for (const auto& owned : raw)
            if (outputs ? isOutput(owned->kind) : isInput(owned->kind))
                shown.push_back(owned.get());
        std::stable_sort(shown.begin(), shown.end(), [](const auto* a, const auto* b) { return familyRank(a->kind) < familyRank(b->kind); });
        juce::String family;
        auto gamepads = false;
        for (const auto* device : shown) {
            const auto its = familyOf(*device);
            if (its != family) {
                family = its;
                ImGui::SeparatorText(family.toRawUTF8());
            }
            gamepads |= device->kind == RawKind::plugin;
            auto label = nameOf(device->id);
            if (!device->present)
                label << (device->kind == RawKind::osc ? ui::Ls(" (fermé)", " (closed)") : ui::Ls(" (débranché)", " (unplugged)"));
            if (ImGui::Selectable((label + "##" + juce::String(device->id)).toRawUTF8(), device->id == value))
                value = device->id, picked = true;
        }
        if (!gamepads) {
            ImGui::SeparatorText(L("Manettes de jeu", "Gamepads"));
            ImGui::TextDisabled("%s", L("aucune branchée (manettes XInput : Xbox et compatibles)", "none plugged in (XInput gamepads: Xbox and compatible)"));
        }
        if (!outputs) {
            ImGui::Separator();
            if (ImGui::Selectable(L("+ Nouveau point OSC (réseau)…", "+ New OSC endpoint (network)…"))) {
                auto port = std::max(1, newOscPort);
                for (auto taken = true; taken; ++port) {
                    taken = false;
                    for (const auto& owned : raw)
                        taken |= owned->kind == RawKind::osc && owned->listenPort == port;
                    if (!taken)
                        break;
                }
                newOscPort = port + 1;
                value = addOsc(ui::Ls("Tablette OSC", "OSC tablet"), port, "127.0.0.1", 0).id;
                picked = true;
            }
            ui::tooltip(L("Écoute un port UDP, sur cette machine et le réseau local", "Listens on a UDP port, on this machine and the local network"));
        }
        ImGui::EndCombo();
    }
    return picked;
}

void Controllers::Impl::drawOscSettings(RawDevice& device)
{
    const auto& p = theme::palette();
    auto edited = false;
    auto port = device.listenPort;
    ImGui::SetNextItemWidth(dp(80.0f));
    edited |= ImGui::InputInt(L("Écoute (port UDP)", "Listen (UDP port)"), &port, 0);
    ImGui::SameLine(0.0f, dp(14.0f));
    std::string hostName = device.sendHost.toStdString();
    if (inputText("##sendHost", hostName, dp(120.0f)))
        device.sendHost = juce::String(hostName), edited = true;
    ImGui::SameLine(0.0f, dp(4.0f));
    auto sendPort = device.sendPort;
    ImGui::SetNextItemWidth(dp(80.0f));
    edited |= ImGui::InputInt(L("Réponse (hôte, port)", "Answer (host, port)"), &sendPort, 0);
    auto own = device.cydawAddresses;
    if (ImGui::Checkbox(L("Adresses /cydaw/… directes", "Direct /cydaw/… addresses"), &own))
        device.cydawAddresses = own, edited = true;
    ui::tooltip(L("/cydaw/transport/play, /cydaw/track/selected/volume 0.8, /cydaw/dj/A/high… sans règle",
                  "/cydaw/transport/play, /cydaw/track/selected/volume 0.8, /cydaw/dj/A/high… without a rule"));
    if (edited) {
        device.listenPort = std::clamp(port, 0, 65'535);
        device.sendPort = std::clamp(sendPort, 0, 65'535);
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Appliquer", "Apply"))) {
        applyRaw(device);
        changed(true, false);
    }
    if (device.error.isNotEmpty())
        ImGui::TextColored(theme::vec(p.record), "%s", device.error.toRawUTF8());
}

void Controllers::Impl::drawHardware(control::VirtualDevice& device)
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("1 · Matériel", "1 · Hardware"));
    const auto label = dp(80.0f);
    const auto comboWidth = dp(330.0f);
    // The input: what it hears.
    auto* input = rawDevice(device.input);
    formLabel(L("Entrée", "Input"), label);
    activityDot(input != nullptr ? input->activity : 0.0);
    ImGui::SameLine(0.0f, dp(2.0f));
    auto chosen = device.input;
    if (rawCombo("##input", chosen, false, comboWidth)) {
        if (learnHardware == device.id)
            learnHardware.clear();
        setDeviceInput(device, chosen);
        input = rawDevice(device.input);
    }
    ImGui::SameLine();
    const auto detecting = learnHardware == device.id;
    if (detecting)
        ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(p.accent));
    if (ImGui::Button(detecting ? L("Bougez-le…###detect", "Move it…###detect") : L("Détecter###detect", "Detect###detect"))) {
        stopLearning();
        if (!detecting)
            learnHardware = device.id;
    }
    if (detecting)
        ImGui::PopStyleColor();
    ui::tooltip(L("Bougez un bouton, un potard ou une touche de l'appareil : CyDAW le reconnaît",
                  "Move a button, a knob or a key on the device: CyDAW knows it"));
    if (input != nullptr && input->kind == RawKind::midiIn) {
        ImGui::SameLine();
        auto notes = input->notes;
        if (ui::toggle(L("Notes", "Notes"), &notes, p.play, ImVec2(dp(56.0f), 0.0f),
                       L("Ses notes et contrôleurs jouent aussi l'instrument sélectionné, sauf ceux qu'une règle pilote (réglage du port MIDI)",
                         "Its notes and controllers also play the selected instrument, but those a rule drives (the MIDI port's setting)"))) {
            input->notes = notes;
            changed(true, false);
        }
    }
    if (input != nullptr && !input->enabled) {
        ImGui::SameLine();
        if (ImGui::Button(L("Ouvrir le port", "Open the port"))) {
            input->enabled = true;
            applyRaw(*input);
            changed(true, false);
        }
        ui::tooltip(L("Il est fermé : il n'entend rien", "It is closed: it hears nothing"));
    } else if (input != nullptr && !input->present) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.record), "%s", input->kind == RawKind::osc ? L("fermé", "closed") : L("débranché", "unplugged"));
    }
    if (detecting) {
        ImGui::Indent(label);
        waiting(L("Bougez un contrôle de l'appareil (un potard, un bouton, une touche, un stick), ou choisissez-le dans la liste. Échap : annuler.",
                  "Move a control on the device (a knob, a button, a key, a stick), or pick it in the list. Escape: cancel."));
        ImGui::Unindent(label);
        host.wake(2);
    }
    if (input != nullptr && input->kind == RawKind::osc) {
        ImGui::Indent(label);
        ImGui::PushID("osc");
        drawOscSettings(*input);
        ImGui::PopID();
        ImGui::Unindent(label);
    }
    if (input != nullptr && input->kind == RawKind::keyboard) {
        ImGui::Indent(label);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Les touches de ses règles ne font plus leurs raccourcis ni leurs notes, sauf pendant une saisie de texte ; Ctrl et Alt gardent les raccourcis.",
                                    "The keys of its rules no longer do their shortcuts or notes, but while text is typed; Ctrl and Alt keep the shortcuts."));
        ImGui::PopTextWrapPos();
        ImGui::Unindent(label);
    }
    // The output: where values go back.
    const auto* output = rawDevice(device.output);
    formLabel(L("Retour", "Feedback"), label);
    activityDot(output != nullptr ? output->activity : 0.0);
    ImGui::SameLine(0.0f, dp(2.0f));
    auto back = device.output;
    if (rawCombo("##output", back, true, comboWidth))
        setDeviceOutput(device, back);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("LED, faders motorisés, écran d'une tablette, vibration d'une manette",
                                "LEDs, motorised faders, a tablet's screen, a gamepad's rumble"));
}

void Controllers::Impl::drawControls(control::VirtualDevice& device)
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("2 · Règles", "2 · Rules"));
    ImGui::SameLine();
    const auto learning = learnControls == device.id;
    ImGui::BeginDisabled(device.input.empty());
    auto on = learning;
    if (ui::toggle(L("Apprendre###learnControls", "Learn###learnControls"), &on, p.accent, ImVec2(dp(90.0f), 0.0f),
                   L("Bougez les contrôles de l'appareil un par un : chacun devient une règle",
                     "Move the device's controls one by one: each becomes a rule"))) {
        stopLearning();
        if (on)
            learnControls = device.id;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("+ Règle", "+ Rule")))
        selectedControl = addControl(device.id, {}, control::ControlKind::knob, {}, {});
    ui::tooltip(L("Une règle vide, son adresse à apprendre ou à écrire", "An empty rule, its address to learn or to type"));
    if (device.input.empty()) {
        ImGui::TextColored(theme::vec(p.warn), "%s", L("Choisissez d'abord l'entrée (ou Détecter).", "Pick the input first (or Detect)."));
    } else if (learning) {
        waiting(L("Bougez les contrôles de l'appareil un par un : chacun devient une règle. Échap ou Apprendre : terminer.",
                  "Move the device's controls one by one: each becomes a rule. Escape or Learn: done."));
        host.wake(2);
    }
    if (device.controls.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Aucune règle. « Apprendre », puis bougez un potard, un bouton, une touche… Une règle, c'est un contrôle de l'appareil, ses modifications et ce qu'il pilote dans CyDAW.",
                                    "No rule yet. « Learn », then move a knob, a button, a key… A rule is a control of the device, its modifiers and what it moves in CyDAW."));
        ImGui::PopTextWrapPos();
        return;
    }
    // What each control moves.
    std::map<std::string, std::vector<std::string>> moves;
    for (const auto& assignment : router.assignments)
        if (assignment.device == device.id)
            moves[assignment.control].push_back(assignment.target);
    std::string removed;
    if (ImGui::BeginTable("##rules", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn(L("Contrôle", "Control"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn(L("Type", "Kind"), ImGuiTableColumnFlags_WidthFixed, dp(74.0f));
        ImGui::TableSetupColumn(L("Adresse", "Address"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Valeur", "Value"), ImGuiTableColumnFlags_WidthFixed, dp(54.0f));
        ImGui::TableSetupColumn(L("Pilote", "Moves"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& control : device.controls) {
            ImGui::PushID(control.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(control.name.c_str(), selectedControl == control.id, ImGuiSelectableFlags_SpanAllColumns))
                selectedControl = control.id;
            if (ImGui::BeginPopupContextItem("##ruleMenu")) {
                if (ImGui::MenuItem(L("Supprimer la règle", "Delete the rule")))
                    removed = control.id;
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", kindLabel(control.kind));
            ImGui::TableNextColumn();
            auto address = control.sourceAddress.empty() ? ui::Ls("à apprendre", "to learn") : readableAddress(control.sourceAddress);
            if (control.sourceDevice != device.input)
                address << middleDot() << (control.sourceDevice.empty() ? ui::Ls("aucune entrée", "no input") : nameOf(control.sourceDevice));
            ImGui::TextDisabled("%s", address.toRawUTF8());
            ImGui::TableNextColumn();
            valueBar(control.value);
            ImGui::TableNextColumn();
            const auto found = moves.find(control.id);
            if (found == moves.end()) {
                ImGui::TextDisabled("%s", L("rien", "nothing"));
            } else {
                const auto info = targets.describe(found->second.front());
                auto what = info ? info->label : juce::String(found->second.front());
                if (found->second.size() > 1)
                    what << " +" << juce::String(static_cast<int>(found->second.size()) - 1);
                ImGui::TextUnformatted(what.toRawUTF8());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!removed.empty()) {
        removeControl(device.id, removed);
        if (selectedControl == removed)
            selectedControl.clear();
    }
}

void Controllers::Impl::drawControl(control::VirtualDevice& device, control::VirtualControl& control)
{
    const auto& p = theme::palette();
    ImGui::PushID(control.id.c_str());
    ui::sectionTitle((ui::Ls("Règle « ", "Rule « ") + juce::String(control.name) + ui::Ls(" »", " »")).toRawUTF8());
    const auto label = dp(84.0f);
    // Its name and kind.
    formLabel(L("Nom", "Name"), label);
    if (inputText("##controlName", control.name, dp(170.0f)))
        changed(true, false);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(118.0f));
    if (ImGui::BeginCombo("##kind", kindLabel(control.kind))) {
        for (int index = 0; index < control::controlKindCount; ++index) {
            const auto kind = static_cast<control::ControlKind>(index);
            if (ImGui::Selectable(kindLabel(kind), kind == control.kind)) {
                control.kind = kind;
                control.chain = control::defaultChain(kind);
                changed(true, false);
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Le type remet la chaîne de modifications qui lui va", "The kind sets the chain of modifiers that suits it"));
    ImGui::SameLine();
    if (ImGui::Button(L("Supprimer", "Delete"))) {
        const auto deviceId = device.id;
        const auto controlId = control.id;
        ImGui::PopID();
        removeControl(deviceId, controlId);
        selectedControl.clear();
        return;
    }
    // Its control on the hardware: an address of the device's input.
    formLabel(L("Contrôle", "Control"), label);
    if (inputText("##sourceAddress", control.sourceAddress, dp(120.0f)))
        changed(true, false);
    ui::tooltip(L("MIDI : cc/<canal>/<numéro>, note/<canal>/<note>, pb/<canal>… ; manette : button/a, axis/leftX… ; clavier : key/F1 ; OSC : l'adresse",
                  "MIDI: cc/<channel>/<number>, note/<channel>/<note>, pb/<channel>…; gamepad: button/a, axis/leftX…; keyboard: key/F1; OSC: the address"));
    ImGui::SameLine();
    const auto listening = learnSource && learnSource->first == device.id && learnSource->second == control.id;
    if (listening)
        ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(p.accent));
    if (ImGui::Button(listening ? L("Bougez-le…###learnSource", "Move it…###learnSource") : L("Apprendre###learnSource", "Learn###learnSource"))) {
        stopLearning();
        if (!listening)
            learnSource = std::make_pair(device.id, control.id);
    }
    if (listening)
        ImGui::PopStyleColor();
    ui::tooltip(L("Prend l'adresse du prochain contrôle bougé", "Takes the address of the next control moved"));
    if (!control.sourceAddress.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", readableAddress(control.sourceAddress).toRawUTF8());
    }
    if (listening)
        host.wake(2);
    // Its value going back.
    formLabel(L("Retour", "Feedback"), label);
    auto back = !control.feedbackDevice.empty();
    const auto answer = !device.output.empty() ? device.output : feedbackFor(control.sourceDevice);
    ImGui::BeginDisabled(!back && answer.empty());
    if (ImGui::Checkbox("##feedbackOn", &back)) {
        control.feedbackDevice = back ? answer : std::string();
        control.feedbackAddress = back ? (control.feedbackAddress.empty() ? control.sourceAddress : control.feedbackAddress) : std::string();
        changed(true, false);
    }
    ImGui::EndDisabled();
    ui::tooltip(answer.empty() ? L("Choisissez d'abord un retour pour le périphérique", "Pick a feedback output for the device first")
                               : L("Sa valeur repart vers l'appareil quand la cible bouge autrement", "Its value goes back to the device when the target moves otherwise"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!back);
    if (inputText("##feedbackAddress", control.feedbackAddress, dp(120.0f)))
        changed(true, false);
    ImGui::EndDisabled();
    if (back) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", nameOf(control.feedbackDevice).toRawUTF8());
    }
    // Another source or output than the device's: rare, kept apart.
    if (control.sourceDevice != device.input || (back && control.feedbackDevice != device.output && !device.output.empty())
        || ImGui::TreeNode(L("Avancé", "Advanced"))) {
        const auto open = control.sourceDevice == device.input && (!back || control.feedbackDevice == device.output || device.output.empty());
        formLabel(L("Écoute", "Listens to"), label);
        if (rawCombo("##source", control.sourceDevice, false, dp(240.0f)))
            changed(true, false);
        if (control.sourceDevice != device.input) {
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Celle du périphérique", "The device's"))) {
                control.sourceDevice = device.input;
                changed(true, false);
            }
        }
        formLabel(L("Répond à", "Answers"), label);
        if (rawCombo("##feedbackDevice", control.feedbackDevice, true, dp(240.0f))) {
            if (!control.feedbackDevice.empty() && control.feedbackAddress.empty())
                control.feedbackAddress = control.sourceAddress;
            changed(true, false);
        }
        if (open)
            ImGui::TreePop();
    }
    // The chain, raw to virtual.
    ImGui::Spacing();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Modifications (de l'appareil vers CyDAW, et retour)", "Modifiers (device to CyDAW, and back)"));
    for (std::size_t index = 0; index < control.chain.size(); ++index) {
        auto& modifier = control.chain[index];
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
                std::swap(control.chain[index], control.chain[index - 1]);
                changed(true, false);
            }
        }
        ImGui::PopID();
        if (removed) {
            control.chain.erase(control.chain.begin() + static_cast<std::ptrdiff_t>(index));
            changed(true, false);
            break;
        }
    }
    ImGui::SetNextItemWidth(dp(150.0f));
    if (ImGui::BeginCombo("##addModifier", L("+ Modification", "+ Modifier"), ImGuiComboFlags_NoArrowButton)) {
        for (int index = 0; index < control::modifierKindCount; ++index) {
            const auto kind = static_cast<Kind>(index);
            if (ImGui::Selectable(modifierLabel(kind))) {
                control.chain.push_back(control::defaultModifier(kind));
                changed(true, false);
            }
            ui::tooltip(modifierHelp(kind));
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawControlTargets(device, control);
    ImGui::PopID();
}

void Controllers::Impl::drawControlTargets(control::VirtualDevice& device, control::VirtualControl& control)
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("3 · Ce qu'elle pilote", "3 · What it moves"));
    std::optional<std::size_t> removed;
    auto any = false;
    for (std::size_t index = 0; index < router.assignments.size(); ++index) {
        auto& assignment = router.assignments[index];
        if (assignment.device != device.id || assignment.control != control.id)
            continue;
        any = true;
        ImGui::PushID(static_cast<int>(index));
        const auto info = targets.describe(assignment.target);
        ImGui::AlignTextToFramePadding();
        ImGui::Bullet();
        if (info)
            ImGui::TextUnformatted(info->label.toRawUTF8());
        else
            ImGui::TextColored(theme::vec(p.record), "%s", assignment.target.c_str());
        ui::tooltip((juce::String(assignment.target) + (assignment.project ? ui::Ls("  ·  gardée avec le projet", "  ·  kept with the project")
                                                                          : ui::Ls("  ·  pour tous les projets", "  ·  for every project")))
                        .toRawUTF8());
        // Its range and pickup, on the right.
        const auto right = dp(50.0f) * 2.0f + dp(2.0f) + ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - right));
        auto low = static_cast<float>(assignment.minimum);
        auto high = static_cast<float>(assignment.maximum);
        ImGui::SetNextItemWidth(dp(50.0f));
        auto edited = ImGui::DragFloat("##low", &low, 0.005f, 0.0f, 1.0f, "%.2f");
        ui::tooltip(L("La partie de la course de la cible que la règle couvre", "The part of the target's course the rule covers"));
        ImGui::SameLine(0.0f, dp(2.0f));
        ImGui::SetNextItemWidth(dp(50.0f));
        edited |= ImGui::DragFloat("##high", &high, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
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
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            removed = index;
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled("%s", L("Rien encore.", "Nothing yet."));
    if (removed)
        unassign(*removed);
    // A target picked by a click in the interface, or found by name.
    ImGui::Spacing();
    const auto picking = pickTarget && pickTarget->first == device.id && pickTarget->second == control.id;
    if (picking)
        ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(p.accent));
    if (ImGui::Button(picking ? L("Cliquez la cible dans CyDAW…###pick", "Click the target in CyDAW…###pick")
                              : L("+ Cliquer la cible dans CyDAW###pick", "+ Click the target in CyDAW###pick"))) {
        if (picking) {
            pickTarget.reset();
            learnMode = false;
        } else {
            stopLearning();
            pickTarget = std::make_pair(device.id, control.id);
            learnMode = true;
            learnTarget.clear();
        }
        ui::setLearnMode(learnMode);
    }
    if (picking)
        ImGui::PopStyleColor();
    ui::tooltip(L("Tout ce qui peut être piloté s'encadre : cliquez ce que la règle doit bouger (un potard, un fader, le transport…)",
                  "Everything that can be driven is framed: click what the rule should move (a knob, a fader, the transport…)"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##targetFilter", L("ou cherchez une cible : volume, cutoff, transport…", "or find a target: volume, cutoff, transport…"),
                             targetFilter, sizeof(targetFilter));
    if (targetFilter[0] == 0)
        return;
    if (ImGui::BeginChild("##targetList", ImVec2(0.0f, dp(170.0f)), ImGuiChildFlags_Borders)) {
        const auto filter = juce::String::fromUTF8(targetFilter);
        auto shown = 0;
        for (const auto& key : targets.list()) {
            const auto info = targets.describe(key);
            if (!info || (!info->label.containsIgnoreCase(filter) && !juce::String(key).containsIgnoreCase(filter)))
                continue;
            if (++shown > 200)
                break;
            if (ImGui::Selectable((info->label + "##" + juce::String(key)).toRawUTF8()))
                assign(device.id, control.id, key, false);
        }
        if (shown == 0)
            ImGui::TextDisabled("%s", L("Aucune cible de ce nom.", "No target of that name."));
    }
    ImGui::EndChild();
}

// ---- The hardware tab ---------------------------------------------------------------------------

void Controllers::Impl::drawRaw()
{
    const auto& p = theme::palette();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Ce que CyDAW voit tel quel : les ports MIDI, le clavier de l'ordinateur, les appareils des plug-ins d'application (manettes), les points OSC. Un périphérique en prend une entrée et un retour.",
                                "What CyDAW sees as it is: the MIDI ports, the computer's keyboard, the application plug-ins' devices (gamepads), the OSC endpoints. A device takes an input and a feedback output from them."));
    ImGui::PopTextWrapPos();
    // Which devices use each one.
    std::map<std::string, juce::String> users;
    for (const auto& device : router.devices)
        for (const auto* id : { &device.input, &device.output })
            if (!id->empty()) {
                auto& names = users[*id];
                if (!names.contains(juce::String(device.name)))
                    names << (names.isEmpty() ? "" : ", ") << juce::String(device.name);
            }
    for (const auto kind : { RawKind::midiIn, RawKind::midiOut, RawKind::keyboard, RawKind::plugin, RawKind::osc }) {
        ImGui::Spacing();
        ui::sectionTitle(rawKindLabel(kind));
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
            auto label = nameOf(device.id);
            if (!device.present)
                label << (kind == RawKind::osc ? ui::Ls(" (fermé)", " (closed)") : ui::Ls(" (débranché)", " (unplugged)"));
            if (ImGui::Selectable(label.toRawUTF8(), selectedRaw == device.id, 0, ImVec2(dp(320.0f), 0.0f)))
                selectedRaw = device.id;
            ImGui::EndDisabled();
            if (kind == RawKind::midiIn) {
                ImGui::SameLine();
                auto notes = device.notes;
                if (ui::toggle(L("Notes", "Notes"), &notes, p.play, ImVec2(dp(52.0f), 0.0f),
                               L("Ses notes et contrôleurs jouent aussi l'instrument sélectionné (sauf ceux qu'une règle pilote)",
                                 "Its notes and controllers also play the selected instrument (but those a rule drives)"))) {
                    device.notes = notes;
                    changed(true, false);
                }
            }
            if (const auto found = users.find(device.id); found != users.end()) {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::vec(p.textDim), "%s", (juce::String::fromUTF8("\xe2\x86\x92 ") + found->second).toRawUTF8());
            }
            if (isInput(kind) && device.present) {
                ImGui::SameLine();
                if (ImGui::SmallButton(L("Nouveau périphérique avec celui-ci", "New device with this one"))) {
                    stopLearning();
                    selectedDevice = addVirtualDevice({}, device.id, {});
                    selectedControl.clear();
                    requestedTab = 0;
                }
            }
            if (device.error.isNotEmpty())
                ImGui::TextColored(theme::vec(p.record), "  %s", device.error.toRawUTF8());
            // An OSC endpoint's settings, when picked.
            if (kind == RawKind::osc && selectedRaw == device.id) {
                ImGui::Indent(dp(16.0f));
                drawOscSettings(device);
                ImGui::SameLine();
                if (ImGui::Button(L("Retirer", "Remove"))) {
                    const auto id = device.id;
                    ImGui::Unindent(dp(16.0f));
                    ImGui::PopID();
                    removeRaw(id);
                    return;
                }
                ImGui::Unindent(dp(16.0f));
            }
            ImGui::PopID();
        }
        if (!any)
            ImGui::TextDisabled("  %s", kind == RawKind::plugin ? L("aucun appareil (une manette XInput apparaît quand elle est branchée)",
                                                                   "no device (an XInput gamepad shows when it is plugged in)")
                                                                : L("aucune", "none"));
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
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::BeginChild("##hardwareMonitor", ImVec2(0.0f, dp(220.0f))))
        drawMonitor(nullptr);
    ImGui::EndChild();
}

// ---- Every assignment ---------------------------------------------------------------------------

void Controllers::Impl::drawAssignments()
{
    const auto& p = theme::palette();
    if (router.assignments.empty()) {
        ImGui::TextDisabled("%s", L("Aucune. Dans un périphérique, une règle dit ce qu'elle pilote ; ou « Apprendre », puis cliquez une commande de CyDAW et bougez un contrôle.",
                                    "None. In a device, a rule says what it moves; or « Learn », then click a CyDAW control and move a control."));
        return;
    }
    std::optional<std::size_t> removed;
    if (ImGui::BeginTable("##assigned", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn(L("Périphérique · règle", "Device · rule"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
            const auto name = (device != nullptr ? juce::String(device->name) : juce::String("?")) + middleDot()
                              + (control != nullptr ? juce::String(control->name) : juce::String("?"));
            if (ImGui::Selectable(name.toRawUTF8(), false) && control != nullptr) {
                selectedDevice = assignment.device;
                selectedControl = assignment.control;
                requestedTab = 0;
            }
            ui::tooltip(L("Ouvrir la règle", "Open the rule"));
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
}

// ---- The monitor --------------------------------------------------------------------------------

void Controllers::Impl::drawMonitor(const control::VirtualDevice* only)
{
    const auto& p = theme::palette();
    ui::sectionTitle(L("Moniteur", "Monitor"));
    ImGui::SameLine();
    ImGui::Checkbox(L("Pause", "Pause"), &monitorPaused);
    if (only != nullptr) {
        ImGui::SameLine();
        ImGui::Checkbox(L("Tout le matériel", "All hardware"), &monitorAll);
        ui::tooltip(L("Sinon, seulement l'entrée et le retour de ce périphérique", "Else only this device's input and feedback"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Effacer", "Clear")))
        monitor.clear();
    // The rows shown: this device's hardware, or all.
    std::vector<std::size_t> rows;
    rows.reserve(monitor.size());
    for (std::size_t index = 0; index < monitor.size(); ++index)
        if (only == nullptr || monitorAll || monitor[index].device == only->input || monitor[index].device == only->output)
            rows.push_back(index);
    if (ImGui::BeginTable("##seen", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, dp(60.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, dp(20.0f));
        ImGui::TableSetupColumn(L("Appareil", "Device"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Adresse", "Address"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Valeur", "Value"), ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        const auto now = secondsNow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& seen = monitor[rows[static_cast<std::size_t>(row)]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("-%.1fs", now - seen.time);
                ImGui::TableNextColumn();
                ImGui::TextColored(theme::vec(seen.out ? p.accent : p.play), "%s", seen.out ? "<" : ">");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(rawDevice(seen.device) != nullptr ? nameOf(seen.device).toRawUTF8() : seen.device.c_str());
                ImGui::TableNextColumn();
                // The address, and how it reads when that says more.
                const auto readable = readableAddress(seen.message.address);
                ImGui::TextUnformatted(seen.message.address.c_str());
                if (readable != juce::String(seen.message.address)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", readable.toRawUTF8());
                }
                ImGui::TableNextColumn();
                const auto midi = !seen.message.address.empty() && seen.message.address.find('/') != std::string::npos && seen.message.address[0] != '/'
                                  && seen.message.address.rfind("key/", 0) != 0 && seen.message.address.rfind("button/", 0) != 0
                                  && seen.message.address.rfind("axis/", 0) != 0 && seen.message.address.rfind("trigger/", 0) != 0;
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

// ---- The computer's keyboard --------------------------------------------------------------------

bool Controllers::Impl::keyboardWanted()
{
    const auto listens = [&](const std::string& id) {
        const auto* device = router.findDevice(id);
        return device != nullptr && device->input == computerKeyboardId;
    };
    // Learnt in the window: the keys are its own only while it is in front
    // (Space elsewhere still plays).
    if (!learnHardware.empty())
        return windowFocused;
    if (!learnControls.empty() && listens(learnControls))
        return windowFocused;
    if (learnSource) {
        if (!windowFocused)
            return false;
        if (listens(learnSource->first))
            return true;
        if (const auto* control = router.findControl(learnSource->first, learnSource->second);
            control != nullptr && control->sourceDevice == computerKeyboardId)
            return true;
    }
    if (learnMode && !learnTarget.empty())
        for (const auto& device : router.devices)
            if (device.input == computerKeyboardId)
                return true;
    return false;
}

void Controllers::Impl::pollKeys()
{
    const auto& io = ImGui::GetIO();
    // Escape stops what the window is learning.
    if ((!learnHardware.empty() || !learnControls.empty() || learnSource) && !io.WantTextInput
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        learnHardware.clear();
        learnControls.clear();
        learnSource.reset();
        host.wake(2);
    }
    // Typing, or a shortcut held: not a control.
    const auto* keyboard = rawDevice(computerKeyboardId);
    const auto allowed = keyboard != nullptr && keyboard->enabled && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    std::set<int> keys;
    if (allowed) {
        for (const auto& device : router.devices)
            for (const auto& control : device.controls)
                if (control.sourceDevice == computerKeyboardId && control.sourceAddress.rfind("key/", 0) == 0)
                    if (const auto key = keyNamed(control.sourceAddress.substr(4)); key != ImGuiKey_None && learnableKey(key))
                        keys.insert(key);
        if (keyboardWanted())
            for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key)
                if (learnableKey(static_cast<ImGuiKey>(key)))
                    keys.insert(key);
        // A key taken while down stays taken until let go: the one that
        // ended a learn does not start a note late.
        for (const auto& [key, down] : keysDown)
            if (down)
                keys.insert(key);
    }
    const auto say = [this](const int key, const bool down) {
        process({ Incoming::Kind::message, computerKeyboardId,
                  { "key/" + std::string(ImGui::GetKeyName(static_cast<ImGuiKey>(key))), down ? 1.0 : 0.0 } });
    };
    // A key said down that is not listened to any more: let go.
    for (auto it = keysDown.begin(); it != keysDown.end();) {
        if (!keys.contains(it->first)) {
            if (it->second)
                say(it->first, false);
            it = keysDown.erase(it);
        } else {
            ++it;
        }
    }
    static const auto owner = ImHashStr("CyDAW controllers' keys");
    for (const auto key : keys) {
        const auto down = ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
        // Its own while listened to: no shortcut, no note takes it.
        ImGui::SetKeyOwner(static_cast<ImGuiKey>(key), owner, ImGuiInputFlags_LockThisFrame);
        auto& was = keysDown[key];
        if (down != was) {
            was = down;
            say(key, down);
        }
    }
}

// ---- Learn mode over the whole interface --------------------------------------------------------------

void Controllers::Impl::drawOverlay()
{
    const auto spots = ui::takeLearnSpots();
    if (!learnMode) {
        pickTarget.reset();
        return;
    }
    const auto& p = theme::palette();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (!learnTarget.empty()) {
            learnTarget.clear();
        } else {
            learnMode = false;
            pickTarget.reset();
            ui::setLearnMode(false);
        }
        host.wake(3);
        return;
    }
    const auto* picking = pickTarget ? router.findControl(pickTarget->first, pickTarget->second) : nullptr;
    if (pickTarget && picking == nullptr)
        pickTarget.reset();
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
            ImGui::TextDisabled("%s", picking != nullptr ? L("Clic : la règle la pilote", "Click: the rule moves it")
                                                         : L("Clic : apprendre · clic droit : retirer l'assignation",
                                                             "Click: learn · right click: remove the assignment"));
            ImGui::EndTooltip();
            if (clickedLeft) {
                const auto info = targets.describe(hovered->key);
                const auto label = info ? info->label : juce::String(hovered->label);
                if (picking != nullptr) {
                    // The rule picked in the window moves it.
                    const auto [deviceId, controlId] = *pickTarget;
                    learnStatus = label + juce::String::fromUTF8(" \xe2\x86\x90 ") + juce::String(picking->name);
                    assign(deviceId, controlId, hovered->key, false);
                    pickTarget.reset();
                    learnMode = false;
                    ui::setLearnMode(false);
                } else {
                    learnTarget = hovered->key;
                    learnTargetLabel = label;
                    learnStatus.clear();
                }
            }
            if (clickedRight)
                for (std::size_t index = router.assignments.size(); index-- > 0;)
                    if (router.assignments[index].target == hovered->key)
                        unassign(index);
        }
        // The banner, on the main window.
        if (id == ImGui::GetMainViewport()->ID && learnMode) {
            const auto message = pickTarget && picking != nullptr
                                     ? ui::Ls("« ", "« ") + juce::String(picking->name)
                                           + ui::Ls(" » : cliquez ce que la règle doit piloter. Échap : annuler.", " »: click what the rule should move. Escape: cancel.")
                                 : !learnTarget.empty()
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
                pickTarget.reset();
                ui::setLearnMode(false);
            }
        }
        ImGui::End();
    }
    host.wake(2);
}

} // namespace cydaw::v2
