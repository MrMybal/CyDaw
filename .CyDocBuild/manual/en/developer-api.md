---
id: developer-api
title: Extend and control CyDAW
description: Choose the public contract for custom components, controllers or automation.
section: Develop / Public API
order: 100
revision: 50dad86ee80c0b28
---

## Choose an interface {#overview}
Use the interface that matches the work you need to perform:

| Goal | Public contract |
| --- | --- |
| Draw an interactive workshop component | [Visual component ABI](doc:api-interface-abi) |
| Add a raw hardware or network controller | [Controller extension ABI](doc:api-controller-abi) |
| Implement an engine audio processor | [Audio processor contract](doc:api-audio-processor) |
| Drive an existing CyDAW session from an MCP client | [MCP server](doc:mcp) |

These contracts describe different lifetimes and threads. UI callbacks do not process audio; controller extensions do not receive audio buffers. Use the versioned host structures and public includes rather than assuming that arbitrary builds are binary compatible.

## Keep examples small {#examples}
The API pages provide signatures, parameters, return values and short usage fragments. Full component templates come from the workshop’s Example action. Save your device before trying a new component and verify its bindings in the [interface designer](doc:interface-design#code).
