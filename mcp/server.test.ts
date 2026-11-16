import { describe, expect, test } from "bun:test";
import { classifyBridgeError, validateExec } from "./server.ts";

describe("validateExec", () => {
  test("accepts valid server and client executions", () => {
    expect(validateExec("print('ok')", "server", undefined, false)).toBeUndefined();
    expect(validateExec("print('ok')", "client", "all", false)).toBeUndefined();
  });

  test("rejects empty Lua code", () => {
    expect(validateExec("  \n", "server", undefined, false)).toEqual({
      ok: false,
      message: "Lua code must not be empty.",
    });
  });

  test("requires a target for client execution", () => {
    expect(validateExec("print('ok')", "client", undefined, false)).toMatchObject({
      ok: false,
      message: expect.stringContaining("target is required"),
    });
    expect(validateExec("print('ok')", "client", "  ", false)).toMatchObject({
      ok: false,
    });
  });

  test("rejects server targets and non-boolean return_result", () => {
    expect(validateExec("print('ok')", "server", "player", false)).toEqual({
      ok: false,
      message: "target is only valid for client execution.",
    });
    expect(
      validateExec("print('ok')", "server", undefined, undefined as never),
    ).toEqual({
      ok: false,
      message: "return_result must be a boolean.",
    });
  });
});

describe("classifyBridgeError", () => {
  test("maps timeouts distinctly from unavailable", () => {
    expect(classifyBridgeError(new Error("GMod bridge timed out."))).toEqual({
      kind: "timeout",
      message: "GMod bridge timed out.",
    });
  });

  test("maps unauthorized bridge responses", () => {
    expect(
      classifyBridgeError(new Error("Invalid or missing GM_MCP_TOKEN.")),
    ).toEqual({
      kind: "unauthorized",
      message: "Invalid or missing GM_MCP_TOKEN.",
    });
    expect(
      classifyBridgeError({
        error: "unauthorized",
        message: "Invalid or missing GM_MCP_TOKEN.",
      }),
    ).toEqual({
      kind: "unauthorized",
      message: "Invalid or missing GM_MCP_TOKEN.",
    });
  });

  test("maps connection refused as unavailable", () => {
    const cause = Object.assign(new Error("connect ECONNREFUSED"), {
      code: "ECONNREFUSED",
    });
    expect(classifyBridgeError(cause)).toEqual({
      kind: "unavailable",
      message: "GMod MCP is unavailable.",
    });
  });

  test("preserves other bridge messages", () => {
    expect(classifyBridgeError(new Error("Lua code must not be empty."))).toEqual({
      kind: "bridge",
      message: "Lua code must not be empty.",
    });
  });
});
