#include "ComputerKeyboard.h"

#include "Studio.h"

#include <imgui_internal.h>

#include <algorithm>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace cydaw::v2 {

namespace {

// Where the keys are, by their scan code (the same on every layout), and
// the QWERTY letter each carries for a computer that cannot say.
struct Place {
    int scancode;
    char qwerty;
    int semitone;
};
constexpr Place places[] = {
    // The middle row: the white keys, from C.
    { 0x1E, 'A', 0 }, { 0x1F, 'S', 2 }, { 0x20, 'D', 4 }, { 0x21, 'F', 5 }, { 0x22, 'G', 7 }, { 0x23, 'H', 9 },
    { 0x24, 'J', 11 }, { 0x25, 'K', 12 }, { 0x26, 'L', 14 }, { 0x27, ';', 16 }, { 0x28, '\'', 17 },
    // The row above: the black keys between them.
    { 0x11, 'W', 1 }, { 0x12, 'E', 3 }, { 0x14, 'T', 6 }, { 0x15, 'Y', 8 }, { 0x16, 'U', 10 }, { 0x18, 'O', 13 },
    { 0x19, 'P', 15 },
    // The bottom row: octave down and up, velocity down and up.
    { 0x2C, 'Z', -1 }, { 0x2D, 'X', -2 }, { 0x2E, 'C', -3 }, { 0x2F, 'V', -4 },
};

ImGuiKey keyOfCharacter(const char character)
{
    if (character >= 'A' && character <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (character - 'A'));
    if (character >= '0' && character <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (character - '0'));
    switch (character) {
    case ';': return ImGuiKey_Semicolon;
    case '\'': return ImGuiKey_Apostrophe;
    case ',': return ImGuiKey_Comma;
    case '.': return ImGuiKey_Period;
    default: return ImGuiKey_None;
    }
}

// The ImGui key a place gives on this computer, and what it types. Letters
// come by what they type (the key code), the other keys by their place, as
// the Windows backend reads them.
ImGuiKey keyOfPlace(const Place& place, std::string& label)
{
#if JUCE_WINDOWS
    const auto layout = GetKeyboardLayout(0);
    const auto code = MapVirtualKeyExW(static_cast<UINT>(place.scancode), MAPVK_VSC_TO_VK, layout);
    const auto typed = MapVirtualKeyExW(code, MAPVK_VK_TO_CHAR, layout) & 0x7fffu;
    if (typed != 0)
        label = juce::String::charToString(static_cast<juce::juce_wchar>(typed)).toUpperCase().toStdString();
    if (code >= 'A' && code <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(code - 'A'));
    if (code >= '0' && code <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(code - '0'));
    if (code == VK_OEM_COMMA)
        return ImGuiKey_Comma;
    if (code == VK_OEM_PERIOD)
        return ImGuiKey_Period;
    switch (place.scancode) {
    case 0x27: return ImGuiKey_Semicolon;
    case 0x28: return ImGuiKey_Apostrophe;
    case 0x1A: return ImGuiKey_LeftBracket;
    case 0x1B: return ImGuiKey_RightBracket;
    default: break;
    }
#endif
    label = std::string(1, place.qwerty);
    return keyOfCharacter(place.qwerty);
}

} // namespace

ComputerKeyboard::ComputerKeyboard(Studio& studio) : studio_(studio)
{
    auto& settings = studio_.settings();
    enabled_ = settings.getBoolValue("computerKeyboard", true);
    octave_ = std::clamp(settings.getIntValue("computerKeyboardOctave", 4), -1, 8);
    velocity_ = std::clamp(settings.getIntValue("computerKeyboardVelocity", 100), 1, 127);
    readLayout();
}

ComputerKeyboard::~ComputerKeyboard()
{
    allNotesOff();
}

void ComputerKeyboard::readLayout()
{
    keys_.clear();
    for (const auto& place : places) {
        Key key;
        key.key = keyOfPlace(place, key.label);
        key.semitone = place.semitone;
        if (key.key != ImGuiKey_None)
            keys_.push_back(std::move(key));
    }
}

void ComputerKeyboard::update(const bool allowed)
{
    const auto& io = ImGui::GetIO();
    // A shortcut (Ctrl, Alt) or a text being typed: not a note.
    const auto playing = enabled_ && allowed && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    active_ = playing;
    if (!playing) {
        allNotesOff();
        return;
    }
    auto& keyboard = studio_.engine().keyboardState();
    static const auto owner = ImHashStr("CyDAW computer keyboard");
    for (const auto& key : keys_) {
        const auto down = ImGui::IsKeyDown(key.key);
        // Its keys are its own while it plays: no shortcut takes them.
        ImGui::SetKeyOwner(key.key, owner, ImGuiInputFlags_LockThisFrame);
        const auto held = held_.find(key.key);
        if (down && held == held_.end()) {
            if (key.semitone >= 0) {
                const auto note = (octave_ + 1) * 12 + key.semitone;
                if (note >= 0 && note <= 127) {
                    keyboard.noteOn(1, note, static_cast<float>(velocity_) / 127.0f);
                    held_[key.key] = note;
                }
            } else {
                switch (key.semitone) {
                case -1: setOctave(octave_ - 1); break;
                case -2: setOctave(octave_ + 1); break;
                case -3: setVelocity(velocity_ <= 20 ? 1 : velocity_ - 20); break;
                case -4: setVelocity(velocity_ == 1 ? 20 : velocity_ + 20); break;
                default: break;
                }
                held_[key.key] = -1;
            }
        } else if (!down && held != held_.end()) {
            if (held->second >= 0)
                keyboard.noteOff(1, held->second, 0.0f);
            held_.erase(held);
        }
    }
}

void ComputerKeyboard::allNotesOff()
{
    auto& keyboard = studio_.engine().keyboardState();
    for (const auto& [key, note] : held_)
        if (note >= 0)
            keyboard.noteOff(1, note, 0.0f);
    held_.clear();
}

void ComputerKeyboard::setEnabled(const bool enabled)
{
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    if (!enabled_)
        allNotesOff();
    save();
}

void ComputerKeyboard::setOctave(const int octave)
{
    octave_ = std::clamp(octave, -1, 8);
    save();
}

void ComputerKeyboard::setVelocity(const int velocity)
{
    velocity_ = std::clamp(velocity, 1, 127);
    save();
}

std::vector<int> ComputerKeyboard::heldNotes() const
{
    std::vector<int> notes;
    for (const auto& [key, note] : held_)
        if (note >= 0)
            notes.push_back(note);
    std::sort(notes.begin(), notes.end());
    return notes;
}

void ComputerKeyboard::save()
{
    // An automated session leaves the person's choices be.
    if (!studio_.keepsSettings())
        return;
    auto& settings = studio_.settings();
    settings.setValue("computerKeyboard", enabled_);
    settings.setValue("computerKeyboardOctave", octave_);
    settings.setValue("computerKeyboardVelocity", velocity_);
}

} // namespace cydaw::v2
