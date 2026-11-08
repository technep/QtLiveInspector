"""gherkin.py — a focused Gherkin parser for QtLiveInspector BDD tests.

Supported subset (covers the Squish/cucumber everyday vocabulary):
  Feature, Background, Scenario, Scenario Outline/Template (+ Examples),
  Given/When/Then/And/But steps, @tags, # comments,
  doc strings (\"\"\"), data tables (| ... |).

Scenario outlines are expanded into concrete scenarios at parse time.
"""
from dataclasses import dataclass, field


@dataclass
class Step:
    keyword: str          # Given / When / Then (And/But inherit previous)
    text: str
    line: int = 0
    docstring: str = None
    table: list = None    # list of list of str


@dataclass
class Scenario:
    name: str
    steps: list = field(default_factory=list)
    tags: list = field(default_factory=list)
    line: int = 0


@dataclass
class Feature:
    name: str
    description: str = ""
    tags: list = field(default_factory=list)
    background: list = field(default_factory=list)   # steps
    scenarios: list = field(default_factory=list)
    path: str = ""


class GherkinError(Exception):
    pass


_STEP_KEYWORDS = ("Given", "When", "Then", "And", "But")


def _split_table_row(line):
    # | a | b |  ->  ["a", "b"]   (\| escapes a literal pipe)
    cells, cur, esc = [], "", False
    for ch in line.strip().strip("|"):
        if esc:
            cur += ch
            esc = False
        elif ch == "\\":
            esc = True
        elif ch == "|":
            cells.append(cur.strip())
            cur = ""
        else:
            cur += ch
    cells.append(cur.strip())
    return cells


def _substitute(text, row):
    for key, val in row.items():
        text = text.replace(f"<{key}>", val)
    return text


def parse_file(path):
    with open(path, encoding="utf-8-sig") as f:
        return parse_text(f.read(), path)


def parse_text(text, path="<string>"):
    feature = None
    scenario = None          # current Scenario (steps appended here)
    background = None        # list while parsing Background
    in_examples = False      # parsing the Examples table of an outline
    pending_tags = []
    last_step = None
    in_docstring = False
    docstring_lines = []
    examples_header = None
    line_no = 0

    def current_steps():
        if scenario is None and background is None:
            raise GherkinError(f"{path}:{line_no}: step outside a scenario")
        return background if background is not None else scenario.steps

    for raw in text.splitlines():
        line_no += 1
        line = raw.strip()

        if in_docstring:
            if line.startswith('"""'):
                last_step.docstring = "\n".join(docstring_lines)
                docstring_lines = []
                in_docstring = False
            else:
                docstring_lines.append(raw)
            continue

        if not line or line.startswith("#"):
            continue

        if line.startswith("@"):
            pending_tags.extend(t for t in line.split() if t.startswith("@"))
            continue

        keyword, _, rest = line.partition(":")
        keyword = keyword.strip()
        rest = rest.strip()

        if keyword == "Feature":
            if feature is not None:
                raise GherkinError(f"{path}:{line_no}: multiple Feature lines")
            feature = Feature(name=rest, tags=pending_tags, path=path)
            pending_tags = []
            scenario = None
            background = None
            continue

        if feature is None:
            raise GherkinError(f"{path}:{line_no}: content before Feature:")

        if keyword == "Background":
            background = []
            scenario = None
            in_examples = False
            last_step = None
            continue

        if keyword in ("Scenario", "Example", "Scenario Outline", "Scenario Template"):
            scenario = Scenario(name=rest, tags=pending_tags, line=line_no)
            scenario.is_outline = "Outline" in keyword or "Template" in keyword
            scenario.examples = []
            pending_tags = []
            feature.scenarios.append(scenario)
            background = None
            in_examples = False
            last_step = None
            continue

        if keyword == "Examples":
            if scenario is None or not getattr(scenario, "is_outline", False):
                raise GherkinError(
                    f"{path}:{line_no}: Examples outside a Scenario Outline")
            in_examples = True
            examples_header = None
            continue

        matched_step = None
        body = None
        for kw in _STEP_KEYWORDS:
            if line == kw or line.startswith(kw + " "):
                matched_step = kw
                body = line[len(kw):].strip()
                break
        if matched_step:
            in_examples = False
            if matched_step in ("And", "But"):
                matched_step = last_step.keyword if last_step else "Given"
            step = Step(keyword=matched_step, text=body, line=line_no)
            current_steps().append(step)
            last_step = step
            continue

        if line.startswith('"""'):
            if last_step is None:
                raise GherkinError(f"{path}:{line_no}: doc string without a step")
            in_docstring = True
            docstring_lines = []
            continue

        if line.startswith("|"):
            cells = _split_table_row(line)
            if in_examples:
                if examples_header is None:
                    examples_header = cells
                else:
                    if len(cells) != len(examples_header):
                        raise GherkinError(
                            f"{path}:{line_no}: Examples row has {len(cells)} "
                            f"cells, header has {len(examples_header)}")
                    scenario.examples.append(dict(zip(examples_header, cells)))
                continue
            if last_step is None:
                raise GherkinError(f"{path}:{line_no}: data table without a step")
            if last_step.table is None:
                last_step.table = []
            last_step.table.append(cells)
            continue

        # plain text: Feature description (before any scenario)
        if scenario is None and not feature.background:
            feature.description += line + "\n"
            continue

        raise GherkinError(f"{path}:{line_no}: cannot parse line: {line!r}")

    if feature is None:
        raise GherkinError(f"{path}: no Feature found")

    # Expand scenario outlines into concrete scenarios.
    expanded = []
    for sc in feature.scenarios:
        if not getattr(sc, "is_outline", False):
            expanded.append(sc)
            continue
        if not sc.examples:
            raise GherkinError(
                f"{path}: Scenario Outline '{sc.name}' has no Examples")
        for i, row in enumerate(sc.examples, 1):
            steps = [Step(keyword=s.keyword, text=_substitute(s.text, row),
                          line=s.line,
                          docstring=_substitute(s.docstring, row) if s.docstring else None,
                          table=[[_substitute(c, row) for c in r]
                                 for r in s.table] if s.table else None)
                     for s in sc.steps]
            expanded.append(Scenario(
                name=f"{_substitute(sc.name, row)} [example {i}]",
                steps=steps, tags=sc.tags, line=sc.line))
    feature.scenarios = expanded
    return feature
