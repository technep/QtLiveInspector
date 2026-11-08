"""steps_builtin.py — built-in Squish-like step vocabulary.

Every step receives a Context (see runner.py) as its first argument.
User step files can add their own definitions on top of these.
"""
import time

from .stepregistry import step
from .values import parse_literal, to_python, values_equal


# --- connection -------------------------------------------------------------

@step("the application is running")
def app_is_running(ctx):
    res = ctx.probe.command("hello")
    if not res.get("ok"):
        raise AssertionError(f"probe answered with error: {res.get('error')}")


# --- existence / visibility ---------------------------------------------------

@step("an object named {string} should exist")
def object_should_exist(ctx, name):
    if not ctx.probe.find_by_name(name):
        raise AssertionError(f"no object named '{name}' found")


@step("an object named {string} should not exist")
def object_should_not_exist(ctx, name):
    if ctx.probe.find_by_name(name):
        raise AssertionError(f"object named '{name}' unexpectedly exists")


@step("the object {string} should be visible")
def object_visible(ctx, name):
    actual = ctx.prop(name, "visible")
    if not values_equal(actual, True):
        raise AssertionError(f"'{name}'.visible is {to_python(actual)!r}, expected true")


@step("the object {string} should not be visible")
def object_not_visible(ctx, name):
    actual = ctx.prop(name, "visible")
    if not values_equal(actual, False):
        raise AssertionError(f"'{name}'.visible is {to_python(actual)!r}, expected false")


# --- interaction --------------------------------------------------------------

@step("I click {string}")
def click_object(ctx, name):
    ctx.probe.check(ctx.probe.command("click", objectName=name))


@step("I click node {int}")
def click_node(ctx, node):
    ctx.probe.check(ctx.probe.command("click", node=node))


@step("I type {string} into {string}")
def type_into(ctx, text, name):
    ctx.probe.check(ctx.probe.command("typeText", objectName=name, text=text))


@step("I invoke {string} on {string}")
def invoke_on(ctx, method, name):
    ctx.probe.check(ctx.probe.command("invoke", objectName=name, method=method,
                                      args=[]))


@step("I set the {string} property of {string} to {any}")
def set_property(ctx, prop, name, value):
    ctx.probe.check(ctx.probe.command("setProperty", objectName=name, name=prop,
                                      value=parse_literal(value)))


@step("I wait {float} seconds")
def wait_seconds(ctx, seconds):
    time.sleep(seconds)


@step("I highlight {string} for {float} seconds")
def highlight_object(ctx, name, seconds):
    ctx.probe.check(ctx.probe.command("highlight", objectName=name))
    time.sleep(seconds)
    ctx.probe.check(ctx.probe.command("clearHighlight"))


@step("I take a screenshot and save it as {string}")
def take_screenshot(ctx, filename):
    path = ctx.save_screenshot(filename)
    ctx.note(f"screenshot saved: {path}")


# --- assertions ---------------------------------------------------------------

@step("the {string} property of {string} should exist")
def property_should_exist(ctx, prop, name):
    ctx.prop(name, prop)  # raises RuntimeError when absent


@step("the {string} property of {string} should be {any}")
def property_should_be(ctx, prop, name, expected):
    actual = ctx.prop(name, prop)
    if not values_equal(actual, parse_literal(expected)):
        raise AssertionError(
            f"'{name}'.{prop} is {to_python(actual)!r}, expected {expected!r}")


@step("the {string} property of {string} should contain {string}")
def property_should_contain(ctx, prop, name, needle):
    actual = ctx.prop(name, prop)
    if needle not in str(to_python(actual)):
        raise AssertionError(
            f"'{name}'.{prop} is {to_python(actual)!r}, expected it to contain {needle!r}")


@step("the {string} property of {string} should become {any} within {float} seconds")
def property_should_become(ctx, prop, name, expected, timeout):
    expected = parse_literal(expected)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            actual = ctx.prop(name, prop)
        except RuntimeError:
            actual = None
        if values_equal(actual, expected):
            return
        time.sleep(0.1)
    raise AssertionError(
        f"'{name}'.{prop} did not become {expected!r} within {timeout:g}s "
        f"(last value: {to_python(actual)!r})")


@step("the Squish locator of {string} should be {string}")
def locator_should_be(ctx, name, expected):
    res = ctx.probe.check(ctx.probe.command("locator", objectName=name))
    actual = res.get("locator", "")
    if actual != expected:
        raise AssertionError(f"locator is {actual!r}, expected {expected!r}")
