/**
 * @doc api-interface-abi
 * @locale en
 * @title Custom visual component ABI
 * @section API
 * @order 240
 * @symbol CyInterfaceContext
 * ## Context {#context}
 * CyInterfaceContext is the versioned bridge between an authored ImGui component and its host. abiVersion defaults to 1; imguiVersion identifies the ImGui build. The host supplies the ImGui context, opaque user pointer, four parameter bindings and get/set callbacks.
 *
 * ## Thread and binding rules {#bindings}
 * All callbacks run on the UI thread. No audio buffers cross this interface. Use the supplied get(user, key) and set(user, key, value) callbacks with valid bindings. Check the ABI/ImGui versions rather than assuming binary compatibility between arbitrary builds. The public callbacks are described below; the Interface editor's Example command supplies the complete current entry point template.
 *
 * See [interface code](doc:interface-design#code).
 */
/**
 * @doc api-interface-abi
 * @locale fr
 * @title ABI des composants visuels
 * @section API
 * @order 240
 * @symbol CyInterfaceContext
 * ## Contexte {#context}
 * CyInterfaceContext relie un composant ImGui personnalisé à son hôte par une ABI versionnée. abiVersion vaut 1 par défaut ; imguiVersion identifie la compilation ImGui. L'hôte fournit le contexte ImGui, un pointeur utilisateur opaque, quatre liaisons de paramètres et les callbacks get/set.
 *
 * ## Thread et liaisons {#bindings}
 * Tous les callbacks s'exécutent sur le thread UI. Aucun tampon audio ne traverse cette interface. Employer get(user, key) et set(user, key, value) avec des liaisons valides. Vérifier les versions ABI/ImGui avant de supposer la compatibilité binaire entre compilations. Les callbacks publics sont décrits ci-dessous ; la commande Exemple de l'éditeur Interface fournit le modèle complet actuel.
 *
 * Voir le [code d'interface](doc:interface-design#code).
 */
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
