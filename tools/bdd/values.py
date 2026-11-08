"""values.py — conversion and comparison of probe JSON values."""
import json


def to_python(v):
    """Convert a probe JSON property value to a plain Python value."""
    if isinstance(v, dict):
        if "object" in v:
            s = v.get("className") or "QObject"
            if v.get("objectName"):
                s += f" '{v['objectName']}'"
            return s + f" [#{v['object']}]"
        if v.get("enum"):
            return v.get("text", v.get("value"))
        if "width" in v and "x" in v:
            return f"{v['x']},{v['y']} {v['width']}x{v['height']}"
        if "width" in v:
            return f"{v['width']}x{v['height']}"
        if "x" in v:
            return f"{v['x']},{v['y']}"
        return json.dumps(v, separators=(",", ":"))
    return v


def parse_literal(text):
    """Parse a Gherkin step argument: JSON scalar if possible, else string."""
    try:
        return json.loads(text)
    except (json.JSONDecodeError, TypeError):
        return text


def values_equal(actual, expected):
    """Tolerant comparison: numbers numerically, bools as true/false,
    everything else as strings."""
    actual = to_python(actual)
    if isinstance(actual, bool) or isinstance(expected, bool):
        return _as_bool(actual) == _as_bool(expected)
    fa, fb = _as_float(actual), _as_float(expected)
    if fa is not None and fb is not None:
        return abs(fa - fb) < 1e-9
    return str(actual) == str(expected)


def _as_bool(v):
    if isinstance(v, bool):
        return v
    if isinstance(v, str) and v.lower() in ("true", "false"):
        return v.lower() == "true"
    return bool(v)


def _as_float(v):
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v)
        except ValueError:
            return None
    return None
