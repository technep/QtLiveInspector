#!/usr/bin/env python3
"""integration_probe.py — end-to-end test: real probe injected into quickdemo.

Runs the sample application with libqtinspectorprobe preloaded under the
offscreen platform and exercises the security + robustness + protocol
features against the live process:

    basic     auto-generated token is read from the app log (fail-safe auth),
              localhost binding, protocol handshake, property roundtrip,
              synthesized click, screenshot, audit trail, and a hostile-input
              barrage (malformed JSON, unknown command, protocol mismatch,
              oversized message) after which the probe must still answer.
    readonly  QLI_READONLY=1: read commands pass, every mutating command is
              rejected with a clear error.
    noauth    QLI_TOKEN explicitly empty: development no-auth mode works.
    vizenv    QLI_INPUT_VIZ=1 (via --set): the overlay is shown before any
              client connects, interaction still works, protocol can
              disable it again.

Usage (driven by CTest):
    python3 integration_probe.py --demo <quickdemo> --probe-so <lib...so>
        --port 17317 --mode basic

Profiles select the small behavioral differences between the sample apps
(all three expose the SAME objectNames by design):

    demo (default)  Qt Quick ApplicationWindow target (quickdemo)
    quickview       same QML surface hosted in a QQuickView
    widgets         Qt Widgets counterpart (widgetdemo): native combo box,
                    QLineEdit focus via the setFocus slot
"""
import argparse
import base64
import os
import re
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
from probeclient import ProbeClient, PROTOCOL_VERSION  # noqa: E402

FAILURES = []

PROFILES = {
    "demo": {"focus_method": "forceActiveFocus", "combo": "qml"},
    "quickview": {"focus_method": "forceActiveFocus", "combo": "qml"},
    "widgets": {"focus_method": "setFocus", "combo": "native"},
}


def check(name, cond, detail=""):
    print(f"[{'ok' if cond else 'FAIL'}] {name}"
          + (f" — {detail}" if detail and not cond else ""), flush=True)
    if not cond:
        FAILURES.append(name)


class Target:
    """quickdemo + probe, with the app's stderr captured in the background."""

    def __init__(self, demo, probe_so, port, mode, extra_env=None):
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["QLI_PORT"] = str(port)
        # Sanitizer builds (see tests/CMakeLists.txt): the runtime must be
        # preloaded BEFORE the probe or the target dies at startup.
        extra = os.environ.get("QLI_TEST_PRELOAD_EXTRA", "")
        env["LD_PRELOAD"] = (extra + ":" + probe_so) if extra else probe_so
        if mode in ("basic", "vizenv"):
            env.pop("QLI_TOKEN", None)      # exercise auto-generation
            env.pop("QLI_READONLY", None)
        elif mode == "readonly":
            env["QLI_TOKEN"] = "itest-readonly-token"
            env["QLI_READONLY"] = "1"
        elif mode == "noauth":
            env["QLI_TOKEN"] = ""           # explicitly open (dev mode)
            env.pop("QLI_READONLY", None)
        else:
            raise ValueError(mode)
        # Test-controlled overrides (e.g. QLI_INPUT_VIZ=1 for vizenv),
        # applied after the mode setup so they win.
        for key, value in (extra_env or {}).items():
            env[key] = value

        self.proc = subprocess.Popen(
            [os.path.abspath(demo)], cwd=os.path.dirname(os.path.abspath(demo)),
            env=env, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, bufsize=1)
        self.lines = []
        self.token = None
        self.ready = threading.Event()

        def reader():
            for line in self.proc.stderr:
                self.lines.append(line)
                # QLI_TOKEN may be any non-empty string (readonly mode uses
                # "itest-readonly-token"), so don't restrict the capture to
                # the hex shape of auto-generated tokens.
                m = re.search(r"auth token: (\S+)", line)
                if m:
                    self.token = m.group(1)
                    self.ready.set()
                elif "probe active" in line and "no auth" in line:
                    # No-auth mode never prints an auth-token line; its
                    # warning is part of the banner line itself.
                    self.ready.set()

        self._thread = threading.Thread(target=reader, daemon=True)
        self._thread.start()

    def wait_ready(self, timeout=20.0):
        return self.ready.wait(timeout)

    @property
    def log(self):
        return "".join(self.lines)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def raw_request(port, payload, read_response=True, timeout=5.0):
    """Send one raw line, return the parsed response (or None)."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        if isinstance(payload, str):
            payload = payload.encode()
        s.sendall(payload)
        if not read_response:
            return None
        f = s.makefile("rb")
        line = f.readline()
        if not line:
            return None
        import json
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            return {"_raw": line.decode(errors="replace")}


def test_basic(target, port, profile):
    log_head = "\n".join(target.lines[:3])
    check("banner: bound to localhost",
          "127.0.0.1" in log_head, log_head)
    check("banner: token required", "[token required]" in log_head, log_head)
    check("auto-generated token captured", bool(target.token))

    token = target.token
    cli = ProbeClient(port=port, token=token, timeout=10, lazy=False)

    res = cli.hello()
    check("hello ok", res.get("ok") is True, str(res)[:200])
    check("hello protocolVersion is 1.0",
          res.get("protocolVersion") == PROTOCOL_VERSION, str(res)[:200])

    r = raw_request(port,
                    '{"id": 2, "cmd": "hello", "token": "wrong-token"}\n')
    check("wrong token rejected", r and r.get("ok") is False
          and "unauthorized" in r.get("error", ""), str(r)[:200])

    res = cli.command("hello", protocolVersion="99.0")
    check("protocol major mismatch rejected", res.get("ok") is False
          and "protocol version mismatch" in res.get("error", ""),
          str(res)[:200])

    root = cli.check(cli.command("tree"))["root"]
    names = []

    def walk(n):
        names.append(n.get("name") or "")
        for c in n.get("children", []):
            walk(c)

    walk(root)
    check("tree contains demo objects",
          {"clickButton", "nameField", "volumeSlider"} <= set(names),
          str(names[:10]))

    props = cli.check(cli.command("properties",
                                  objectName="nameField"))["properties"]
    check("properties readable",
          any(p["name"] == "text" for p in props), str(props)[:200])

    cli.check(cli.command("setProperty", objectName="nameField",
                          name="text", value="itest-basic"))
    value = cli.get_property("text", {"objectName": "nameField"})
    check("setProperty roundtrip", value == "itest-basic", str(value))

    # ---- input visualizer monkey mode: style command, enabled untouched ----
    viz = cli.check(cli.command("setInputVisualizer", monkey=True))
    check("monkey mode on", viz.get("monkey") is True, str(viz))
    check("monkey mode did not force-enable",
          viz.get("enabled") is False, str(viz))
    viz = cli.check(cli.command("setInputVisualizer"))
    check("visualizer state queryable", "monkey" in viz and "enabled" in viz,
          str(viz))
    viz = cli.check(cli.command("setInputVisualizer", monkey=False))
    check("monkey mode restored", viz.get("monkey") is False, str(viz))

    cli.check(cli.command("click", objectName="clickButton"))
    time.sleep(0.6)  # widget buttons emit clicked ~100ms after animateClick
    text = cli.get_property("text", {"objectName": "clickButton"})
    check("synthesized click landed",
          isinstance(text, str) and "Clicked 1 times" in text, str(text))

    cli.check(cli.command("invoke", objectName="nameField",
                          method=PROFILES[profile]["focus_method"]))
    check("invoke method ok", True)

    shot = cli.check(cli.command("screenshot"))
    png = base64.b64decode(shot.get("png", ""))
    check("screenshot captured", len(png) > 1000 and png[:4] == b"\x89PNG",
          f"{len(png)} bytes")

    # ---- popup overlay scenario: dialog, popup, combobox dropdown --------
    # The dialog's content lives in the QQuickOverlay layer; its buttons are
    # only visually (not QObject-) parented into the scene — the tree and the
    # name resolution must still find them (visual-parenthood traversal).
    cli.check(cli.command("click", objectName="settingsButton"))
    time.sleep(0.3)
    okProps = cli.check(cli.command("properties",
                                    objectName="dialogOkButton"))["properties"]
    okVisible = next((p.get("value") for p in okProps if p["name"] == "visible"),
                     None)
    check("dialog button reachable while dialog open", okVisible is True,
          str(okProps)[:200])
    cli.check(cli.command("click", objectName="dialogOkButton"))
    time.sleep(0.6)
    text = cli.get_property("text", {"objectName": "statusLabel"})
    check("dialog accepted by click inside overlay",
          "dialog:accepted" in str(text), str(text))

    if profile != "widgets":
        # The widgets target has no popover; dialogs above cover its overlays.
        cli.check(cli.command("click", objectName="actionButton"))
        time.sleep(0.3)
        cli.check(cli.command("click", objectName="popoverAction1"))
        time.sleep(0.2)
        text = cli.get_property("text", {"objectName": "statusLabel"})
        check("popup action clicked", "popover:1" in str(text), str(text))

    # ComboBox: the QML targets open a popup with named delegates (a real
    # popup-overlay workout); the widgets target uses the native dropdown,
    # so it is driven via the currentIndex property instead.
    if PROFILES[profile]["combo"] == "qml":
        cli.check(cli.command("click", objectName="fruitCombo"))
        time.sleep(0.4)
        root = cli.check(cli.command("tree"))["root"]
        comboItems = []
        def walk(n):
            if "comboItem" in (n.get("name") or ""):
                comboItems.append(n)
            for ch in n.get("children", []):
                walk(ch)
        walk(root)
        check("dropdown delegates in tree while open", len(comboItems) == 4,
              str([(c.get("id"), c.get("name")) for c in comboItems]))
        cherry = next((c for c in comboItems if c.get("name") == "comboItem2"),
                      None)
        if cherry:
            cli.check(cli.command("click", node=cherry["id"]))
            time.sleep(0.3)
            text = cli.get_property("currentText", {"objectName": "fruitCombo"})
            check("combo selection via delegate click", text == "Cherry",
                  str(text))
    else:
        cli.check(cli.command("setProperty", objectName="fruitCombo",
                              name="currentIndex", value=2))
        time.sleep(0.2)
        text = cli.get_property("currentText", {"objectName": "fruitCombo"})
        check("combo selection via currentIndex", text == "Cherry", str(text))

    # Granular input: a press alone must NOT activate a button, press+release
    # must (mouseDown / mouseUp).
    import re as _re
    def counter():
        t = str(cli.get_property("text", {"objectName": "clickButton"}))
        m = _re.search(r"(\d+)", t)
        return int(m.group(1)) if m else -1
    before = counter()
    cli.check(cli.command("mouseDown", objectName="clickButton"))
    time.sleep(0.15)
    check("mouseDown alone does not activate", counter() == before,
          f"{before} -> {counter()}")
    cli.check(cli.command("mouseUp"))
    time.sleep(0.15)
    check("mouseDown+mouseUp clicks", counter() == before + 1,
          f"{before} -> {counter()}")

    # ---- hostile input: the probe must survive all of this ----
    r = raw_request(port, "this is not json\n")
    check("garbage line -> malformed JSON error",
          r and r.get("ok") is False and "malformed" in r.get("error", ""),
          str(r)[:200])

    r = raw_request(port, "[1,2,3]\n")
    check("non-object JSON rejected", r and r.get("ok") is False, str(r)[:200])

    r = raw_request(port, '{"id": 3, "cmd": "doesNotExist", "token": "%s"}\n'
                    % token)
    check("unknown command rejected", r and r.get("ok") is False
          and "unknown command" in r.get("error", ""), str(r)[:200])

    # oversized message (limit is 8 MiB): peer must be disconnected.
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            s.sendall(b'A' * (9 * 1024 * 1024))  # no newline
            s.settimeout(5)
            closed = s.recv(1024) == b""
        check("oversized message -> connection closed", closed)
    except (ConnectionResetError, socket.timeout) as e:
        check("oversized message -> connection closed", True, str(e))

    # after all that, the app and probe must be fully alive
    cli2 = ProbeClient(port=port, token=token, timeout=10, lazy=False)
    res = cli2.hello()
    check("probe alive after hostile input", res.get("ok") is True,
          str(res)[:200])
    text = cli2.get_property("text", {"objectName": "clickButton"})
    check("app alive after hostile input", "Clicked" in str(text), str(text))


def test_readonly(target, port, profile=None):
    token = "itest-readonly-token"
    check("banner: read-only mode", "[read-only]" in target.log, target.log)

    cli = ProbeClient(port=port, token=token, timeout=10, lazy=False)
    res = cli.hello()
    check("hello ok", res.get("ok") is True, str(res)[:200])
    res = cli.command("tree")
    check("tree allowed", res.get("ok") is True)
    res = cli.command("properties", objectName="nameField")
    check("properties allowed", res.get("ok") is True)
    res = cli.command("screenshot")
    check("screenshot allowed", res.get("ok") is True, str(res)[:120])

    for cmd, args in [
        ("setProperty", {"objectName": "nameField", "name": "text",
                         "value": "nope"}),
        ("click", {"objectName": "clickButton"}),
        ("typeText", {"objectName": "nameField", "text": "nope"}),
        ("invoke", {"objectName": "nameField", "method": "forceActiveFocus"}),
        ("highlight", {"objectName": "animatedBall"}),
        ("setInputVisualizer", {"enabled": True}),
        ("clearHighlight", {}),
    ]:
        res = cli.command(cmd, **args)
        check(f"read-only rejects {cmd}",
              res.get("ok") is False and "read-only" in res.get("error", ""),
              str(res)[:200])

    # mutation must not have happened
    cli2 = ProbeClient(port=port, token=token, timeout=10, lazy=False)
    value = cli2.get_property("text", {"objectName": "nameField"})
    check("no mutation occurred", "nope" not in str(value), str(value))


def test_noauth(target, port, profile=None):
    check("banner: no-auth warning",
          "no auth" in target.log and "development only" in target.log,
          target.log)
    cli = ProbeClient(port=port, token=None, timeout=10, lazy=False)
    res = cli.hello()
    check("hello without token ok", res.get("ok") is True, str(res)[:200])


def test_vizenv(target, port, profile=None):
    # QLI_INPUT_VIZ=1 was passed in the target's environment: the overlay
    # must be up before any client speaks to the probe.
    cli = ProbeClient(port=port, token=target.token, timeout=10, lazy=False)
    res = cli.check(cli.command("setInputVisualizer"))
    check("visualizer enabled at startup via QLI_INPUT_VIZ",
          res.get("enabled") is True, str(res))

    # The shown overlay must not swallow normal interaction.
    cli.check(cli.command("click", objectName="clickButton"))
    time.sleep(0.6)
    text = cli.get_property("text", {"objectName": "clickButton"})
    check("app responsive with overlay shown",
          isinstance(text, str) and "Clicked 1 times" in text, str(text))

    shot = cli.check(cli.command("screenshot"))
    png = base64.b64decode(shot.get("png", ""))
    check("screenshot captured with overlay", len(png) > 1000
          and png[:4] == b"\x89PNG", f"{len(png)} bytes")

    # And the protocol can still turn it off again.
    res = cli.check(cli.command("setInputVisualizer", enabled=False))
    check("disable via protocol", res.get("enabled") is False, str(res))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", required=True)
    ap.add_argument("--probe-so", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--mode", choices=["basic", "readonly", "noauth", "vizenv"],
                    required=True)
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="extra environment for the target; repeatable")
    ap.add_argument("--profile", choices=list(PROFILES.keys()), default="demo")
    args = ap.parse_args()

    if not sys.platform.startswith("linux"):
        print("SKIP: probe injection test requires Linux (LD_PRELOAD)")
        return 0
    if not os.path.exists(args.probe_so):
        print(f"SKIP: probe library not found: {args.probe_so}")
        return 0

    extra_env = {}
    for pair in args.set:
        key, sep, value = pair.partition("=")
        if not sep:
            print(f"error: --set expects KEY=VALUE, got: {pair}")
            return 2
        extra_env[key] = value

    target = Target(args.demo, args.probe_so, args.port, args.mode, extra_env)
    try:
        if not target.wait_ready():
            check("probe started", False, target.log[-2000:])
            return 1
        check("probe started", True)
        {"basic": test_basic, "readonly": test_readonly,
         "noauth": test_noauth, "vizenv": test_vizenv}[args.mode](
            target, args.port, args.profile)
    finally:
        target.stop()

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        return 1
    print(f"INTEGRATION ({args.mode}) PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
