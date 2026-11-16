#!/usr/bin/env python3
"""TCP contract checks against bridge_harness (auth + non-blocking accept)."""

from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = Path(
    os.environ.get(
        "GM_MCP_BRIDGE_HARNESS",
        str(ROOT / "build" / "linux64" / "bridge_harness"),
    )
)


def exchange(port: int, request: dict, timeout: float = 1.0) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((json.dumps(request) + "\n").encode())
        sock.settimeout(timeout)
        chunks = []
        while True:
            try:
                chunk = sock.recv(4096)
            except socket.timeout as exc:
                raise TimeoutError("GMod bridge timed out.") from exc
            if not chunk:
                break
            chunks.append(chunk)
            if b"\n" in chunk:
                # For JSON responses, one line is enough; keep reading if binary follows.
                data = b"".join(chunks)
                if request.get("action") != "screenshot":
                    return data
        return b"".join(chunks)


def wait_ready(proc: subprocess.Popen[str], timeout: float = 3.0) -> None:
    assert proc.stdout is not None
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = proc.stdout.readline()
        if "READY" in line:
            return
    raise RuntimeError("bridge_harness did not become ready")


def test_auth() -> None:
    port = 17951
    token = "contract-secret"
    proc = subprocess.Popen(
        [str(HARNESS), "--port", str(port), "--token", token],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_ready(proc)
        denied = json.loads(exchange(port, {"action": "status"}))
        assert denied.get("error") == "unauthorized", denied
        assert denied.get("ok") is False, denied
        ok = json.loads(exchange(port, {"action": "status", "token": token}))
        assert ok.get("connected") is True, ok
    finally:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)


def test_auth_optional_when_unset() -> None:
    port = 17953
    proc = subprocess.Popen(
        [str(HARNESS), "--port", str(port)],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_ready(proc)
        ok = json.loads(exchange(port, {"action": "status"}))
        assert ok.get("connected") is True, ok
    finally:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)


def test_nonblocking_accept() -> None:
    port = 17952
    proc = subprocess.Popen(
        [str(HARNESS), "--port", str(port), "--no-autocomplete"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_ready(proc)
        result: dict[str, object] = {}

        def waiter() -> None:
            try:
                result["wait"] = exchange(
                    port,
                    {
                        "action": "exec_lua_code",
                        "state": "server",
                        "code": "return 1",
                        "return_result": True,
                    },
                    timeout=2.0,
                )
            except Exception as exc:  # noqa: BLE001 - surface to main thread
                result["wait_err"] = repr(exc)

        thread = threading.Thread(target=waiter)
        thread.start()
        time.sleep(0.15)

        started = time.time()
        status = json.loads(exchange(port, {"action": "status"}, timeout=1.0))
        elapsed = time.time() - started
        assert status.get("connected") is True, status
        assert elapsed < 0.5, f"status blocked for {elapsed:.3f}s"
        thread.join(timeout=3)
        assert "wait" not in result or result.get("wait") == b"", result
    finally:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)


def main() -> int:
    if not HARNESS.is_file():
        print(f"missing {HARNESS}; build linux64 first", file=sys.stderr)
        return 1
    test_auth()
    print("ok: auth reject / accept with token")
    test_auth_optional_when_unset()
    print("ok: auth optional when token unset")
    test_nonblocking_accept()
    print("ok: accept stays free during waiting exec")
    return 0


if __name__ == "__main__":
    sys.exit(main())
