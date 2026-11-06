/**
 * @doc api-controller-abi
 * @locale en
 * @title Controller extension ABI
 * @section API
 * @order 240
 * @symbol CyControlPlugin
 * ## Contract {#contract}
 * This C ABI creates controller devices; it is separate from audio plugin hosting. A shared library with extension .cyctl exports cydaw_control_plugin(), returning a CyControlPlugin whose abi_version matches CYDAW_CONTROL_ABI_VERSION.
 *
 * ## Lifetime and messages {#lifetime}
 * start(host) announces devices; zero means failure. poll() and send() run on the controller service thread; poll and send may be null. stop() must finish all calls into the host before unloading. Addresses identify controls inside each device. Values are normalised to 0..1; centred axes rest at 0.5. CyControlHost.message may be called from any thread.
 *
 * See [controllers](doc:controllers) for setup, and the public signatures below.
 */
/**
 * @doc api-controller-abi
 * @locale fr
 * @title ABI des extensions contrôleurs
 * @section API
 * @order 240
 * @symbol CyControlPlugin
 * ## Contrat {#contract}
 * Cette ABI C crée des périphériques de contrôle ; elle est distincte des plugins audio. Une bibliothèque partagée .cyctl exporte cydaw_control_plugin(), qui renvoie un CyControlPlugin dont abi_version correspond à CYDAW_CONTROL_ABI_VERSION.
 *
 * ## Durée de vie et messages {#lifetime}
 * start(host) annonce les périphériques ; zéro signale un échec. poll() et send() tournent sur le thread du service contrôleurs et peuvent être nuls. stop() doit terminer tous les appels vers l'hôte avant le déchargement. Les adresses identifient les contrôles de chaque périphérique. Les valeurs vont de 0 à 1 ; un axe centré repose à 0,5. CyControlHost.message peut être appelé depuis n'importe quel thread.
 *
 * Voir les [contrôleurs](doc:controllers) pour la configuration, puis les signatures publiques ci-dessous.
 */
#ifndef CYDAW_CONTROL_PLUGIN_ABI_H
#define CYDAW_CONTROL_PLUGIN_ABI_H

/*
 * CyDAW application plug-ins: devices CyDAW does not know by itself (a
 * gamepad, a HID box, a network protocol) brought in as RAW controllers.
 * Not audio plug-ins: they make no sound and see no audio.
 *
 * A plug-in is a shared library renamed ".cyctl", in "ControlPlugins" beside
 * CyDAW or in Documents/CyDAW/Control Plugins. It exports one C function,
 * cydaw_control_plugin(), that returns its description. CyDAW calls start()
 * once, poll() about a hundred times a second from one thread of its own,
 * send() from that same thread, and stop() before unloading it.
 *
 * Addresses are the plug-in's own words ("button/a", "axis/leftX",
 * "trigger/left", "rumble/left"); values are 0..1 (a centred axis rests at
 * 0.5). They are what a virtual control's source and feedback name.
 *
 * Plain C, so any compiler (and language) can make one.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define CYDAW_CONTROL_ABI_VERSION 1

#if defined(_WIN32)
#define CYDAW_CONTROL_EXPORT __declspec(dllexport)
#else
#define CYDAW_CONTROL_EXPORT __attribute__((visibility("default")))
#endif

typedef struct CyControlHost {
    void* context;
    /* A device appeared (id: unique within the plug-in; name: shown). */
    void (*device_added)(void* context, const char* id, const char* name);
    /* It went away (unplugged). */
    void (*device_removed)(void* context, const char* id);
    /* One of its controls moved. Any thread. */
    void (*message)(void* context, const char* device, const char* address, double value);
    /* A line for CyDAW's controller monitor. */
    void (*log)(void* context, const char* text);
} CyControlHost;

typedef struct CyControlPlugin {
    int abi_version;            /* CYDAW_CONTROL_ABI_VERSION */
    const char* name;           /* "Gamepads (XInput)" */
    const char* version;        /* "1.0" */
    /* Begins: devices are announced through the host from here on. 0 fails. */
    int (*start)(const CyControlHost* host);
    /* Ends: no call into the host after it returns. */
    void (*stop)(void);
    /* Called about a hundred times a second (may be null). */
    void (*poll)(void);
    /* A value back to a device (a light, a rumble): 1 when it was said. May be null. */
    int (*send)(const char* device, const char* address, double value);
} CyControlPlugin;

/* The one function a plug-in exports. */
typedef const CyControlPlugin* (*CyControlPluginEntry)(void);
#define CYDAW_CONTROL_PLUGIN_ENTRY "cydaw_control_plugin"

#ifdef __cplusplus
}
#endif

#endif
