---
id: mcp
title: AI assistant, command line and MCP
description: Use the same CyDaw session tools from the assistant, terminal and an external MCP client.
section: Develop / Public API
order: 100
parent: developer-api
revision: 17a1ba4efa3b6af5
---

## Open the assistant {#overview}

Open **Help → AI assistant**. Choose an API or a local service such as Ollama or LM Studio. Start the local service separately. The **Detect providers** button checks for compatible configurations and local services. Leave the model field empty to use the provider’s catalogue, or enter an exact model ID. An API key can come from the selected provider’s environment variable or the masked field. Use an explicit key with a custom endpoint.

The provider receives your messages and the results of any tools it calls. Attach files only when you want to send them to that provider. **Stop** cancels the active turn and keeps the partial reply. Enable **Keep local history** before connecting to save the conversation on this computer and use **Restore** to reopen it. The context slider limits what goes to the provider while retaining the stored conversation. Set it to zero for the full history.

## Connect a terminal or MCP client {#native}

In the assistant, select **Enable local MCP** and **Copy token**. Configure the client for Streamable HTTP at `http://127.0.0.1:PORT/mcp` with `Authorization: Bearer TOKEN`. The port shown in the assistant identifies the instance. For a scripted launch, set `CYDAW_MCP_TOKEN` in both processes to the same random value of at least 32 characters, then start the desired instance:

```powershell
& '.\CyDAW 2.exe' --mcp-port=47830 --control-port=0
.\cydawctl.exe --port=47830 list
.\cydawctl.exe --port=47830 describe set_tempo
.\cydawctl.exe --port=47830 call session_status
'{"bpm":132}' | .\cydawctl.exe --port=47830 call set_tempo -
```

Enable **Allow session edits** before the last command, or launch an unattended instance with `--mcp-allow-edits`. Save any changes from CyDaw. Use `--silent` for a hidden session with muted speakers. The SDK endpoint is bound to loopback and requires the bearer token. It supports MCP discovery and tool calls over HTTP, including legacy MCP initialisation. Start the existing Python bridge for clients that require stdio.

Without `--port`, the CLI can show schemas and query the factory effect catalogue without starting the app:

```powershell
'{"limit":3}' | .\cydawctl.exe call list_factory_effects -
```

Pass inline JSON, `-` for standard input, or `@PATH` for a UTF-8 JSON file. Results go to stdout. Errors go to stderr with a nonzero exit code. `--version` shows the SDK version and source revision.

The chat, CLI and MCP server use the same catalogue. Use `list` to discover all the tools and `describe NAME` to inspect the arguments. Read actions such as `{"action":"status"}` work in read-only mode. An action that also opens a workspace or selects a tab requires **Allow session edits**.

| Workspace | Commands |
| --- | --- |
| Studio | Session and track status, transport, tempo, loop, mix, MIDI tracks and clips, buses and undo |
| [Graphs](doc:graphs) | Read graphs and devices, edit and wire nodes, set parameters, work with modulation, containers and racks |
| [Cinema](doc:cinema) | Use `cinema` for sessions, video and soundtrack, buses, subtrack containers, clips and audio export |
| [DJ Mix](doc:dj) | Use `dj` for decks, cues, loops, grid, mixer, effects, recording and collection |
| [Foundry](doc:foundry) | Use `foundry` for sessions, import, tracks, editing, processing, playback and export |
| [Plugin Workshop](doc:workshop) | Use `workshop` for devices, tabs, links, simulator, export checks, VST3/CLAP/LV2 builds and portable graph export |

For graph tools, use `"track":"track-id:<trackId>"` with the ID from `list_tracks`, or `"track":"workshop"` for the workshop graph. Use `nextOffset` to continue paged lists.

For file commands, choose **Media and export folder** under **Connection**. An automated instance can set `CYDAW_MCP_FILE_ROOT` to an absolute folder before launching CyDaw. Pass absolute file paths inside that folder. Use a fresh destination for file exports and an empty or new directory for plugin exports. To overwrite an existing file, use the application’s save or export controls. Plugin builds use CyDaw’s own source configuration.

**Discover tools on demand** is enabled by default in the assistant. The server’s catalogue cache uses a 30-second TTL by default. Set `--mcp-cache-seconds=0` to disable it, or reconnect to refresh discovery. Live reads and edits always run against the current session. `--mcp-metrics` writes timing, byte counts, status and cache metadata to `%APPDATA%/CyDAW/Assistant/metrics.jsonl`. The log excludes tool arguments, results, messages and credentials.

## Python bridge compatibility {#python-bridge}

CyDAW’s original Python MCP server translates requests into commands on a separate local control port. It retains its existing command catalogue, including note editing, devices and mix export.

## Start from your MCP client {#connection}
1. Build or install CyDAW and locate `CyDAW 2.exe`.
2. Configure your MCP client to launch Python 3 with the supplied `tools/mcp/cydaw_mcp_server.py` script as its argument. Communication with the client uses standard input/output.
3. Set `CYDAW_EXE` to the executable path if automatic discovery does not find your installation. Set `CYDAW_CONTROL_PORT` when the default port `47820` is unavailable.
4. Connect and request `status` before making edits. The bridge can launch the application when needed.

The application listens on `127.0.0.1`; `--control-port=N` changes the port and `--control-port=0` disables it. If you configure `CYDAW_CONTROL_TOKEN`, the bridge passes the matching shared token when launching the application. Keep client and application settings consistent.

## Operate a session {#workflow}
Start by checking the project, active workspace and transport. Ask the client to create or select a track, add an instrument or effect, then adjust the intended parameter. Use the returned track/node IDs rather than assuming a track’s visual position is its identity. Confirm a render’s destination before calling `export_mix`.

For example, ask the chat to list the buses and subtracks in the open Cinema session. The equivalent read-only CLI call is `cinema` with `action: status`. Use `dj`, `foundry` or `workshop` with the same action to inspect those workspaces. To inspect the active Workshop device graph, use `get_track_graph` with `track: workshop`.

```powershell
'{"action":"status"}' | .\cydawctl.exe --port=47830 call cinema -
'{"track":"workshop"}' | .\cydawctl.exe --port=47830 call get_track_graph -
```

After reviewing a result, enable session edits to ask for a specific change. Name the workspace and the target track, tab or deck. For media and exports, give a path inside the allowed folder above. Check [audio setup](doc:setup) if a successful action produces no audible result.

## Recover from a connection problem {#troubleshooting}
Verify the executable path, local port and matching token. Close a stale client connection, reconnect, then request the current status before retrying edits. The bridge has no third-party Python dependency. Avoid sending logs or project paths to services you have not chosen to use.
