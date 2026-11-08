# QtLiveInspector

[![CI](https://github.com/technep/QtLiveInspector/actions/workflows/ci.yml/badge.svg)](https://github.com/technep/QtLiveInspector/actions/workflows/ci.yml)

A lightweight, GammaRay-style live inspector for **Qt 6** applications
(Qt Quick and Qt Widgets). Attach to a running process — locally or over the
network — browse the live QObject tree, search it, watch property changes in
real time, pick on-screen elements with a red rectangle overlay, and copy
Squish-compatible object locators for test automation.

```
┌────────────────────────────┐         TCP :17017          ┌──────────────────────────┐
│  Target Qt6 application    │   newline-delimited JSON    │  qtliveinspector (GUI)   │
│  + libqtinspectorprobe.so  │ ◄─────────────────────────► │  any machine, any OS     │
│  (injected shared library) │                             │                          │
└────────────────────────────┘                             └──────────────────────────┘
```

## Features

- **Live QObject tree** — QCoreApplication, all top-level windows/widgets, and
  the full QML item hierarchy, with class names, objectNames and addresses.
- **Search** — filter by objectName, class name or address; matching branches
  stay visible (recursive filtering).
- **Live properties** — full property table per object; properties with
  `NOTIFY` signals update instantly (push), everything else on refresh.
  Writable scalar properties are editable in place.
- **Object picker** — click "Pick", then click any element in the target app;
  the click is consumed, the deepest QQuickItem/QWidget under the cursor is
  selected in the tree and highlighted. `Esc` cancels.
- **Red rectangle highlight** — a pulsing red overlay glued to the selected
  QQuickItem or QWidget, tracking moves/resizes/animations.
- **Squish locator** — one click copies a Squish-style multi-property object
  name (`{container=':mainWindow' objectName='clickButton' ...}`) to the
  clipboard.
- **Remote** — the probe speaks plain JSON over TCP; bind it to a specific
  interface or tunnel over SSH. Token auth is always on (auto-generated and
  printed to the target's log unless you configure `QLI_TOKEN`); a read-only
  mode allows observation without control. See **Security** below.

## Repository layout

```
QtLiveInspector/
├── CMakeLists.txt          top-level build
├── protocol.txt            wire protocol reference
├── probe/                  libqtinspectorprobe — injected into the target
├── inputviz/               libqtinspectorinputviz — standalone touch visualizer
├── inspector/              qtliveinspector — the GUI client
├── samples/quickdemo/      sample Qt Quick target app (QQmlApplicationEngine)
├── samples/quickviewdemo/  same QML surface hosted in a plain QQuickView
├── samples/widgetdemo/     pure Qt Widgets target (same objectNames)
├── tests/                  unit, tool-smoke and probe-integration tests (ctest)
├── examples/features/      sample Gherkin feature files (BDD)
└── tools/
    ├── probeclient.py      shared Python client library for the probe
    ├── qli_cli.py          minimal CLI client (no Qt needed)
    ├── qli_mcp.py          MCP server — let an AI agent drive the app
    ├── qli_bdd.py          Gherkin/BDD test runner (Squish-style)
    ├── qli_monkey.py       random-input stress tester ("monkey")
    └── bdd/                BDD framework: parser, step registry, runner, reports
```

## Building

Requirements: CMake ≥ 3.19, a C++17 compiler, Qt ≥ 6.2
(Core, Gui, Widgets, Network, Quick, QuickControls2).

```bash
# Debian/Ubuntu:
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Artifacts:

| Artifact | Path |
|---|---|
| probe | `build/probe/libqtinspectorprobe.so` |
| input visualizer | `build/inputviz/libqtinspectorinputviz.so` |
| inspector GUI | `build/inspector/qtliveinspector` |
| demo target | `build/samples/quickdemo/quickdemo` |

## Attaching the probe to a target application

The probe is a single shared library with three injection methods.
Pick whichever fits your platform and constraints.

### 1. LD_PRELOAD (Linux)

```bash
LD_PRELOAD=/path/to/libqtinspectorprobe.so ./your-qt6-app
```

### 2. Qt style-plugin trick (all platforms, incl. Windows/macOS)

Qt loads "style" plugins into the process very early. The probe doubles as a
style plugin that returns plain Fusion, so the app looks unchanged:

```bash
mkdir -p /tmp/qli/styles
cp /path/to/libqtinspectorprobe.so /tmp/qli/styles/
QT_PLUGIN_PATH=/tmp/qli QT_STYLE_OVERRIDE=qtinspectorprobe ./your-qt6-app
```

### 3. Source-level init (when you can rebuild the target)

```cpp
extern "C" void qtinspector_init();
int main(int argc, char **argv) {
    qtinspector_init();           // before or after QApplication — both fine
    QApplication app(argc, argv);
    ...
}
```
Link the target against `libqtinspectorprobe`.

### Touch visualizer only (no inspector)

For devices that just need touch/mouse feedback without any remote control,
there is a standalone visualizer library — same `InputVisualizer` code as the
probe, but **zero network surface**:

```bash
LD_PRELOAD=/path/to/libqtinspectorinputviz.so ./your-qt6-app
```

It draws the press/ress-and-hold/release circle overlay on touch and mouse
input. `QLI_INPUT_VIZ=0` disables it; `QLI_INPUT_VIZ_MULTITOUCH=1` shows all
simultaneous touches (default: first touch only);
`QLI_INPUT_VIZ_MONKEY=1` swaps the circle for a monkey head — the visual
marker for `qli_monkey.py` stress runs (the monkey tool enables it
automatically when it drives the app through the full probe). Lightweight
enough for embedded targets — no sockets, no threads beyond one lazy init,
no Qt Quick dependency in the app itself.

### Probe configuration (environment of the *target*)

| Variable | Default | Purpose |
|---|---|---|
| `QLI_PORT` | `17017` | TCP port the probe listens on |
| `QLI_HOST` | `127.0.0.1` | bind address — `"any"` or `0.0.0.0` exposes the probe on all interfaces (see **Security**) |
| `QLI_TOKEN` | *(auto)* | shared secret. Unset → a random token is generated and printed once to the target's log; explicitly empty → no auth, development only |
| `QLI_READONLY` | *(off)* | `"1"` = read-only mode: tree/properties/screenshot/perf/locator allowed, every mutating command rejected |
| `QLI_INPUT_VIZ` | *(off)* | `"1"` = show the input overlay from startup, without a client connection (the standalone `libqtinspectorinputviz.so` defaults it **on** and uses `QLI_INPUT_VIZ=0` to disable) |
| `QLI_INPUT_VIZ_MULTITOUCH` | *(off)* | `"1"` = input visualizer tracks all simultaneous touches (default: first touch only) |
| `QLI_INPUT_VIZ_MONKEY` | *(off)* | `"1"` = input visualizer draws a monkey head instead of the circle (fun mode for `qli_monkey.py` runs; also switchable at runtime via `setInputVisualizer {monkey:true}`) |

When injection succeeds the target prints:

```
[QtInspector] probe active, listening on 127.0.0.1:17017 (pid 12345) [token required]
[QtInspector] auth token: 4f3a9c2e71b8d5e0f6a1c4b2e8d3f7a0
```

## Security

The probe can click, type, set properties and invoke arbitrary slots inside
the target application — that is remote code execution by another name, so
the defaults are fail-safe (changed in v0.2):

- **Loopback only.** The probe binds `127.0.0.1`. To expose it on the
  network, start the target with `QLI_HOST=any` (or a specific address) —
  and know what you are doing.
- **Token always on.** If `QLI_TOKEN` is unset, the probe generates a random
  token on startup and prints it once to the target's log — look for
  `auth token:` there. An explicitly empty `QLI_TOKEN` selects development
  no-auth mode and is logged loudly.
- **Read-only mode.** `QLI_READONLY=1` accepts only observation commands
  (`hello`, `tree`, `properties`, `screenshot`, `perfStats`, `isIdle`,
  `locator`) and rejects everything else with a clear error — the right
  choice for production observability.
- **Audit trail.** Every authenticated mutating command is logged to the
  target's log: `[QtInspector] AUDIT <peer> cmd=<cmd> <target>`.
- **Robustness.** Per-client message limit (8 MiB; offenders are
  disconnected), malformed input gets an error response, and every request
  is handled behind an exception guard — a probe bug answers with an error
  instead of crashing the host application.

There is no transport encryption: for remote inspection, prefer an SSH
tunnel (below) over exposing the probe directly.

## Using the inspector

```bash
./build/inspector/qtliveinspector
```

1. Enter host/port (`127.0.0.1:17017` for a local target) and press **Connect**.
2. Browse or search the object tree on the left.
3. Select a node — its properties appear on the right and live-update.
   Double-click a writable scalar value to edit it.
4. **Pick** activates pick mode in the target: click any UI element there.
5. **Highlight** draws the red rectangle around the selected item.
6. **Copy Squish Locator** puts e.g.
   `{container=':mainWindow' objectName='clickButton' type='Button' visible='true'}`
   on the clipboard.
7. **Auto** toggles periodic tree refreshes (structure changes only; property
   values are always live for the selected object).

### Quick demo

```bash
LD_PRELOAD=$PWD/build/probe/libqtinspectorprobe.so ./build/samples/quickdemo/quickdemo &
./build/inspector/qtliveinspector        # connect to 127.0.0.1:17017
```

## Remote inspection

The probe binds to loopback by default. The recommended way to inspect an
app on another machine is an SSH tunnel — it encrypts the traffic and keeps
the probe unreachable from the LAN:

```bash
# on the remote host (token taken from its log, or set explicitly):
QLI_PORT=17017 QLI_TOKEN=s3cret \
  LD_PRELOAD=/path/to/libqtinspectorprobe.so ./your-qt6-app

# on your workstation:
ssh -L 17017:127.0.0.1:17017 user@remote-host
./qtliveinspector    # connect to 127.0.0.1:17017, token "s3cret"
```

If a tunnel is not an option and the network is trusted, start the target
with `QLI_HOST=any` to listen on all interfaces and set a strong
`QLI_TOKEN`. Consider `QLI_READONLY=1` for production targets.

## Headless testing: qli_cli.py

`tools/qli_cli.py` speaks the probe protocol directly — useful for CI and
remote targets where no GUI is available. Connection settings come from
`--host`/`--port`/`--token` or the `QLI_HOST`/`QLI_PORT`/`QLI_TOKEN`
environment variables:

```bash
python3 tools/qli_cli.py --host 192.168.1.10 tree | head
python3 tools/qli_cli.py props --name volumeSlider       # or: --node 42
python3 tools/qli_cli.py find clickButton
python3 tools/qli_cli.py locator --name clickButton
python3 tools/qli_cli.py highlight --name animatedBall --seconds 3
python3 tools/qli_cli.py set --name volumeSlider value 75   # write a property
python3 tools/qli_cli.py click --name clickButton           # click a button/item
python3 tools/qli_cli.py type --name nameField "Hello Qt"   # type text into a field
python3 tools/qli_cli.py invoke --name clickButton clicked  # emit/call a method
```

Object-addressing commands accept either `--node <id>` (runtime id, shown by
`tree`/`find`) or `--name <objectName>`. The id form is exact; the name form
requires the objectName to exist and be unique — on duplicates the probe
replies with the conflicting node ids so you can pick one.

## AI navigation: MCP server

`tools/qli_mcp.py` is a self-contained [Model Context
Protocol](https://modelcontextprotocol.io) server (stdio transport, Python
standard library only) that lets an AI agent navigate the target application:
browse the object tree, read and write properties, click items, invoke
methods, take screenshots, generate Squish locators, and ask the user to
point at an element via the picker.

Claude Desktop configuration example:

```json
{
  "mcpServers": {
    "qt-app": {
      "command": "python3",
      "args": ["/path/to/QtLiveInspector/tools/qli_mcp.py"],
      "env": { "QLI_HOST": "127.0.0.1", "QLI_PORT": "17017",
               "QLI_TOKEN": "<token from the target's log>" }
    }
  }
}
```

Start the target app with the probe injected (any of the three methods
above), then the AI session can use these tools:

| Tool | What it does |
|---|---|
| `get_app_info` | app name, Qt version, PID |
| `get_object_tree` | indented live object tree (depth-limited) |
| `find_objects` | search by objectName / class name |
| `get_properties` / `set_property` | read / write object properties |
| `invoke_method` | call slots/invokables or emit signals |
| `click` | click a button/item (synthesized input) |
| `type_text` | type text into a focused item/widget |
| `highlight` / `clear_highlight` | red rectangle overlay |
| `get_squish_locator` | Squish locator for an object |
| `pick_object` | ask the user to click an element interactively |
| `watch_properties` | stream property changes for N seconds |
| `screenshot` | PNG screenshot of the app window (image content) |

`QLI_HOST`/`QLI_PORT`/`QLI_TOKEN` configure the probe connection, so the AI
can just as well drive an app on a remote machine.

## BDD testing: qli_bdd.py

`tools/qli_bdd.py` runs [Gherkin](https://cucumber.io/docs/gherkin/) feature
files against a live, probe-injected application — a lightweight, Squish-style
acceptance-test workflow using only the Python standard library:

```bash
# start the target with the probe injected, then:
python3 tools/qli_bdd.py examples/features/ --host 127.0.0.1

# CI-friendly: tag filtering + JUnit/JSON reports
python3 tools/qli_bdd.py tests/features/ \
    --tags @smoke --exclude-tags @wip \
    --junit report.xml --json report.json
```

A feature file looks like this (see `examples/features/quickdemo.feature`):

```gherkin
Feature: quickdemo basics
  Background:
    Given the application is running

  Scenario: clicking increments the counter
    When I click "clickButton"
    And I click "clickButton"
    Then the "text" property of "clickButton" should be "Clicked 2 times"

  Scenario Outline: slider updates the label
    When I set the "value" property of "volumeSlider" to <v>
    Then the "text" property of "volumeLabel" should become <v> within 2 seconds

    Examples:
      | v  |
      | 10 |
      | 90 |
```

Supported: `Feature`, `Background`, `Scenario`, `Scenario Outline` +
`Examples`, `Given/When/Then/And/But`, `@tags`, doc strings and data tables.
Exit code is 0 only when every selected scenario passed.

**Built-in step vocabulary** (all object references are objectNames):

| Step | Action |
|---|---|
| `the application is running` | connect / verify probe handshake |
| `an object named "X" should exist / not exist` | tree lookup |
| `the object "X" should be visible / not visible` | visibility check |
| `I click "X"` / `I click node N` | synthesized click |
| `I type "text" into "X"` | focus + key events |
| `I invoke "method" on "X"` | call slot/invokable |
| `I set the "prop" property of "X" to <value>` | write property |
| `I wait N seconds` | sleep |
| `I highlight "X" for N seconds` | visual debugging |
| `I take a screenshot and save it as "name.png"` | screenshot artifact |
| `the "prop" property of "X" should exist` | property presence |
| `the "prop" property of "X" should be <value>` | equality (tolerant: numbers, bools, JSON) |
| `the "prop" property of "X" should contain "sub"` | substring match |
| `the "prop" property of "X" should become <value> within N seconds` | poll until match |
| `the Squish locator of "X" should be "{...}"` | locator comparison |

**Custom steps** stay modular — put them in your own Python file and pass it
with `--steps` (repeatable). Undefined steps are reported with a ready-to-paste
implementation snippet:

```python
# my_steps.py
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) +
                "/../tools")          # wherever QtLiveInspector/tools lives
from bdd import step

@step('I log in as {string}')
def login(ctx, user):
    ctx.probe.command("typeText", objectName="userField", text=user)
    ctx.probe.command("click", objectName="loginButton")
    ctx.note(f"logged in as {user}")   # shows up in the report
```

```bash
python3 tools/qli_bdd.py features/ --steps my_steps.py
```

`ctx` gives you `ctx.probe` (the raw probe client), `ctx.prop(name, prop)`
(read a property), `ctx.save_screenshot("file.png")`, `ctx.note(text)`, and
`ctx.world` (a dict for passing data between steps), plus
`@before_scenario` / `@after_scenario` hooks.

On failure (and undefined steps) a screenshot is captured automatically to
`bdd-report/screenshots/` (disable with `--no-fail-screenshot`).

## Stress testing: qli_monkey.py

`tools/qli_monkey.py` is a "monkey" tester: it pulls the live object tree,
collects interactive objects (buttons, text fields, sliders, delegates — by
class or objectName), and fires random clicks, text input and value changes
at them:

```bash
python3 tools/qli_monkey.py --actions 500 --delay 50 --seed 42
python3 tools/qli_monkey.py --exclude '^quitButton$' --report monkey.json
```

- Every input is a real synthesized input event going through the probe —
  a wedged GUI thread shows up as command timeouts, and a crashed app drops
  the probe connection entirely.
- **Crash detection**: if the probe stays unreachable for `--crash-grace`
  seconds mid-run, the monkey exits with code 2 and reports the last action
  before the death — the repro is the printed `--seed`.
- **Reproducible**: same `--seed` = same action sequence.
- **Delegate-safe**: objects are addressed by objectName (stable across
  delegate recycling), the tree is re-fetched every `--tree-refresh`
  actions, and objects that vanish mid-run (recycled delegates, closed
  pages) are counted as normal UI churn, not failures.
- **Filters**: `--include`/`--exclude` regexes over objectName/class keep
  destructive buttons (e.g. "Quit") out of the run.
- Optional `--report monkey.json` summary: action counts, actions/s, objects
  touched, churn errors.

Exit codes: 0 = run completed, 1 = setup error (e.g. app not reachable at
start), 2 = target lost mid-run.

## Protocol

Plain newline-delimited JSON over TCP — trivially scriptable from any
language. See [protocol.txt](protocol.txt) for the full reference.

## Squish locator notes

Locators are generated as Squish multi-property object names:

- objects **with** an objectName → `objectName='...' type='...'`
- objects **without** one → `type='...' occurrence='N'` (1-based index among
  unnamed same-class siblings)
- the immediate parent becomes `container=':symbolicName'`
- `visible` is included for QQuickItems and QWidgets

For robust Squish suites, prefer giving stable `objectName`s to the QML
items/widgets under test — the locator generator uses them whenever present.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

| Test | What it covers |
|---|---|
| `JsonUtilTest` | C++ unit tests: QVariant ⇆ JSON conversion |
| `ToolSmokeTest` | all Python tools compile; CLI + MCP run a full session against a stub probe; token enforcement |
| `ProbeIntegration_basic` | real probe in quickdemo (offscreen): auto-token from log, localhost bind, protocol handshake, property roundtrip, click, screenshot, hostile-input barrage (malformed JSON, unknown command, oversized message) |
| `ProbeIntegration_readonly` | read-only mode: observation passes, every mutation rejected |
| `ProbeIntegration_noauth` | explicit empty `QLI_TOKEN` development mode |
| `ProbeIntegration_quickview` | same QML surface hosted in a plain `QQuickView` — probe tree/click/property paths work without `QQmlApplicationEngine` |
| `ProbeIntegration_widgets` | pure Qt Widgets target: tree, property set, focus, dialog accept/reject by synthesized click, native ComboBox via property |

## Compatibility

| Component | Requirement |
|---|---|
| Target Qt | Qt 6.2+ (Core, Gui, Widgets, Network, Quick, QuickControls2) |
| Probe injection | Linux (`LD_PRELOAD`), any platform (style-plugin trick or source-level init) |
| Inspector GUI | Linux / macOS / Windows desktop, same Qt version family as practical |
| Tools | Python ≥ 3.9, standard library only |
| Wire protocol | version `1.0` — negotiated in the `hello` handshake; older probes (0.1.x) work but are flagged |

## Limitations

- Qt 6 targets only (the probe links Qt6::Core/Gui/Widgets/Quick).
- The probe runs in the target's GUI thread; building the full tree of a
  *huge* application can cause a brief hiccup on refresh.
- Property push notifications require the property to have a `NOTIFY` signal;
  constant/notification-less properties update on refresh/reselect.
- The pick click is consumed by the picker (the app does not react to it) —
  this is intentional.
- On Wayland, cross-window picking works only for windows of the target
  process (which is exactly what this tool inspects anyway).

## License

MIT — see [LICENSE](LICENSE).

This project links **dynamically** against Qt 6 (QtCore, QtGui, QtWidgets,
QtNetwork, QtQml, QtQuick), which is available under the
[LGPLv3](https://www.qt.io/licensing/) in the Qt open-source edition.
Dynamic linking keeps this project's MIT license unaffected; just don't
statically link a commercial-free Qt build into a closed-source derivative
without reviewing the LGPL relinking requirements.

## Acknowledgements

QtLiveInspector was created by [NepTech](https://github.com/technep)
**in co-programming together with an AI coding assistant** (Kimi, Moonshot
AI): NepTech conceived the idea, specified every feature — the probe
protocol, popup-overlay testing, touch visualization, the monkey tester,
the standalone inputviz library — and built, ran and validated everything
on a real Qt 6 setup, while the assistant designed and implemented large
parts of the codebase side by side. All code is original — no code was
copied from KDAB GammaRay or any other project; GammaRay served as
conceptual inspiration only.

