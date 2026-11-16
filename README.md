# gm_mcp

[![Build](https://github.com/unknown-gd/gm_mcp/actions/workflows/build.yml/badge.svg)](https://github.com/unknown-gd/gm_mcp/actions/workflows/build.yml)

MCP bridge for Garry's Mod — C++ server module + Bun MCP server.

Repository: https://github.com/unknown-gd/gm_mcp

Inspired by [mathis1M/gmod-mcp](https://github.com/mathis1M/gmod-mcp), rewritten in C++ with a `mcp` Lua library name (server modules only).

## Companion Lua host

**`gm_mcp` is the native IPC + MCP face.** The supported Lua host is **[Dreamwork](https://github.com/Pika-Software/dreamwork)** (`lua/dreamwork/modules/mcp.lua`): poll loop, `mcp.returnResult`, client net shuttle, screenshots, enable/exec/capture gates, and error windows.

Do not expect to vendor Dreamwork into this repository — keep the split:

| Layer | Owns |
| --- | --- |
| `gmsv_mcp_*.dll` | Listen/bind, optional token auth, queue, wait/cancel, binary screenshots |
| Bun `@unknown-gd/gm-mcp` | MCP stdio tools, typed bridge errors, `GM_MCP_*` forwarding |
| Dreamwork | Game-side poll/execute, realm policy, client capture, CLI |

For CI / smoke without Dreamwork, see [`fixtures/minimal_host.lua`](fixtures/minimal_host.lua).

## Layout

| Path | Role |
| --- | --- |
| `src/` | Native bridge (`gmsv_mcp_*.dll`) — TCP JSON bridge + `require("mcp")` |
| `mcp/` | Bun MCP server (stdio) talking to `127.0.0.1:17905` |
| `fixtures/` | Tiny contract host (not a full product addon) |
| `tests/` | Protocol unit tests + bridge TCP harness |

## Native library (`mcp`)

```lua
require("mcp")

local status = mcp.getStatus() -- { connected, message }
mcp.setStatus(true, "Garry's Mod is connected.")
local command = mcp.pollCommand() -- table or nil
mcp.respondLua(requestId, ok, payload)
mcp.respondError(message)                -- fail active waiting exec (console errors)
mcp.respondError(requestId, message)     -- fail a specific waiting exec
mcp.respondScreenshot(requestId, ok, jpeg_or_error)
mcp.isRunning()
mcp.shutdown()
```

Dreamwork also exposes `mcp.returnResult(...)` for waiting execs (not part of the native export table).

`pollCommand()` returns fields: `tool`, `state`, `code`, `target`, `classFilter`, `customCheck`, `limit`, `requestId`, `returnResult`.

`respondError` returns `true` if a waiting MCP request was notified. Use it from a console/error hook instead of `respondLua(id, false, …)`.

### Bind address and auth

| Variable | Default | Role |
| --- | --- | --- |
| `GM_MCP_HOST` | `127.0.0.1` | Bridge bind / MCP client connect host |
| `GM_MCP_PORT` | `17905` | Bridge bind / MCP client connect port |
| `GM_MCP_TOKEN` | _(empty)_ | When set, every TCP JSON request must include matching `"token"` |

Auth is optional during migration: unset token keeps old clients working. Set the same `GM_MCP_TOKEN` in the game process and the Bun MCP server when locking down localhost.

Copy the matching **server** module into `garrysmod/lua/bin`:

| GMod branch | OS | Module |
| --- | --- | --- |
| x64 | Linux | `gmsv_mcp_linux64.dll` |
| x32 | Linux | `gmsv_mcp_linux.dll` |
| x64 | Windows | `gmsv_mcp_win64.dll` |
| x32 | Windows | `gmsv_mcp_win32.dll` |

## Building

### CMake presets

```sh
cmake --preset linux64 && cmake --build --preset linux64
cmake --preset linux32 && cmake --build --preset linux32   # needs g++-multilib

# Windows — MSVC Developer shell + Ninja
cmake --preset win64 && cmake --build --preset win64
cmake --preset win32 && cmake --build --preset win32
```

Linux builds also produce `protocol_test` and `bridge_harness` (disable with `-DGM_MCP_BUILD_TESTS=OFF`).

```sh
ctest --test-dir build/linux64
cd mcp && bun test
```

Or:

```sh
cmake -B build -G Ninja -DGM_MCP_TARGET=linux64|linux32|win64|win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Optional: Nix flake (local)

```sh
nix build .#linux64
nix develop               # cmake, ninja, gcc, clangd, bun
```

## MCP server (Bun)

Published to GitHub Packages as [`@unknown-gd/gm-mcp`](https://github.com/unknown-gd/gm_mcp/pkgs/npm/gm-mcp).

GitHub Packages **requires auth** even for public packages. Put this in `~/.npmrc`:

```ini
@unknown-gd:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=YOUR_GITHUB_PAT
```

The token needs `read:packages` (and `repo` if the package is from a private repo).

Then configure your MCP client:

```json
{
    "command": "bunx",
    "args": ["--bun", "--package", "@unknown-gd/gm-mcp@next", "gm-mcp"],
    "env": {
        "GM_MCP_TOKEN": "optional-shared-secret"
    }
}
```

Or after a global install (`bun add -g @unknown-gd/gm-mcp@next`):

```json
{
    "command": "gm-mcp"
}
```

Local development (no registry):

```sh
cd mcp && bun install
bun run dev
```

```json
{
    "command": "bun",
    "args": ["/abs/path/to/gm_mcp/mcp/server.ts"],
    "env": {
        "GM_MCP_TOKEN": "optional-shared-secret"
    }
}
```

Tools: `gmod_status`, `gmod_players`, `gmod_entities`, `exec_lua_code`, `gmod_screenshot`.

Bridge failures are classified (`unavailable`, `timeout`, `unauthorized`, `bridge`) and returned with MCP `isError` instead of a soft “unavailable” for every fault.

## How it works

```text
AI client  --stdio MCP-->  Bun MCP server  --JSON TCP :17905-->  gmsv_mcp_*.dll  <-- Lua mcp.* -->  Dreamwork
```

## License

See [LICENSE](LICENSE).
