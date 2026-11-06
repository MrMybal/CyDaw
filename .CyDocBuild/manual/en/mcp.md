---
id: mcp
title: Control a session with MCP
description: Connect an MCP client and operate a local CyDAW session.
section: Develop / Public API
order: 100
parent: developer-api
revision: 34a4876056e9ddf1
---

## Connect the local bridge {#overview}
CyDAW’s Python MCP server translates client requests into commands on the application’s local control port. It can inspect a session, create tracks and notes, adjust devices, operate the transport and render a mix. These operations change the active session: save your project first.

## Start from your MCP client {#connection}
1. Build or install CyDAW and locate `CyDAW 2.exe`.
2. Configure your MCP client to launch Python 3 with the supplied `tools/mcp/cydaw_mcp_server.py` script as its argument. Communication with the client uses standard input/output.
3. Set `CYDAW_EXE` to the executable path if automatic discovery does not find your installation. Set `CYDAW_CONTROL_PORT` when the default port `47820` is unavailable.
4. Connect and request `status` before making edits. The bridge can launch the application when needed.

The application listens on `127.0.0.1`; `--control-port=N` changes the port and `--control-port=0` disables it. If you configure `CYDAW_CONTROL_TOKEN`, the bridge passes the matching shared token when launching the application. Keep client and application settings consistent.

## Operate a session {#workflow}
Start by checking the project, active workspace and transport. Ask the client to create or select a track, add an instrument or effect, then adjust the intended parameter. Use the returned track/node IDs rather than assuming a track’s visual position is its identity. Confirm a render’s destination before calling `export_mix`.

The tool catalog also includes dedicated Foundry, Cinema and workshop operations. Their contexts are separate from the Studio project. An operation’s successful reply does not prove that the selected audio output is connected to your speakers; check [audio setup](doc:setup) when nothing is heard.

## Recover from a connection problem {#troubleshooting}
Verify the executable path, local port and matching token. Close a stale client connection, reconnect, then request the current status before retrying edits. The bridge has no third-party Python dependency. Avoid sending logs or project paths to services you have not chosen to use.
