#!/usr/bin/env python3
"""qli_bdd.py — Gherkin BDD test runner for QtLiveInspector.

Executes .feature files against a Qt 6 application running with the probe.

Usage:
    qli_bdd.py [options] <feature-file-or-dir> [...]

Options:
    --host H / --port P / --token T   probe connection (defaults 127.0.0.1:17017)
    --steps FILE                      user step definitions (repeatable)
    --tags @a,@b                      run only scenarios carrying one of these tags
    --exclude-tags @wip               skip scenarios carrying these tags
    --junit FILE                      write a JUnit XML report
    --json FILE                       write a JSON report
    --screenshots DIR                 directory for screenshots
                                      (default bdd-report/screenshots)
    --no-fail-screenshot              don't grab a screenshot on failure

Exit code: 0 = all scenarios passed, 1 = failures/undefined steps/parse errors.

Connection settings also come from QLI_HOST / QLI_PORT / QLI_TOKEN (since
probe v0.2 a token is required unless the target was started with QLI_TOKEN
explicitly empty; the auto-generated token is printed to the target's log).

User step files are plain Python modules using the registry decorators:

    from bdd.stepregistry import step

    @step("the counter shows {int}")
    def counter_shows(ctx, value):
        assert ctx.prop("counterLabel", "text") == str(value)

The first argument is a Context: ctx.probe (ProbeClient), ctx.prop(name, prop),
ctx.world (per-scenario dict), ctx.save_screenshot(filename), ctx.note(text).
Hooks: @before_scenario / @after_scenario from bdd.stepregistry.
"""
import argparse
import importlib.util
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # tools/

from probeclient import DEFAULT_PORT, ProbeClient
from qli_version import __version__
from bdd import gherkin, report
from bdd.runner import FeatureResult, Runner, RunnerConfig
from bdd.stepregistry import default_registry
import bdd.steps_builtin  # noqa: F401  (registers built-in steps)


def load_step_file(path):
    spec = importlib.util.spec_from_file_location(
        "qli_user_steps_" + os.path.basename(path).replace(".", "_"), path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)


def collect_feature_files(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, names in os.walk(p):
                for n in sorted(names):
                    if n.endswith(".feature"):
                        files.append(os.path.join(root, n))
        else:
            files.append(p)
    return files


def tags_match(feature_tags, scenario_tags, include, exclude):
    tags = set(feature_tags) | set(scenario_tags)
    if include and not (tags & include):
        return False
    if exclude and (tags & exclude):
        return False
    return True


def main():
    ap = argparse.ArgumentParser(description="QtLiveInspector BDD test runner")
    ap.add_argument("--version", action="version",
                    version=f"%(prog)s {__version__}")
    ap.add_argument("paths", nargs="+", help=".feature files or directories")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--token", default=None)
    ap.add_argument("--steps", action="append", default=[],
                    help="python file with user step definitions (repeatable)")
    ap.add_argument("--tags", default="", help="comma-separated tags to include")
    ap.add_argument("--exclude-tags", default="",
                    help="comma-separated tags to exclude")
    ap.add_argument("--junit", default=None)
    ap.add_argument("--json", dest="json_path", default=None)
    ap.add_argument("--screenshots", default="bdd-report/screenshots")
    ap.add_argument("--no-fail-screenshot", action="store_true")
    args = ap.parse_args()

    for step_file in args.steps:
        load_step_file(step_file)

    include = {t.strip() for t in args.tags.split(",") if t.strip()}
    exclude = {t.strip() for t in args.exclude_tags.split(",") if t.strip()}

    probe = ProbeClient(args.host, args.port, args.token)
    runner = Runner(probe, default_registry,
                    RunnerConfig(screenshot_dir=args.screenshots,
                                 fail_screenshot=not args.no_fail_screenshot))

    features = []
    for path in collect_feature_files(args.paths):
        try:
            feature = gherkin.parse_file(path)
        except (gherkin.GherkinError, OSError) as e:
            features.append(FeatureResult(name=os.path.basename(path),
                                          path=path, error=str(e)))
            continue
        feature.scenarios = [
            s for s in feature.scenarios
            if tags_match(feature.tags, s.tags, include, exclude)
        ]
        if feature.scenarios:
            features.append(runner.run_feature(feature))
        else:
            features.append(report.FeatureResult(name=feature.name,
                                                 path=feature.path))

    lines = []
    totals = report.console_report(features, lines)
    print("\n".join(lines))

    if runner.undefined_snippets:
        print("\nUndefined steps — suggested implementations:\n")
        for snippet in runner.undefined_snippets:
            print(snippet)

    if args.junit:
        with open(args.junit, "w", encoding="utf-8") as f:
            f.write(report.junit_xml(features))
        print(f"JUnit report: {args.junit}")
    if args.json_path:
        with open(args.json_path, "w", encoding="utf-8") as f:
            f.write(report.json_str(features))
        print(f"JSON report: {args.json_path}")

    parse_errors = sum(1 for f in features if f.error)
    ok = (totals.failed == 0 and totals.undefined == 0 and parse_errors == 0
          and totals.scenarios > 0)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
