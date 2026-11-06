# gm_mcp

[![Build](https://github.com/unknown-gd/gm_mcp/actions/workflows/build.yml/badge.svg)](https://github.com/unknown-gd/gm_mcp/actions/workflows/build.yml)

MCP bridge for Garry's Mod — C++ server module + Node MCP server.

Repository: https://github.com/unknown-gd/gm_mcp

Inspired by [mathis1M/gmod-mcp](https://github.com/mathis1M/gmod-mcp), rewritten in C++ with a `mcp` Lua library name (server modules only).

## Layout

| Path      | Role                                                                  |
| --------- | --------------------------------------------------------------------- |
| `src/`    | Native bridge (`gmsv_mcp_*.dll`) — TCP JSON bridge + `require("mcp")` |
| `mcp/`    | Node MCP server (stdio) talking to `127.0.0.1:17905`                  |
| Lua addon | Not included yet — write your own against the `mcp` library API below |

## Native library (`mcp`)

```lua
require("mcp")

local status = mcp.getStatus() -- { connected, message }
mcp.setStatus(true, "Garry's Mod is connected.")
local command = mcp.pollCommand() -- table or nil
mcp.respondLua(requestId, ok, payload)
mcp.respondScreenshot(requestId, ok, jpeg_or_error)
mcp.isRunning()
mcp.shutdown()
```

`pollCommand()` returns fields: `tool`, `state`, `code`, `target`, `classFilter`, `customCheck`, `limit`, `requestId`, `returnResult`.

Bind address defaults to `127.0.0.1:17905`. Override with `GM_MCP_HOST` / `GM_MCP_PORT`.

Copy the matching **server** module into `garrysmod/lua/bin`:

| GMod branch | OS      | Module                 |
| ----------- | ------- | ---------------------- |
| x64         | Linux   | `gmsv_mcp_linux64.dll` |
| x32         | Linux   | `gmsv_mcp_linux.dll`   |
| x64         | Windows | `gmsv_mcp_win64.dll`   |
| x32         | Windows | `gmsv_mcp_win32.dll`   |

## Building

### CMake presets

```sh
cmake --preset linux64 && cmake --build --preset linux64
cmake --preset linux32 && cmake --build --preset linux32   # needs g++-multilib

# Windows — MSVC Developer shell + Ninja
cmake --preset win64 && cmake --build --preset win64
cmake --preset win32 && cmake --build --preset win32
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

```sh
# Install from GitHub Packages
echo "@unknown-gd:registry=https://npm.pkg.github.com" >> .npmrc
# auth: //npm.pkg.github.com/:_authToken=YOUR_GITHUB_TOKEN
bun add -g @unknown-gd/gm-mcp@next
```

Local development:

```sh
nix develop .#mcp          # Bun shell in mcp/
# or:
cd mcp && bun install
bun run dev                # local stdio server
bun test
bun run build

nix build .#mcp            # → result/bin/gm-mcp
nix run .#mcp
```

Point your MCP client at the package or a local checkout:

```json
{
  "command": "bunx",
  "args": ["--registry=https://npm.pkg.github.com", "@unknown-gd/gm-mcp@next"]
}
```

```json
{
  "command": "bun",
  "args": ["/abs/path/to/gm_mcp/mcp/server.ts"]
}
```

Tools: `gmod_status`, `gmod_players`, `gmod_entities`, `exec_lua_code`, `gmod_screenshot`.

## How it works

```text
AI client  --stdio MCP-->  Bun MCP server  --JSON TCP :17905-->  gmsv_mcp_*.dll  <-- Lua mcp.* -->  your addon
```

## License

See [LICENSE](LICENSE).
