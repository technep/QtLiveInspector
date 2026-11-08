#!/usr/bin/env python3
"""qli_mcp.py — MCP (Model Context Protocol) server for QtLiveInspector.

Bridges MCP clients (Claude Desktop, and other MCP-capable AI tools) to a
QtLiveInspector probe running inside a Qt 6 application, so an AI model can
navigate the app: browse the object tree, read/write properties, click items,
invoke methods, take screenshots and generate Squish locators.

Transport: MCP over stdio (newline-delimited JSON-RPC 2.0). No dependencies
beyond the Python 3.9+ standard library.

Configuration via environment:
    QLI_HOST    probe host (default 127.0.0.1)
    QLI_PORT    probe port (default 17017)
    QLI_TOKEN   shared secret. Since probe v0.2 a token is REQUIRED unless
                the target was started with QLI_TOKEN explicitly empty; when
                the target has no QLI_TOKEN set it auto-generates one and
                prints it to the target application's log on startup.

Claude Desktop config example:
    {
      "mcpServers": {
        "qt-app": {
          "command": "python3",
          "args": ["/path/to/QtLiveInspector/tools/qli_mcp.py"],
          "env": {"QLI_HOST": "127.0.0.1", "QLI_PORT": "17017"}
        }
      }
    }
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probeclient import ProbeClient
from qli_version import __version__

SERVER_INFO = {"name": "qtliveinspector", "version": __version__}
FALLBACK_PROTOCOL_VERSION = "2025-03-26"

HOST = os.environ.get("QLI_HOST", "127.0.0.1")
PORT = int(os.environ.get("QLI_PORT", "17017"))
TOKEN = os.environ.get("QLI_TOKEN") or None


# ---------------------------------------------------------------------------
# Result formatting helpers
# ---------------------------------------------------------------------------

def text_result(text):
    return {"content": [{"type": "text", "text": text}]}


def error_result(text):
    return {"content": [{"type": "text", "text": f"Error: {text}"}], "isError": True}


def fmt_value(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, str):
        return v
    if isinstance(v, list):
        return f"[{len(v)} items]"
    if isinstance(v, dict):
        if "object" in v:
            s = v.get("className") or "QObject"
            if v.get("objectName"):
                s += f" '{v['objectName']}'"
            return s + f" [#{v['object']}]"
        if v.get("enum"):
            return str(v.get("text", v.get("value")))
        if "width" in v and "x" in v:
            return f"{v['x']},{v['y']} {v['width']}x{v['height']}"
        if "width" in v:
            return f"{v['width']}x{v['height']}"
        if "x" in v:
            return f"{v['x']},{v['y']}"
        return json.dumps(v, separators=(",", ":"))
    return str(v)


def fmt_tree(node, depth, max_depth, lines, budget):
    if len(lines) >= budget:
        return
    if depth > max_depth:
        if node.get("children") and depth == max_depth + 1:
            lines.append("  " * depth + "…")
        return
    name = node.get("name") or ""
    cls = node.get("className") or "?"
    label = f"{name} ({cls})" if name else cls
    lines.append("  " * depth + f"[{node.get('id')}] {label}")
    for child in node.get("children", []):
        fmt_tree(child, depth + 1, max_depth, lines, budget)


def walk(node):
    yield node
    for child in node.get("children", []):
        yield from walk(child)


def target_kwargs(args):
    if args.get("objectName"):
        return {"objectName": args["objectName"]}
    if args.get("node") is not None:
        return {"node": args["node"]}
    raise ValueError("provide either 'node' (id) or 'objectName'")


# ---------------------------------------------------------------------------
# Tool implementations
# ---------------------------------------------------------------------------

def call_tool(probe, name, args):
    args = args or {}

    if name == "get_app_info":
        res = probe.hello()
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result(
            f"Application: {res.get('app')}\nQt version: {res.get('qt')}\n"
            f"PID: {res.get('pid')}\nProbe version: {res.get('probeVersion')}\n"
            f"Protocol version: {res.get('protocolVersion', 'unknown (pre-0.2 probe)')}")

    if name == "get_object_tree":
        max_depth = min(int(args.get("maxDepth", 4)), 20)
        res = probe.command("tree")
        if not res.get("ok"):
            return error_result(res.get("error"))
        lines = []
        fmt_tree(res["root"], 0, max_depth, lines, budget=1000)
        if len(lines) >= 1000:
            lines.append("… (output truncated; use find_objects or a smaller maxDepth)")
        return text_result("\n".join(lines))

    if name == "find_objects":
        query = (args.get("query") or "").lower()
        if not query:
            return error_result("missing 'query'")
        res = probe.command("tree")
        if not res.get("ok"):
            return error_result(res.get("error"))
        hits = []
        for n in walk(res["root"]):
            if query in (n.get("name") or "").lower() or query in (n.get("className") or "").lower():
                hits.append(f"[{n.get('id')}] {(n.get('name') or '')} "
                            f"({n.get('className')}) {n.get('address', '')}")
            if len(hits) >= 50:
                break
        if not hits:
            return text_result(f"No objects matching '{args.get('query')}'.")
        out = "\n".join(hits)
        if len(hits) == 50:
            out += "\n… (truncated at 50 matches)"
        return text_result(out)

    if name == "get_properties":
        res = probe.command("properties", **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        lines = []
        for p in res.get("properties", []):
            flags = "".join([" W" if p.get("writable") else "",
                             " N" if p.get("notify") else "",
                             " C" if p.get("constant") else ""])
            lines.append(f"{p['name']} ({p['type']}) = {fmt_value(p.get('value'))}{flags}")
        return text_result("\n".join(lines) or "No properties.")

    if name == "set_property":
        res = probe.command("setProperty", name=args.get("name"),
                            value=args.get("value"), **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result(f"OK, new value: {fmt_value(res.get('value'))}")

    if name == "invoke_method":
        res = probe.command("invoke", method=args.get("method"),
                            args=args.get("args") or [], **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result(f"Invoked {args.get('method')} successfully.")

    if name == "click":
        res = probe.command("click", **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result("Clicked.")

    if name == "type_text":
        res = probe.command("typeText", text=args.get("text", ""),
                            **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result("Text typed.")

    if name == "highlight":
        seconds = min(float(args.get("seconds", 5.0)), 30.0)
        res = probe.command("highlight", **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        time.sleep(seconds)
        probe.command("clearHighlight")
        return text_result(f"Highlighted for {seconds:g}s.")

    if name == "clear_highlight":
        res = probe.command("clearHighlight")
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result("Highlight cleared.")

    if name == "get_squish_locator":
        res = probe.command("locator", **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result(res.get("locator", ""))

    if name == "pick_object":
        timeout = min(float(args.get("timeoutSeconds", 60)), 300.0)
        res = probe.command("pickStart")
        if not res.get("ok"):
            return error_result(res.get("error"))
        ev = probe.wait_event("picked", timeout)
        if ev is None:
            probe.command("pickStop")
            return error_result("no pick within the timeout — was anything clicked?")
        node = ev.get("node")
        info = probe.command("properties", node=node)
        desc = ""
        if info.get("ok"):
            for p in info.get("properties", []):
                if p["name"] == "objectName":
                    desc = fmt_value(p.get("value"))
        return text_result(
            f"User picked node {node}"
            + (f" (objectName '{desc}')" if desc else "")
            + f"\nid path: {ev.get('path')}")

    if name == "watch_properties":
        seconds = min(float(args.get("seconds", 5.0)), 30.0)
        res = probe.command("watch", **target_kwargs(args))
        if not res.get("ok"):
            return error_result(res.get("error"))
        changes = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ev = probe.wait_event("propertyChanged", deadline - time.monotonic())
            if ev is None:
                break
            changes.append(f"{ev.get('property')} = {fmt_value(ev.get('value'))}")
            if len(changes) >= 200:
                changes.append("… (truncated)")
                break
        probe.command("unwatch")
        if not changes:
            return text_result(f"Watched {res.get('watching')} properties for "
                               f"{seconds:g}s — no changes.")
        return text_result(f"{len(changes)} change(s) in {seconds:g}s:\n" + "\n".join(changes))

    if name == "set_input_visualizer":
        enabled = bool(args.get("enabled", True))
        kwargs = {"enabled": enabled}
        if "multiTouch" in args:
            kwargs["multiTouch"] = bool(args.get("multiTouch"))
        res = probe.command("setInputVisualizer", **kwargs)
        if not res.get("ok"):
            return error_result(res.get("error"))
        mode = "all touches" if res.get("multiTouch") else "first touch only"
        return text_result(
            f"Input visualizer {'enabled' if res.get('enabled') else 'disabled'} "
            f"({mode}).")

    if name == "get_perf_stats":
        res = probe.command("perfStats")
        if not res.get("ok"):
            return error_result(res.get("error"))
        return text_result(f"FPS: {res.get('fps', 0):.1f}\n"
                           f"Event Loop Delay: {res.get('eventLoopDelayMs', 0):.2f} ms\n"
                           f"Window Count: {res.get('windowCount', 0)}\n"
                           f"Is Idle: {res.get('isIdle', False)}")

    if name == "wait_until_idle":
        timeout = float(args.get("timeoutSeconds", 5.0))
        max_delay = float(args.get("maxDelayMs", 25.0))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            res = probe.command("isIdle", maxDelayMs=max_delay)
            if res.get("ok") and res.get("idle"):
                return text_result(f"Target is idle (delay: {res.get('eventLoopDelayMs', 0):.2f} ms).")
            time.sleep(0.1)
        return error_result(f"Target did not reach idle state within {timeout}s.")

    if name == "screenshot":
        kwargs = {}
        if args.get("objectName") or args.get("node") is not None:
            kwargs = target_kwargs(args)
        res = probe.command("screenshot", **kwargs)
        if not res.get("ok"):
            return error_result(res.get("error"))
        return {"content": [
            {"type": "image", "data": res["png"], "mimeType": "image/png"},
            {"type": "text",
             "text": f"Screenshot {res.get('width')}x{res.get('height')}"},
        ]}

    return error_result(f"unknown tool '{name}'")


# ---------------------------------------------------------------------------
# Tool schemas
# ---------------------------------------------------------------------------

_TARGET_PROPS = {
    "node": {"type": "integer",
             "description": "Runtime object id (from get_object_tree, "
                            "find_objects or pick_object)"},
    "objectName": {"type": "string",
                   "description": "objectName of the target object; must match "
                                  "exactly one object"},
}

def _target_tool(name, description, extra_props=None, required=None):
    props = dict(_TARGET_PROPS)
    props.update(extra_props or {})
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": props,
                            "required": required or []}}


TOOLS = [
    {"name": "get_app_info",
     "description": "Get info about the connected Qt application (name, Qt version, PID).",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "get_object_tree",
     "description": "Get the live QObject tree of the Qt application (indented text, "
                    "each line: [node-id] objectName (ClassName)). Use maxDepth to "
                    "control depth; use find_objects to search deep trees.",
     "inputSchema": {"type": "object",
                     "properties": {"maxDepth": {"type": "integer", "default": 4}}}},
    {"name": "find_objects",
     "description": "Search all objects by objectName or class name (substring, "
                    "case-insensitive). Returns matching node ids.",
     "inputSchema": {"type": "object",
                     "properties": {"query": {"type": "string"}},
                     "required": ["query"]}},
    _target_tool("get_properties",
                 "List all properties of an object with current values. Flags: "
                 "W=writable, N=has notify signal, C=constant. "
                 "Address the object by node id or objectName."),
    _target_tool("set_property",
                 "Write a property of an object (scalar values, enums by name).",
                 extra_props={"name": {"type": "string", "description": "property name"},
                              "value": {"description": "new value (JSON scalar)"}},
                 required=["name", "value"]),
    _target_tool("invoke_method",
                 "Call an invokable method or slot on an object, or emit one of its "
                 "signals (e.g. 'clicked'). Up to 6 JSON arguments, converted to the "
                 "declared parameter types.",
                 extra_props={"method": {"type": "string"},
                              "args": {"type": "array", "items": {}}},
                 required=["method"]),
    _target_tool("click",
                 "Click an object: animateClick() for widget buttons, otherwise a "
                 "synthesized mouse press+release at the center of the item/widget."),
    _target_tool("type_text",
                 "Give an object keyboard focus and type text into it with "
                 "synthesized key events (text fields, editors).",
                 extra_props={"text": {"type": "string"}},
                 required=["text"]),
    _target_tool("highlight",
                 "Draw a pulsing red rectangle around an object in the target app "
                 "for a few seconds.",
                 extra_props={"seconds": {"type": "number", "default": 5}}),
    {"name": "clear_highlight",
     "description": "Remove the red highlight rectangle.",
     "inputSchema": {"type": "object", "properties": {}}},
    _target_tool("get_squish_locator",
                 "Generate a Squish-compatible multi-property object locator for an "
                 "object (for test automation with froglogic Squish)."),
    {"name": "pick_object",
     "description": "Arm the interactive picker in the target application: the next "
                    "element the user clicks is reported (node id + path). Use this "
                    "to ask the user 'which element do you mean?'.",
     "inputSchema": {"type": "object",
                     "properties": {"timeoutSeconds": {"type": "number", "default": 60}}}},
    _target_tool("watch_properties",
                 "Watch all notify-capable properties of an object for a few seconds "
                 "and report every change (live property monitoring).",
                 extra_props={"seconds": {"type": "number", "default": 5}}),
    _target_tool("screenshot",
                 "Take a screenshot of the target application's window (PNG image). "
                 "Optionally pass a specific window/item/widget as target; without a "
                 "target the first visible window is captured."),
    {"name": "set_input_visualizer",
     "description": "Enable or disable touch and mouse input visualization overlays in the target application. "
                    "By default only the first touch is shown; multiTouch=true tracks every finger.",
     "inputSchema": {"type": "object", "properties": {"enabled": {"type": "boolean", "default": True}, "multiTouch": {"type": "boolean"}}}},
    {"name": "get_perf_stats",
     "description": "Get real-time performance telemetry (FPS, main thread event loop delay, window count, idle state).",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "wait_until_idle",
     "description": "Wait until the target application's main GUI thread event loop is responsive and idle.",
     "inputSchema": {"type": "object", "properties": {"timeoutSeconds": {"type": "number", "default": 5.0}, "maxDelayMs": {"type": "number", "default": 25.0}}}},
]


# ---------------------------------------------------------------------------
# MCP stdio loop (newline-delimited JSON-RPC 2.0)
# ---------------------------------------------------------------------------

def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def reply(mid, result):
    send({"jsonrpc": "2.0", "id": mid, "result": result})


def reply_error(mid, code, message):
    send({"jsonrpc": "2.0", "id": mid, "error": {"code": code, "message": message}})


def main():
    probe = ProbeClient(HOST, PORT, TOKEN, lazy=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(msg, dict) or "method" not in msg:
            continue

        method = msg["method"]
        mid = msg.get("id")

        if method == "initialize":
            params = msg.get("params") or {}
            reply(mid, {
                "protocolVersion": params.get("protocolVersion",
                                              FALLBACK_PROTOCOL_VERSION),
                "capabilities": {"tools": {}},
                "serverInfo": SERVER_INFO,
            })
        elif method == "notifications/initialized":
            pass
        elif method == "ping":
            reply(mid, {})
        elif method == "tools/list":
            reply(mid, {"tools": TOOLS})
        elif method == "tools/call":
            params = msg.get("params") or {}
            try:
                result = call_tool(probe, params.get("name"),
                                   params.get("arguments") or {})
            except (ConnectionError, ValueError) as e:
                result = error_result(str(e))
            except Exception as e:  # never let the loop die on a tool error
                result = error_result(f"{type(e).__name__}: {e}")
            reply(mid, result)
        elif method == "resources/list":
            reply(mid, {"resources": []})
        elif method == "prompts/list":
            reply(mid, {"prompts": []})
        elif mid is not None:
            reply_error(mid, -32601, f"method not found: {method}")


if __name__ == "__main__":
    main()
