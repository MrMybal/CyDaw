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
