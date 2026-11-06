## Includes and entry point {#signatures}
These are the exported entry point and the callback signatures stored in `CyControlPlugin`. Return a stable descriptor; CyDAW checks `abi_version` against `CYDAW_CONTROL_ABI_VERSION` before starting the device service.

```c
#include <cydaw/control/ControlPluginABI.h>
const CyControlPlugin* cydaw_control_plugin(void);
int start(const CyControlHost* host);
void stop(void);
void poll(void);
int send(const char* device, const char* address, double value);
```

## Parameters and results {#parameters}
- `host` provides the host context and device/message/log callbacks. Keep it valid only for the active service lifetime.
- `start` returns nonzero on success. Announce each device with `device_added(context, id, name)` and remove disconnected devices with `device_removed(context, id)`.
- `device` and `address` are device-local identifiers, such as `gamepad-1` and `axis/leftX`; they are not audio or MIDI buffer addresses.
- `value` is normalised from 0 to 1. `send` returns 1 when the value was sent; it may be null if feedback is unsupported.
- `poll` and `stop` return no value. Once `stop` returns, no callback into the host may remain in flight.

## Sending a control value {#example}
This short fragment runs while the service has a valid host pointer. The strings identify a device already announced by the extension.

```c
host->message(host->context, "gamepad-1", "axis/leftX", 0.5);
```

Install the shared library as `.cyctl` in `ControlPlugins` beside CyDAW or in `Documents/CyDAW/Control Plugins`. This interface adds raw controllers; it is independent of the audio plugin formats. See [hardware setup](doc:controllers).
