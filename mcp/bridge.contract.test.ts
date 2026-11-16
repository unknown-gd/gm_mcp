import { afterAll, beforeAll, describe, expect, test } from "bun:test";
import { createConnection } from "node:net";
import { spawn, type Subprocess } from "bun";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const harnessPath = join(root, "build/linux64/bridge_harness");
const PORT = 17927;
const TOKEN = "contract-secret";

type JsonObject = Record<string, unknown>;

const exchange = (
  host: string,
  port: number,
  request: object,
  timeoutMs = 2_000,
): Promise<Buffer> =>
  new Promise((resolve, reject) => {
    const socket = createConnection({ host, port });
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
      timeoutMs,
    );
    socket.once("connect", () => socket.write(`${JSON.stringify(request)}\n`));
    socket.on("data", (chunk) => chunks.push(Buffer.from(chunk)));
    socket.once("end", () => finish(() => resolve(Buffer.concat(chunks))));
    socket.once("error", (cause) => finish(() => reject(cause)));
  });

const parseJson = (data: Buffer): JsonObject => {
  const text = data.toString("utf8").replace(/\n$/, "");
  return JSON.parse(text) as JsonObject;
};

async function waitForReady(proc: Subprocess, timeoutMs = 3_000): Promise<boolean> {
  const reader = proc.stdout.getReader();
  const decoder = new TextDecoder();
  let buffer = "";
  const deadline = Date.now() + timeoutMs;

  while (Date.now() < deadline) {
    const remaining = deadline - Date.now();
    const result = await Promise.race([
      reader.read(),
      Bun.sleep(remaining).then(() => null),
    ]);
    if (!result) return false;
    if (result.done) return buffer.includes("READY");
    buffer += decoder.decode(result.value, { stream: true });
    if (buffer.includes("READY")) return true;
  }
  return false;
}

describe("bridge TCP contract", () => {
  let harness: Subprocess | undefined;

  beforeAll(async () => {
    const file = Bun.file(harnessPath);
    if (!(await file.exists())) {
      throw new Error(
        `Missing ${harnessPath}. Build with: cmake --preset linux64 && cmake --build --preset linux64`,
      );
    }
  });

  afterAll(() => {
    harness?.kill();
  });

  test("rejects requests when token is configured but missing", async () => {
    harness = spawn({
      cmd: [harnessPath, "--port", String(PORT), "--token", TOKEN],
      cwd: root,
      stdout: "pipe",
      stderr: "pipe",
    });

    expect(await waitForReady(harness)).toBe(true);

    const body = parseJson(await exchange("127.0.0.1", PORT, { action: "status" }));
    expect(body).toMatchObject({
      ok: false,
      error: "unauthorized",
      message: "Invalid or missing GM_MCP_TOKEN.",
    });

    const ok = parseJson(
      await exchange("127.0.0.1", PORT, { action: "status", token: TOKEN }),
    );
    expect(ok).toMatchObject({
      connected: true,
      message: "Garry's Mod is connected.",
    });

    harness.kill();
    await harness.exited;
    harness = undefined;
  });
});
