"""stepregistry.py — step definition registry with cucumber-style patterns.

Patterns support placeholders:
    {string}  quoted string   "..."
    {word}    single word
    {int}     integer
    {float}   number
    {any}     anything
Patterns starting with '^' are treated as raw regular expressions.

Step functions receive a Context as first argument, then the captured
values in order (ints/floats are converted automatically).
"""
import re

_PLACEHOLDERS = {
    "int": (r"(-?\d+)", int),
    "float": (r"(-?\d+(?:\.\d+)?)", float),
    "word": (r"(\w+)", str),
    "string": (r'"([^"]*)"', str),
    "any": (r"(.*)", str),
}


class StepDefinition:
    def __init__(self, pattern, func):
        self.pattern = pattern
        self.func = func
        self.regex, self.converters = self._compile(pattern)

    @staticmethod
    def _compile(pattern):
        if pattern.startswith("^"):
            return re.compile(pattern), []
        converters = []
        regex = ""
        i = 0
        while i < len(pattern):
            if pattern[i] == "{":
                j = pattern.index("}", i)
                ph = pattern[i + 1:j]
                if ph not in _PLACEHOLDERS:
                    raise ValueError(f"unknown placeholder {{{ph}}} in {pattern!r}")
                rx, conv = _PLACEHOLDERS[ph]
                regex += rx
                converters.append(conv)
                i = j + 1
            else:
                regex += re.escape(pattern[i])
                i += 1
        return re.compile("^" + regex + "$"), converters


class Registry:
    def __init__(self):
        self.steps = []
        self.before_scenario_hooks = []
        self.after_scenario_hooks = []

    def step(self, pattern):
        def deco(func):
            self.steps.append(StepDefinition(pattern, func))
            return func
        return deco

    # keyword aliases — purely decorative, matching is keyword-agnostic
    given = step
    when = step
    then = step

    def before_scenario(self, func):
        self.before_scenario_hooks.append(func)
        return func

    def after_scenario(self, func):
        self.after_scenario_hooks.append(func)
        return func

    def match(self, text):
        """Return (func, [converted args]) or None."""
        for defn in self.steps:
            m = defn.regex.match(text)
            if not m:
                continue
            try:
                args = [conv(g) for g, conv in
                        zip(m.groups(), defn.converters or [str] * len(m.groups()))]
            except (TypeError, ValueError):
                continue
            return defn.func, args, defn.pattern
        return None


# The default registry used by built-in and user step definitions.
default_registry = Registry()

step = default_registry.step
given = default_registry.step
when = default_registry.step
then = default_registry.step
before_scenario = default_registry.before_scenario
after_scenario = default_registry.after_scenario
