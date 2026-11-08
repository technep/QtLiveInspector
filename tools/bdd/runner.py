"""runner.py — executes parsed features against the probe."""
import base64
import os
import re
import time
from dataclasses import dataclass, field



@dataclass
class StepResult:
    keyword: str
    text: str
    status: str            # passed | failed | skipped | undefined
    error: str = None
    duration: float = 0.0
    note: str = None


@dataclass
class ScenarioResult:
    name: str
    tags: list
    steps: list = field(default_factory=list)
    status: str = "passed"
    duration: float = 0.0
    screenshot: str = None


@dataclass
class FeatureResult:
    name: str
    path: str
    scenarios: list = field(default_factory=list)
    error: str = None       # parse-level failure


class Context:
    """Handed to every step function. Carries the probe connection,
    per-scenario scratch state, and convenience helpers."""

    def __init__(self, probe, config):
        self.probe = probe
        self.config = config
        self.world = {}          # user scratch space, reset per scenario
        self.feature_name = None
        self.scenario_name = None
        self.notes = []

    def target(self, object_name):
        return {"objectName": object_name}

    def prop(self, object_name, prop):
        return self.probe.get_property(prop, {"objectName": object_name})

    def note(self, text):
        self.notes.append(text)

    def save_screenshot(self, filename):
        res = self.probe.check(self.probe.command("screenshot"))
        safe = re.sub(r"[^\w.-]+", "_", filename)
        if not safe.lower().endswith(".png"):
            safe += ".png"
        path = os.path.join(self.config.screenshot_dir, safe)
        os.makedirs(self.config.screenshot_dir, exist_ok=True)
        with open(path, "wb") as f:
            f.write(base64.b64decode(res["png"]))
        return path


class RunnerConfig:
    def __init__(self, screenshot_dir="bdd-report/screenshots",
                 fail_screenshot=True):
        self.screenshot_dir = screenshot_dir
        self.fail_screenshot = fail_screenshot


class Runner:
    def __init__(self, probe, registry, config=None):
        self.probe = probe
        self.registry = registry
        self.config = config or RunnerConfig()
        self.ctx = Context(probe, self.config)
        self.undefined_snippets = []

    # -- execution -----------------------------------------------------------

    def run_feature(self, feature):
        result = FeatureResult(name=feature.name, path=feature.path)
        for scenario in feature.scenarios:
            result.scenarios.append(self.run_scenario(feature, scenario))
        return result

    def run_scenario(self, feature, scenario):
        sr = ScenarioResult(name=scenario.name,
                            tags=list(feature.tags) + list(scenario.tags))
        self.ctx.world = {}
        self.ctx.feature_name = feature.name
        self.ctx.scenario_name = scenario.name
        self.ctx.notes = []

        start = time.monotonic()
        try:
            for hook in self.registry.before_scenario_hooks:
                hook(self.ctx)

            failed = False
            steps = list(feature.background) + list(scenario.steps)
            for step in steps:
                res = self._run_step(step, skip=failed)
                sr.steps.append(res)
                if res.status in ("failed", "undefined"):
                    failed = True
                    if res.status == "undefined":
                        self.undefined_snippets.append(
                            self._snippet_for(step.text))
            if failed:
                sr.status = "failed" if any(s.status == "failed"
                                            for s in sr.steps) else "undefined"
                if self.config.fail_screenshot:
                    sr.screenshot = self._failure_screenshot(sr.name)
        finally:
            try:
                for hook in self.registry.after_scenario_hooks:
                    hook(self.ctx)
            except Exception as e:
                sr.status = "failed"
                sr.steps.append(StepResult("After", "after_scenario hook",
                                           "failed", error=str(e)))
        sr.duration = time.monotonic() - start
        return sr

    def _run_step(self, step, skip):
        if skip:
            return StepResult(step.keyword, step.text, "skipped")
        found = self.registry.match(step.text)
        if not found:
            return StepResult(step.keyword, step.text, "undefined")
        func, args, _pattern = found
        start = time.monotonic()
        try:
            if step.docstring is not None:
                func(self.ctx, *args, step.docstring)
            elif step.table is not None:
                func(self.ctx, *args, step.table)
            else:
                func(self.ctx, *args)
        except (AssertionError, RuntimeError, ConnectionError) as e:
            return StepResult(step.keyword, step.text, "failed",
                              error=str(e),
                              duration=time.monotonic() - start)
        except Exception as e:
            return StepResult(step.keyword, step.text, "failed",
                              error=f"{type(e).__name__}: {e}",
                              duration=time.monotonic() - start)
        res = StepResult(step.keyword, step.text, "passed",
                         duration=time.monotonic() - start)
        if self.ctx.notes:
            res.note = "; ".join(self.ctx.notes)
            self.ctx.notes = []
        return res

    # -- helpers --------------------------------------------------------------

    def _failure_screenshot(self, name):
        try:
            res = self.probe.command("screenshot")
            if not res.get("ok"):
                return None
            safe = re.sub(r"[^\w.-]+", "_", name)[:80]
            os.makedirs(self.config.screenshot_dir, exist_ok=True)
            path = os.path.join(self.config.screenshot_dir,
                                f"FAILED_{safe}.png")
            with open(path, "wb") as f:
                f.write(base64.b64decode(res["png"]))
            return path
        except Exception:
            return None

    @staticmethod
    def _snippet_for(text):
        return (f'@step("{text}")\n'
                f'def step_impl(ctx):\n'
                f'    raise NotImplementedError("{text}")\n')
