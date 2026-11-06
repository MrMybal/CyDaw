## Include and callbacks {#signatures}
The workshop’s component template supplies `CyInterfaceContext`. Its two callbacks have these signatures; they are function pointers on the context, not global functions to implement in the host.

```cpp
#include "CustomInterfaceABI.h"
double (*get)(void* user, const char* key);
void (*set)(void* user, const char* key, double value);
```

## Parameters and results {#parameters}
`user` is the opaque host pointer and must be passed unchanged to each callback. `key` comes from the context’s `primary`, `secondary`, `tertiary` or `quaternary` binding. A valid `get` returns the current parameter value; `set` updates it and returns no value. `minimum` and `maximum` describe the primary control’s range.

`imgui` is the host’s ImGui context. `abiVersion` and `imguiVersion` describe the supported binary contract. Components must match the supplied template and target compiler; arbitrary Dear ImGui builds are not binary compatible.

## Read and update a bound value {#example}
With a valid context pointer named `context`, the following fragment reads and writes an existing primary binding. It illustrates callback usage, not a complete loadable component.

```cpp
const double value = context->get(context->user, context->primary);
context->set(context->user, context->primary, value);
```

Create and compile the full component from **Interface > Example** in the [interface designer](doc:interface-design#code). Audio processing belongs to the [processor contract](doc:api-audio-processor), not to UI callbacks.
