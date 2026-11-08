#!/usr/bin/env python3
"""stub_probe.py — minimal fake QtLiveInspector probe for tool smoke tests.

Implements just enough of the newline-delimited JSON protocol (see
protocol.txt) for qli_cli.py / qli_mcp.py / qli_monkey.py to run against it
without a real Qt application. This is NOT a protocol conformance suite —
tests/integration_probe.py exercises the real probe end to end.

Usage:
    probe = StubProbe(token=None).start()
    ... run clients against 127.0.0.1:probe.port ...
    probe.stop()
"""
import base64
import json
import socketserver
import threading

PROTOCOL_VERSION = "1.1"

# 1x1 red PNG — just enough for clients to decode something.
_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQ"
    "DwAEhQGAhKmMIQAAAABJRU5ErkJggg==")


def make_tree():
    return {
        "id": 1, "name": "rootObj", "className": "QObject",
        "isQuickItem": False, "isWidget": False,
        "children": [
            {"id": 2, "name": "stubButton", "className": "QQuickItem_QML_1",
             "isQuickItem": True, "isWidget": False, "children": []},
            {"id": 3, "name": "nameField", "className": "TextField_QMLTYPE_1",
             "isQuickItem": True, "isWidget": False, "children": []},
        ],
    }


class StubProbe:
    """Serves the probe protocol on 127.0.0.1:<ephemeral port>."""

    def __init__(self, token=None):
        self.token = token
        self.text = ""
        self.clicks = 0
        self.viz = False
        outer = self

        class Handler(socketserver.StreamRequestHandler):
            def send(self, obj):
                self.wfile.write(json.dumps(obj).encode() + b"\n")

            def handle(self):
                for line in self.rfile:
                    try:
                        req = json.loads(line)
                    except json.JSONDecodeError:
                        self.send({"id": 0, "type": "response", "ok": False,
                                   "error": "malformed JSON"})
                        continue
                    if not isinstance(req, dict):
                        self.send({"id": 0, "type": "response", "ok": False,
                                   "error": "malformed JSON"})
                        continue
                    self.send(outer.dispatch(req))

        self._server = socketserver.ThreadingTCPServer(("127.0.0.1", 0),
                                                       Handler)
        self._server.daemon_threads = True
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        daemon=True)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._server.shutdown()
        self._server.server_close()

    def dispatch(self, req):
        rid = req.get("id", 0)
        cmd = req.get("cmd", "")

        if self.token is not None and req.get("token") != self.token:
            return {"id": rid, "type": "response", "ok": False,
                    "error": "unauthorized: invalid or missing token"}

        if cmd == "hello":
            wanted = str(req.get("protocolVersion", PROTOCOL_VERSION))
            if wanted.split(".")[0] != PROTOCOL_VERSION.split(".")[0]:
                return {"id": rid, "type": "response", "ok": False,
                        "error": f"protocol version mismatch: client speaks "
                                 f"{wanted}, probe speaks {PROTOCOL_VERSION}"}
            return {"id": rid, "type": "response", "ok": True, "app": "StubApp",
                    "qt": "6.8.3", "pid": 4242, "probeVersion": "0.2.0",
                    "protocolVersion": PROTOCOL_VERSION}

        if cmd == "tree":
            return {"id": rid, "type": "response", "ok": True,
                    "root": make_tree()}

        if cmd == "properties":
            target = req.get("objectName") or f"node {req.get('node')}"
            return {"id": rid, "type": "response", "ok": True, "properties": [
                {"name": "objectName", "type": "QString", "value": target,
                 "writable": True, "notify": True},
                {"name": "text", "type": "QString", "value": self.text,
                 "writable": True, "notify": True},
            ]}

        if cmd == "setProperty":
            self.text = req.get("value")
            return {"id": rid, "type": "response", "ok": True,
                    "value": self.text}

        if cmd == "click":
            self.clicks += 1
            return {"id": rid, "type": "response", "ok": True}

        if cmd == "typeText":
            self.text += req.get("text", "")
            return {"id": rid, "type": "response", "ok": True}

        if cmd == "invoke":
            return {"id": rid, "type": "response", "ok": True}

        if cmd == "perfStats":
            return {"id": rid, "type": "response", "ok": True, "fps": 60.0,
                    "eventLoopDelayMs": 0.1, "windowCount": 1, "isIdle": True}

        if cmd == "isIdle":
            return {"id": rid, "type": "response", "ok": True, "idle": True,
                    "eventLoopDelayMs": 0.1}

        if cmd == "screenshot":
            return {"id": rid, "type": "response", "ok": True,
                    "png": base64.b64encode(_PNG).decode(),
                    "width": 1, "height": 1}

        if cmd == "locator":
            return {"id": rid, "type": "response", "ok": True,
                    "locator": "{objectName='stubButton'}"}

        if cmd == "setInputVisualizer":
            self.viz = bool(req.get("enabled", True))
            return {"id": rid, "type": "response", "ok": True,
                    "enabled": self.viz,
                    "multiTouch": bool(req.get("multiTouch", False))}

        if cmd in ("highlight", "clearHighlight", "unwatch", "pickStop",
                   "mouseDown", "mouseUp", "mouseMove"):
            return {"id": rid, "type": "response", "ok": True}

        if cmd == "watch":
            return {"id": rid, "type": "response", "ok": True, "watching": 2}

        if cmd == "pickStart":
            return {"id": rid, "type": "response", "ok": True}

        return {"id": rid, "type": "response", "ok": False,
                "error": f"unknown command: {cmd}"}
