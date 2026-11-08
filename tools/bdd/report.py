"""report.py — console, JUnit XML and JSON reports for BDD runs."""
import json
from xml.sax.saxutils import escape


class Totals:
    def __init__(self, features):
        self.scenarios = 0
        self.passed = 0
        self.failed = 0
        self.undefined = 0
        self.skipped_steps = 0
        for f in features:
            for s in f.scenarios:
                self.scenarios += 1
                if s.status == "passed":
                    self.passed += 1
                elif s.status == "undefined":
                    self.undefined += 1
                else:
                    self.failed += 1
                self.skipped_steps += sum(1 for st in s.steps
                                          if st.status == "skipped")


_MARK = {"passed": "✓", "failed": "✗", "skipped": "-", "undefined": "?"}


def console_report(features, out_lines):
    for f in features:
        out_lines.append("")
        out_lines.append(f"Feature: {f.name}    ({f.path})")
        if f.error:
            out_lines.append(f"  PARSE ERROR: {f.error}")
            continue
        for s in f.scenarios:
            out_lines.append(f"  Scenario: {s.name}   [{s.status}] "
                             f"({s.duration:.2f}s)")
            for st in s.steps:
                mark = _MARK.get(st.status, "?")
                out_lines.append(f"    {mark} {st.keyword} {st.text}"
                                 f"   ({st.duration:.2f}s)")
                if st.error:
                    for ln in st.error.splitlines():
                        out_lines.append(f"        {ln}")
                if st.note:
                    out_lines.append(f"        {st.note}")
            if s.screenshot:
                out_lines.append(f"    failure screenshot: {s.screenshot}")
    t = Totals(features)
    out_lines.append("")
    out_lines.append(
        f"{t.scenarios} scenario(s): {t.passed} passed, {t.failed} failed, "
        f"{t.undefined} undefined")
    return t


def junit_xml(features):
    t = Totals(features)
    cases = []
    for f in features:
        for s in f.scenarios:
            body = ""
            if s.status in ("failed", "undefined"):
                failed_step = next((st for st in s.steps
                                    if st.status in ("failed", "undefined")), None)
                msg = escape(failed_step.error or "undefined step") if failed_step else ""
                kind = "failure" if s.status == "failed" else "skipped"
                body = (f'<{kind} message="{msg}">'
                        + escape("\n".join(
                            f"{st.keyword} {st.text} [{st.status}]"
                            for st in s.steps))
                        + f"</{kind}>")
            cases.append(
                f'<testcase classname="{escape(f.name)}" name="{escape(s.name)}" '
                f'time="{s.duration:.3f}">{body}</testcase>')
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<testsuite name="qli-bdd" tests="{t.scenarios}" '
            f'failures="{t.failed}" skipped="{t.undefined}">'
            + "".join(cases) + "</testsuite>")


def json_report(features):
    return {
        "features": [
            {
                "name": f.name,
                "path": f.path,
                "error": f.error,
                "scenarios": [
                    {
                        "name": s.name,
                        "tags": s.tags,
                        "status": s.status,
                        "duration": s.duration,
                        "screenshot": s.screenshot,
                        "steps": [
                            {"keyword": st.keyword, "text": st.text,
                             "status": st.status, "error": st.error,
                             "duration": st.duration}
                            for st in s.steps
                        ],
                    }
                    for s in f.scenarios
                ],
            }
            for f in features
        ]
    }


def json_str(features):
    return json.dumps(json_report(features), indent=2)
