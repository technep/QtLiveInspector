#!/usr/bin/env python3
"""probeclient.py — shared client for the QtLiveInspector probe protocol.

Used by qli_cli.py, qli_mcp.py and the BDD runner (tools/bdd). Speaks
newline-delimited JSON over TCP; see protocol.txt.

Configuration comes from the target app: since v0.2 the probe binds to
localhost only and requires a token unless explicitly configured otherwise
(see README "Security"). Point the clients at it with:

    QLI_TOKEN=<token>            # token printed to the target app's log
    qli_cli.py --host <addr> --port <port> ...
"""
import json
import socket
import sys
import time

DEFAULT_PORT = 17017

# Wire-protocol version spoken by this client; negotiated in the hello
# handshake (see protocol.txt). Major version must match the probe's.
# 1.1: added mouseDown / mouseUp / mouseMove.
PROTOCOL_VERSION = "1.1"


class ProbeClient:
    def __init__(self, host="127.0.0.1", port=DEFAULT_PORT, token=None,
                 timeout=15.0, lazy=True):
        self.host, self.port, self.token = host, port, token
        self.timeout = timeout
        self.sock = None
        self.buf = b""
        self.next_id = 1
        if not lazy:
            self.ensure()

    # -- connection ---------------------------------------------------------

    def ensure(self):
        if self.sock is None:
            self.sock = socket.create_connection((self.host, self.port),
                                                 timeout=self.timeout)

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None
        self.buf = b""

    # -- protocol -----------------------------------------------------------

    def _read_msg(self, timeout=None):
        # timeout=None means block indefinitely.
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise ConnectionError("probe closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def command(self, cmd, _retry=True, **args):
        rid = self.next_id
        self.next_id += 1
        msg = {"id": rid, "cmd": cmd, **args}
        if self.token:
            msg["token"] = self.token
        try:
            self.ensure()
            self.sock.sendall(json.dumps(msg).encode() + b"\n")
            while True:
                res = self._read_msg(self.timeout)
                if res.get("type") == "event":
                    continue
                if res.get("id") == rid:
                    return res
        except (OSError, ConnectionError) as e:
            self.close()
            if _retry:
                return self.command(cmd, _retry=False, **args)
            raise ConnectionError(
                f"cannot reach probe at {self.host}:{self.port} — is the "
                f"target app running with the probe injected? ({e})") from e

    def wait_event(self, name, timeout=None):
        """Wait for a server-push event; None timeout = wait forever."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            remaining = None if deadline is None else deadline - time.monotonic()
            if remaining is not None and remaining <= 0:
                return None
            try:
                msg = self._read_msg(timeout=remaining)
            except socket.timeout:
                return None
            if msg.get("type") == "event" and msg.get("event") == name:
                return msg

    def hello(self):
        """Handshake: announce our protocol version and sanity-check the
        probe's. Returns the hello response (never None)."""
        res = self.command("hello", protocolVersion=PROTOCOL_VERSION)
        if res.get("ok"):
            probe_pv = res.get("protocolVersion")
            if probe_pv is None:
                print("warning: probe does not report a protocolVersion — "
                      "it predates protocol negotiation (0.1.x); commands may "
                      "fail if the protocol has changed since", file=sys.stderr)
            elif str(probe_pv).split(".")[0] != PROTOCOL_VERSION.split(".")[0]:
                print(f"warning: protocol major mismatch — client speaks "
                      f"{PROTOCOL_VERSION}, probe speaks {probe_pv}", file=sys.stderr)
        return res

    # -- convenience helpers --------------------------------------------------

    def check(self, res):
        if not res.get("ok"):
            raise RuntimeError(res.get("error", "unknown probe error"))
        return res

    def find_by_name(self, object_name):
        """Return all tree nodes with this objectName."""
        root = self.check(self.command("tree"))["root"]
        hits = []

        def walk(node):
            if node.get("name") == object_name:
                hits.append(node)
            for child in node.get("children", []):
                walk(child)

        walk(root)
        return hits

    def get_property(self, name, target):
        """Read one property of an object addressed by {"node": id} or
        {"objectName": name}. Returns the probe's JSON value."""
        res = self.check(self.command("properties", **target))
        for p in res.get("properties", []):
            if p["name"] == name:
                return p.get("value")
        raise RuntimeError(f"no such property: {name}")


def target_kwargs(node=None, object_name=None):
    if object_name:
        return {"objectName": object_name}
    if node is not None:
        return {"node": node}
    raise ValueError("provide node id or object_name")
