#!/usr/bin/env python3
"""qli_monkey.py — random-input stress tester ("monkey") for probe-injected Qt6 apps.

Pulls the live object tree, picks actionable objects (buttons, fields,
sliders, delegates — anything with an objectName or an interactive class),
and fires random clicks, text input and value changes at them.

Because every input goes through the probe's synthesized events, a wedged or
crashed GUI thread shows up immediately: commands time out, and a dead target
drops the probe connection entirely — the monkey reports both.

While a run is active the monkey asks the probe to switch the input
visualizer to monkey mode (monkey-head icon instead of the circle), and
switches it back when the run ends. On the standalone inputviz library (no
probe connection) start the target with QLI_INPUT_VIZ_MONKEY=1 instead.

Examples:
    python3 tools/qli_monkey.py --actions 500 --delay 50
    python3 tools/qli_monkey.py --seed 42 --include '^(click|list)Item' \\
        --report monkey.json

Exit codes: 0 = run completed, 1 = setup/usage error, 2 = target lost
(probe unreachable for --crash-grace seconds — the app crashed or froze).

Connection: QLI_HOST / QLI_PORT / QLI_TOKEN environment variables (since
probe v0.2 a token is required unless the target was started with QLI_TOKEN
explicitly empty — the auto-generated token is printed to the target's log).
"""
import argparse
import json
import random
import re
import string
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from probeclient import DEFAULT_PORT, ProbeClient  # noqa: E402
from qli_version import __version__  # noqa: E402

EXIT_OK, EXIT_ERROR, EXIT_TARGET_LOST = 0, 1, 2

# class-name substring -> action weights for that object
TYPE_CLASSES = ("TextField", "TextInput", "TextEdit", "LineEdit", "SpinBox")
VALUE_CLASSES = ("Slider", "Dial", "SpinBox")
CLICK_CLASSES = ("Button", "Delegate", "Tab", "Check", "Radio", "Switch",
                 "Item", "Rectangle", "Label", "Image", "View", "Control")

WORDS = ("alpha", "bravo", "qt", "quick", "monkey", "stress", "item",
         "delegate", "probe", "42", "!@#", "Grüße", "テスト")


def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Random-input stress tester for a probe-injected Qt6 app")
    p.add_argument("--version", action="version",
                   version=f"%(prog)s {__version__}")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--token", default=None)
    p.add_argument("--actions", type=int, default=100,
                   help="number of random actions (default 100)")
    p.add_argument("--delay", type=float, default=100.0,
                   help="delay between actions in ms (default 100)")
    p.add_argument("--seed", type=int, default=None,
                   help="random seed for reproducible runs")
    p.add_argument("--include", default=None,
                   help="regex: only touch objects whose objectName or class "
                        "matches")
    p.add_argument("--exclude", default=None,
                   help="regex: never touch objects whose objectName or class "
                        "matches (e.g. '^quitButton$')")
    p.add_argument("--tree-refresh", type=int, default=25,
                   help="re-fetch the object tree every N actions "
                        "(default 25) — handles recycled delegates")
    p.add_argument("--crash-grace", type=float, default=5.0,
                   help="seconds to retry a dead probe before declaring the "
                        "target lost (default 5)")
    p.add_argument("--max-text", type=int, default=12,
                   help="max random text length (default 12)")
    p.add_argument("--report", default=None,
                   help="write a JSON summary to this path")
    p.add_argument("--verbose", action="store_true",
                   help="log every action")
    return p.parse_args(argv)


def collect_targets(node, out, include, exclude):
    """Gather interactive objects from a tree node (recursive)."""
    name, cls = node.get("name") or "", node.get("className") or ""
    # Real probe trees always carry isQuickItem/isWidget; tolerate minimal
    # trees (e.g. stubs) that omit the flags entirely.
    flagged = "isQuickItem" in node or "isWidget" in node
    interactive = not flagged or node.get("isQuickItem") or node.get("isWidget")
    if interactive and (name or node.get("id")):
        label = name or cls
        if include and not include.search(label):
            interactive = False
        if exclude and exclude.search(label):
            interactive = False
        if interactive:
            out.append({"name": name, "cls": cls, "id": node.get("id")})
    for child in node.get("children", []):
        collect_targets(child, out, include, exclude)


def action_pool(target, rng):
    """Weighted action choices for one target."""
    cls = target["cls"]
    pool = []
    if any(c in cls for c in CLICK_CLASSES) or not cls:
        pool += ["click"] * 5
    if any(c in cls for c in TYPE_CLASSES):
        pool += ["type"] * 3
    if any(c in cls for c in VALUE_CLASSES):
        pool += ["setValue"] * 2
    if not pool:                     # unknown class — a click is always fair
        pool = ["click"]
    return pool


def random_text(rng, max_len):
    if rng.random() < 0.3:
        return rng.choice(WORDS)
    n = rng.randint(1, max_len)
    return "".join(rng.choice(string.ascii_letters + string.digits + " ")
                   for _ in range(n)).strip() or "x"


def target_ref(t):
    """Prefer objectName addressing (stable across delegate recycling)."""
    return {"objectName": t["name"]} if t["name"] else {"node": t["id"]}


def describe(t):
    return f'{t["name"] or "#" + str(t["id"])} ({t["cls"]})'


class Monkey:
    def __init__(self, args):
        self.args = args
        self.rng = random.Random(args.seed)
        self.probe = ProbeClient(args.host, args.port, args.token,
                                 timeout=15.0, lazy=True)
        self.include = re.compile(args.include) if args.include else None
        self.exclude = re.compile(args.exclude) if args.exclude else None
        self.targets = []
        self.stats = {"actions": 0, "clicks": 0, "types": 0, "setValues": 0,
                      "benign_errors": 0, "errors": [],
                      "objects_touched": []}
        self.touched = set()
        self.started = None

    # -- probe interaction with crash detection -----------------------------

    def call(self, cmd, **kw):
        """One probe command; on connection loss, retry within the grace
        window, then declare the target lost."""
        deadline = time.monotonic() + self.args.crash_grace
        while True:
            try:
                return self.probe.command(cmd, **kw)
            except ConnectionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)

    def query_viz_state(self):
        """Current (enabled, monkey) of the input visualizer; None when the
        probe can't answer (older probe, target lost mid-run)."""
        try:
            r = self.probe.command("setInputVisualizer")
            return (bool(r.get("enabled")), bool(r.get("monkey")))
        except (RuntimeError, ConnectionError):
            return None

    def set_monkey_mode(self, on):
        """Monkey-head icon while a run is active; best effort (older probes
        or a readonly session just keep the circles)."""
        try:
            self.probe.command("setInputVisualizer", enabled=on, monkey=on)
        except (RuntimeError, ConnectionError):
            pass

    def refresh_tree(self):
        root = self.probe.check(self.call("tree"))["root"]
        self.targets.clear()
        collect_targets(root, self.targets, self.include, self.exclude)

    # -- one random action ----------------------------------------------------

    def act(self):
        t = self.rng.choice(self.targets)
        kind = self.rng.choice(action_pool(t, self.rng))
        ref = target_ref(t)
        if kind == "click":
            res = self.call("click", **ref)
        elif kind == "type":
            text = random_text(self.rng, self.args.max_text)
            res = self.call("typeText", text=text, **ref)
        else:  # setValue
            res = self.call("setProperty", name="value",
                            value=round(self.rng.uniform(0, 100), 1), **ref)
        self.touched.add(t["name"] or f"#{t['id']}")
        self.stats[kind + "s" if kind != "type" else "types"] += 1
        return t, kind, res

    # -- main loop ------------------------------------------------------------

    def run(self):
        info = self.probe.check(self.call("hello"))
        viz_state = self.query_viz_state()
        self.set_monkey_mode(True)
        try:
            return self._run(info)
        finally:
            if viz_state is not None:
                # restore exactly what the run found (enabled, monkey)
                try:
                    self.probe.command(
                        "setInputVisualizer",
                        enabled=viz_state[0], monkey=viz_state[1])
                except (RuntimeError, ConnectionError):
                    pass
            else:
                self.set_monkey_mode(False)

    def _run(self, info):
        self.refresh_tree()
        if not self.targets:
            print("no actionable objects found in the tree "
                  "(check --include/--exclude filters)", file=sys.stderr)
            return EXIT_ERROR

        print(f"monkey vs {info.get('app', '?')} (Qt {info.get('qt', '?')}, "
              f"pid {info.get('pid', '?')}): {self.args.actions} actions over "
              f"{len(self.targets)} targets"
              + (f", seed {self.args.seed}" if self.args.seed is not None
                 else ""))

        self.started = time.monotonic()
        for i in range(1, self.args.actions + 1):
            if not self.targets or i % self.args.tree_refresh == 0:
                self.refresh_tree()
            try:
                t, kind, res = self.act()
            except ConnectionError as e:
                self.report_lost(i, e)
                return EXIT_TARGET_LOST
            except RuntimeError:
                # object vanished mid-run (recycled delegate, closed page) —
                # that's normal UI churn, not a failure
                self.stats["benign_errors"] += 1
                self.refresh_tree()
                continue
            if not res.get("ok"):
                self.stats["benign_errors"] += 1
                self.refresh_tree()
                continue
            if self.args.verbose:
                print(f"[{i:>4}] {kind:>8} -> {describe(t)}")
            elif i % max(1, self.args.actions // 10) == 0:
                print(f"[{i:>4}/{self.args.actions}] "
                      f"{self.stats['benign_errors']} churn")
            if self.args.delay:
                time.sleep(self.args.delay / 1000.0)

        self.finish()
        return EXIT_OK

    # -- reporting ------------------------------------------------------------

    def summary(self):
        dur = max(time.monotonic() - self.started, 1e-6)
        s = dict(self.stats)
        s["objects_touched"] = sorted(self.touched)
        s.update(actions=self.stats["clicks"] + self.stats["types"]
                 + self.stats["setValues"],
                 duration_s=round(dur, 2),
                 actions_per_s=round(
                     (self.stats["clicks"] + self.stats["types"]
                      + self.stats["setValues"]) / dur, 1),
                 result="completed")
        return s

    def finish(self):
        s = self.summary()
        print(f"\ndone: {s['actions']} actions in {s['duration_s']}s "
              f"({s['actions_per_s']}/s), "
              f"{len(s['objects_touched'])} objects touched, "
              f"{s['benign_errors']} UI-churn errors")
        self.maybe_write_report(s)

    def report_lost(self, at_action, err):
        print(f"\nTARGET LOST at action {at_action}: {err}\n"
              f"The probe stayed unreachable for {self.args.crash_grace}s — "
              f"the application crashed or its GUI thread is wedged.",
              file=sys.stderr)
        s = self.summary()
        s["result"] = f"target lost at action {at_action}"
        self.maybe_write_report(s)

    def maybe_write_report(self, s):
        if self.args.report:
            with open(self.args.report, "w", encoding="utf-8") as f:
                json.dump(s, f, indent=2)
            print(f"report: {self.args.report}")


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    try:
        return Monkey(args).run()
    except ConnectionError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("\ninterrupted")
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
