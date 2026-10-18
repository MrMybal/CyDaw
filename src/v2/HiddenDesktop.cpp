// An automated session (--silent or --hidden, unless --visible: tests, probes,
// builders) puts its windows on a desktop of its own, never shown: nothing of
// it can come in front of what the person is doing, nor take the keyboard,
// whatever calls it makes (when no key has been pressed for a while, Windows
// lets any program take the foreground). It renders there as anywhere else
// (snapshots, input sent through the control port).
//
// Done before main(), as a static object: a thread moves to another desktop
// only while it has no window yet (JUCE makes its own at start).

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

 #include <string_view>

namespace {

bool hasFlag(const std::wstring_view line, const std::wstring_view flag)
{
    for (auto at = line.find(flag); at != std::wstring_view::npos; at = line.find(flag, at + 1)) {
        const auto before = at == 0 ? L' ' : line[at - 1];
        const auto end = at + flag.size();
        const auto after = end >= line.size() ? L' ' : line[end];
        if ((before == L' ' || before == L'"' || before == L'\t') && (after == L' ' || after == L'"' || after == L'\t'))
            return true;
    }
    return false;
}

const bool onHiddenDesktop = [] {
    const std::wstring_view line(GetCommandLineW());
    if (!(hasFlag(line, L"--silent") || hasFlag(line, L"--hidden")) || hasFlag(line, L"--visible"))
        return false;
    // One desktop for every automated session, made by the first, kept while any runs.
    auto* desktop = CreateDesktopW(L"CyDAW-automation", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    return desktop != nullptr && SetThreadDesktop(desktop) != FALSE;
}();

} // namespace

#endif
