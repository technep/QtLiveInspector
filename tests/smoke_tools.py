#!/usr/bin/env python3
"""smoke_tools.py — fast smoke tests for the Python tooling.

Catches the "file doesn't even parse" and "protocol handshake broken"
classes of regression in seconds, without a real Qt application:

  phase 1  py_compile every tool (CLI, MCP server, monkey, BDD runner, …)
  phase 2  qli_cli.py + qli_mcp.py against a stub probe (no auth)
  phase 3  token enforcement: stub probe requiring a token

Exit code 0 = all checks passed.
"""
import json
import os
import py_compile
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, HERE)
sys.path.insert(0, TOOLS)

from stub_probe import StubProbe  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    status = "ok" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILURES.append(name)


def run_cli(args, env_extra=None, expect_rc=0):
    env = dict(os.environ)
    env.pop("QLI_TOKEN", None)
    if env_extra:
        env.update(env_extra)
    p = subprocess.run([sys.executable, os.path.join(TOOLS, "qli_cli.py"),
                        *args],
                       capture_output=True, text=True, timeout=30, env=env)
    return p.returncode, p.stdout + p.stderr


class McpClient:
    """Drives qli_mcp.py over stdio, like a real MCP client."""

    def __init__(self, env_extra=None):
        env = dict(os.environ)
        env.pop("QLI_TOKEN", None)
        if env_extra:
            env.update(env_extra)
        self.p = subprocess.Popen(
            [sys.executable, os.path.join(TOOLS, "qli_mcp.py")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
            bufsize=1, env=env)
        self.mid = 0

    def call(self, method, params=None):
        self.mid += 1
        self.p.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "id": self.mid, "method": method,
             "params": params or {}}) + "\n")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("MCP server exited unexpectedly")
            msg = json.loads(line)
            if msg.get("id") == self.mid:
                return msg

    def tool(self, name, **args):
        res = self.call("tools/call", {"name": name, "arguments": args})
        return res.get("result", {})

    def close(self):
        self.p.terminate()


def phase_compile():
    print("== phase 1: py_compile all tools ==")
    for dirpath, _dirnames, filenames in os.walk(TOOLS):
        if "__pycache__" in dirpath:
            continue
        for fn in sorted(filenames):
            if fn.endswith(".py"):
                path = os.path.join(dirpath, fn)
                try:
                    py_compile.compile(path, doraise=True)
                    check(f"compile {os.path.relpath(path, ROOT)}", True)
                except py_compile.PyCompileError as e:
                    check(f"compile {os.path.relpath(path, ROOT)}", False, str(e))


def phase_stub_noauth():
    print("== phase 2: CLI + MCP against stub probe (no auth) ==")
    with StubCtx(None) as (port, env):
        rc, out = run_cli(["--port", str(port), "hello"], env)
        check("cli hello rc", rc == 0, out[:200])
        check("cli hello protocolVersion",
              '"protocolVersion": "1.1"' in out, out[:300])

        rc, out = run_cli(["--port", str(port), "tree"], env)
        check("cli tree rc", rc == 0 and "stubButton" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "find", "button"], env)
        check("cli find rc", rc == 0 and "stubButton" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "props", "--name",
                           "nameField"], env)
        check("cli props rc", rc == 0 and "text" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "set", "--name", "nameField",
                           "text", '"hi"'], env)
        check("cli set rc", rc == 0 and '"hi"' in out, out[:200])

        rc, out = run_cli(["--port", str(port), "click", "--name",
                           "stubButton"], env)
        check("cli click rc", rc == 0, out[:200])

        rc, out = run_cli(["--port", str(port), "perf"], env)
        check("cli perf rc", rc == 0 and "FPS" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "touchvis"], env)
        check("cli touchvis rc", rc == 0 and "enabled" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "locator", "--name",
                           "stubButton"], env)
        check("cli locator rc", rc == 0 and "stubButton" in out, out[:200])

        mcp = McpClient(env)
        try:
            r = mcp.call("initialize")["result"]
            check("mcp initialize", r.get("serverInfo", {}).get("name")
                  == "qtliveinspector", str(r))

            r = mcp.call("tools/list")["result"]
            names = {t["name"] for t in r.get("tools", [])}
            check("mcp tools/list >= 15 tools", len(names) >= 15, str(names))
            check("mcp tools/list includes screenshot",
                  "screenshot" in names, str(names))

            res = mcp.tool("get_app_info")
            txt = res.get("content", [{}])[0].get("text", "")
            check("mcp get_app_info",
                  "StubApp" in txt and "Protocol version: 1.1" in txt, txt[:200])

            # get_perf_stats was the tool with the fatal f-string bug.
            res = mcp.tool("get_perf_stats")
            txt = res.get("content", [{}])[0].get("text", "")
            check("mcp get_perf_stats", "FPS" in txt and "Event Loop" in txt,
                  txt[:200])

            res = mcp.tool("click", objectName="stubButton")
            txt = res.get("content", [{}])[0].get("text", "")
            check("mcp click", not res.get("isError") and "Clicked" in txt,
                  txt[:200])

            res = mcp.tool("screenshot")
            types = [c.get("type") for c in res.get("content", [])]
            check("mcp screenshot image content", "image" in types, str(types))

            # protocol mismatch must surface as an explicit error
            res = mcp.tool("get_squish_locator", objectName="stubButton")
            txt = res.get("content", [{}])[0].get("text", "")
            check("mcp locator", "stubButton" in txt, txt[:200])
        finally:
            mcp.close()


class StubCtx:
    def __init__(self, token):
        self.token = token
        self.probe = None
        self.env = None
        self.port = None

    def __enter__(self):
        self.probe = StubProbe(token=self.token).start()
        self.port = self.probe.port
        self.env = {"QLI_HOST": "127.0.0.1", "QLI_PORT": str(self.port)}
        if self.token is not None:
            self.env["QLI_TOKEN"] = self.token
        return self.probe.port, self.env

    def __exit__(self, *exc):
        self.probe.stop()
        return False


def phase_token_enforced():
    print("== phase 3: token enforcement ==")
    token = "s3cret-smoke-token"
    with StubCtx(token) as (port, env):
        rc, out = run_cli(["--port", str(port), "hello"],
                          {"QLI_HOST": "127.0.0.1", "QLI_PORT": str(port)})
        check("cli hello without token rejected",
              rc != 0 and "unauthorized" in out, out[:200])

        rc, out = run_cli(["--port", str(port), "hello"], env)
        check("cli hello with $QLI_TOKEN accepted",
              rc == 0 and '"app": "StubApp"' in out, out[:200])

        rc, out = run_cli(["--port", str(port), "--token", "wrong", "click",
                           "--name", "stubButton"], env)
        check("cli explicit wrong --token rejected",
              rc != 0 and "unauthorized" in out, out[:200])

        mcp = McpClient({"QLI_HOST": "127.0.0.1", "QLI_PORT": str(port)})
        try:
            res = mcp.tool("get_app_info")
            check("mcp without token rejected", res.get("isError") is True,
                  str(res)[:200])
        finally:
            mcp.close()

        mcp = McpClient(env)
        try:
            res = mcp.tool("get_app_info")
            txt = res.get("content", [{}])[0].get("text", "")
            check("mcp with $QLI_TOKEN accepted", "StubApp" in txt,
                  txt[:200])
        finally:
            mcp.close()


def main():
    phase_compile()
    phase_stub_noauth()
    phase_token_enforced()
    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print("ALL SMOKE TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
