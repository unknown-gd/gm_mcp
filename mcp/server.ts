#!/usr/bin/env bun

import { createConnection } from "node:net";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const BRIDGE_HOST = process.env.GM_MCP_HOST ?? "127.0.0.1";
const BRIDGE_PORT = Number(process.env.GM_MCP_PORT ?? "17905");
const BRIDGE_TOKEN = process.env.GM_MCP_TOKEN ?? "";
const BRIDGE_REQUEST_TIMEOUT = 1_000;
const BRIDGE_EXEC_RESULT_TIMEOUT = 11_000;
const MAX_SCREENSHOT_BYTES = 1024 * 1024;
const UNAVAILABLE = "GMod MCP is unavailable.";
const UNAUTHORIZED = "Invalid or missing GM_MCP_TOKEN.";

type JsonObject = Record<string, unknown>;

export type BridgeFailure = {
  kind: "unavailable" | "timeout" | "unauthorized" | "bridge";
  message: string;
};

const isObject = (value: unknown): value is JsonObject =>
  typeof value === "object" && value !== null && !Array.isArray(value);

export function classifyBridgeError(cause: unknown): BridgeFailure {
  if (isObject(cause) && cause.error === "unauthorized") {
    return {
      kind: "unauthorized",
      message: typeof cause.message === "string" ? cause.message : UNAUTHORIZED,
    };
  }

  if (cause instanceof Error) {
    const text = cause.message;
    if (text === "GMod bridge timed out." || /timed out/i.test(text)) {
      return { kind: "timeout", message: text };
    }
    if (text === UNAUTHORIZED || /GM_MCP_TOKEN/i.test(text) || /unauthorized/i.test(text)) {
      return { kind: "unauthorized", message: text.includes("token") || text.includes("TOKEN")
        ? text
        : UNAUTHORIZED };
    }
    const code = (cause as NodeJS.ErrnoException).code;
    if (
      code === "ECONNREFUSED" ||
      code === "ENOTFOUND" ||
      code === "ECONNRESET" ||
      /ECONNREFUSED/i.test(text)
    ) {
      return { kind: "unavailable", message: UNAVAILABLE };
    }
    return { kind: "bridge", message: text || UNAVAILABLE };
  }

  return { kind: "unavailable", message: UNAVAILABLE };
}

const exchange = (request: Buffer, timeout: number): Promise<Buffer> =>
  new Promise((resolve, reject) => {
    const socket = createConnection({ host: BRIDGE_HOST, port: BRIDGE_PORT });
    const chunks: Buffer[] = [];
    let done = false;

    const finish = (callback: () => void) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      socket.destroy();
      callback();
    };

    const timer = setTimeout(
      () => finish(() => reject(new Error("GMod bridge timed out."))),
      timeout,
    );

    socket.once("connect", () => socket.write(request));
    socket.on("data", (chunk) => chunks.push(Buffer.from(chunk)));
    socket.once("end", () => finish(() => resolve(Buffer.concat(chunks))));
    socket.once("error", (cause) => finish(() => reject(cause)));
  });

const parseObject = (data: Buffer): JsonObject => {
  const result: unknown = JSON.parse(data.toString("utf8"));
  if (!isObject(result)) throw new Error("Invalid bridge response.");
  return result;
};

const withToken = (fields: JsonObject): JsonObject =>
  BRIDGE_TOKEN ? { ...fields, token: BRIDGE_TOKEN } : fields;

const throwIfUnauthorized = (result: JsonObject) => {
  if (result.error === "unauthorized") {
    throw Object.assign(new Error(message(result, UNAUTHORIZED)), {
      error: "unauthorized",
      message: message(result, UNAUTHORIZED),
    });
  }
};

const bridgeRequest = async (
  action: string,
  fields: JsonObject = {},
  timeout = BRIDGE_REQUEST_TIMEOUT,
): Promise<JsonObject> => {
  const response = await exchange(
    Buffer.from(`${JSON.stringify(withToken({ action, ...fields }))}\n`),
    timeout,
  );
  const result = parseObject(response);
  throwIfUnauthorized(result);
  return result;
};

const bridgeScreenshot = async (target: string): Promise<Buffer> => {
  const response = await exchange(
    Buffer.from(`${JSON.stringify(withToken({ action: "screenshot", target }))}\n`),
    12_000,
  );

  const lineEnd = response.indexOf(0x0a);
  if (lineEnd < 0) throw new Error("Invalid bridge response.");

  const header: unknown = JSON.parse(response.subarray(0, lineEnd).toString("utf8"));
  if (!isObject(header)) throw new Error("Invalid bridge response.");
  throwIfUnauthorized(header);
  if (header.ok !== true) {
    throw new Error(
      typeof header.message === "string" ? header.message : "Screenshot failed.",
    );
  }
  if (
    typeof header.size !== "number" ||
    !Number.isInteger(header.size) ||
    header.size <= 0 ||
    header.size > MAX_SCREENSHOT_BYTES
  ) {
    throw new Error("Invalid screenshot size.");
  }

  const screenshot = response.subarray(lineEnd + 1);
  if (screenshot.length !== header.size) throw new Error("Incomplete screenshot.");
  return screenshot;
};

const message = (result: JsonObject, fallback: string) =>
  typeof result.message === "string" ? result.message : fallback;

const json = (result: unknown, isError = false) => ({
  content: [{ type: "text" as const, text: JSON.stringify(result) }],
  ...(isError ? { isError: true as const } : {}),
});

const failureJson = (failure: BridgeFailure, extra: JsonObject = {}) =>
  json({ ok: false, error: failure.kind, message: failure.message, ...extra }, true);

const runBridge = async <T>(
  run: () => Promise<T>,
  mapSuccess: (value: T) => unknown,
  mapFailure?: (failure: BridgeFailure) => unknown,
): Promise<ReturnType<typeof json>> => {
  try {
    return json(mapSuccess(await run()));
  } catch (cause) {
    const failure = classifyBridgeError(cause);
    if (mapFailure) return json(mapFailure(failure), true);
    return failureJson(failure);
  }
};

export function validateExec(
  code: string,
  state: "server" | "client",
  target: string | undefined,
  returnResult: boolean,
): JsonObject | undefined {
  if (!code.trim()) return { ok: false, message: "Lua code must not be empty." };
  if (state === "client" && !target?.trim()) {
    return {
      ok: false,
      message:
        "target is required for client execution: player name, SteamID, SteamID64, or 'all'.",
    };
  }
  if (state === "server" && target !== undefined) {
    return { ok: false, message: "target is only valid for client execution." };
  }
  if (typeof returnResult !== "boolean") {
    return { ok: false, message: "return_result must be a boolean." };
  }
}

export function createServer() {
  const mcp = new McpServer(
    { name: "gm-mcp", version: "0.1.0" },
    {
      instructions:
        "MCP for Garry's Mod via gm_mcp + Dreamwork. For Lua execution, avoid hook Think, hook Tick, and player.GetAll unless necessary; keep code simple and efficient. Use mcp.returnResult(...) when return_result=true (provided by Dreamwork's MCP host).",
    },
  );

  mcp.registerTool(
    "gmod_status",
    { description: "Report the current Garry's Mod connection status." },
    async () =>
      runBridge(
        async () => {
          const status = await bridgeRequest("status");
          if (typeof status.connected !== "boolean" || typeof status.message !== "string") {
            throw new Error("Invalid bridge response.");
          }
          return { connected: status.connected, message: status.message };
        },
        (status) => status,
        (failure) => ({
          connected: false,
          error: failure.kind,
          message: failure.message,
        }),
      ),
  );

  mcp.registerTool(
    "gmod_players",
    {
      description: "List connected players and their current server-side state.",
    },
    async () =>
      runBridge(async () => {
        const result = await bridgeRequest("gmod_players");
        if (result.ok !== true) {
          throw new Error(message(result, "Player query failed."));
        }
        if (!Array.isArray(result.result)) throw new Error("Invalid bridge response.");
        return { ok: true, players: result.result };
      }, (value) => value),
  );

  mcp.registerTool(
    "gmod_entities",
    {
      description: "List server entities with a capped, structured snapshot.",
      inputSchema: {
        class_filter: z
          .string()
          .optional()
          .describe("Optional case-insensitive substring matched against entity classes."),
        customCheck: z
          .string()
          .max(60 * 1024)
          .optional()
          .describe(
            "Lua function body; return true to include an entity. The entity is available as `entity`.",
          ),
        limit: z
          .number()
          .int()
          .min(1)
          .max(200)
          .optional()
          .default(100)
          .describe("Maximum entities to return."),
      },
    },
    async ({ class_filter, customCheck, limit }) => {
      const fields: JsonObject = { limit };
      if (class_filter?.trim()) fields.class_filter = class_filter.trim();
      if (customCheck?.trim()) fields.custom_check = customCheck;
      return runBridge(async () => {
        const result = await bridgeRequest("gmod_entities", fields);
        if (result.ok !== true) {
          throw new Error(message(result, "Entity query failed."));
        }
        if (!isObject(result.result) || !Array.isArray(result.result.entities)) {
          throw new Error("Invalid bridge response.");
        }
        return { ok: true, ...result.result };
      }, (value) => value);
    },
  );

  mcp.registerTool(
    "exec_lua_code",
    {
      description:
        "Execute Lua; set return_result=true only when the code returns a value or calls mcp.returnResult(...).",
      inputSchema: {
        code: z.string().describe("Lua code to execute."),
        state: z
          .enum(["server", "client"])
          .describe("Execute on the server or on one client."),
        target: z
          .string()
          .optional()
          .describe("Client name, SteamID, or SteamID64; required for client state."),
        return_result: z
          .boolean()
          .optional()
          .default(false)
          .describe("Wait for and return the Lua result."),
      },
    },
    async ({ code, state, target, return_result }) => {
      const invalid = validateExec(code, state, target, return_result);
      if (invalid) return json(invalid, true);

      const fields: JsonObject = { state, code };
      if (target !== undefined) fields.target = target;
      if (return_result) fields.return_result = true;

      return runBridge(async () => {
        const result = await bridgeRequest(
          "exec_lua_code",
          fields,
          return_result ? BRIDGE_EXEC_RESULT_TIMEOUT : BRIDGE_REQUEST_TIMEOUT,
        );
        if (typeof result.ok !== "boolean") {
          throw new Error("Invalid bridge response.");
        }
        if (return_result) {
          if (result.ok) return { ok: true, result: result.result };
          throw new Error(message(result, "Lua execution failed."));
        }
        if (typeof result.message !== "string") {
          throw new Error("Invalid bridge response.");
        }
        if (!result.ok) throw new Error(result.message);
        return { ok: result.ok, message: result.message };
      }, (value) => value);
    },
  );

  mcp.registerTool(
    "gmod_screenshot",
    {
      description:
        "Capture one player's current GMod render as a JPEG image, including VGUI. Save the returned screenshot to a local file before displaying it.",
      inputSchema: {
        target: z.string().describe("Client name, SteamID, or SteamID64."),
      },
    },
    async ({ target }) => {
      if (!target.trim()) {
        return json({ ok: false, message: "target is required for a screenshot." }, true);
      }
      try {
        const screenshot = await bridgeScreenshot(target.trim());
        return {
          content: [
            {
              type: "image" as const,
              data: screenshot.toString("base64"),
              mimeType: "image/jpeg",
            },
          ],
        };
      } catch (cause) {
        return failureJson(classifyBridgeError(cause));
      }
    },
  );

  return mcp;
}

export async function main() {
  await createServer().connect(new StdioServerTransport());
}

if (import.meta.main) {
  main().catch((cause) => {
    console.error(cause);
    process.exitCode = 1;
  });
}
