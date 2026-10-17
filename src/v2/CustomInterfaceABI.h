#pragma once
#include <imgui.h>
#include <cstdint>
#if defined(_WIN32)
#define CY_INTERFACE_EXPORT __declspec(dllexport)
#else
#define CY_INTERFACE_EXPORT __attribute__((visibility("default")))
#endif
// Versioned interface between a custom visual component and its host.
// All callbacks run on the UI thread. No audio buffers cross this interface.
struct CyInterfaceContext
{
    std::uint32_t abiVersion{1};
    std::uint32_t imguiVersion{IMGUI_VERSION_NUM};
    ImGuiContext *imgui{};
    void *user{};
    double minimum{};
    double maximum{1};
    const char *primary{};
    const char *secondary{};
    const char *tertiary{};
    const char *quaternary{};
    double (*get)(void *, const char *){};
    void (*set)(void *, const char *, double){};
};
