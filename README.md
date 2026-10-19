# QtLiveInspector

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
- **Remote** — the probe speaks plain JSON over TCP; point the inspector at
  any reachable host. Optional shared-token auth (`QLI_TOKEN`).

## Repository layout

```
QtLiveInspector/
├── CMakeLists.txt          top-level build
├── protocol.txt            wire protocol reference
├── probe/                  libqtinspectorprobe — injected into the target
├── inspector/              qtliveinspector — the GUI client
├── samples/quickdemo/      sample Qt Quick target app
└── tools/qli_cli.py        minimal Python CLI client (no Qt needed)
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

### Probe configuration (environment of the *target*)

| Variable | Default | Purpose |
|---|---|---|
| `QLI_PORT` | `17017` | TCP port the probe listens on (all interfaces) |
| `QLI_TOKEN` | *(empty)* | shared secret; clients must send the same token |

When injection succeeds the target prints:

```
[QtInspector] probe active, listening on port 17017 (pid 12345)
```

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

The probe listens on all interfaces, so a direct LAN connection works out of
the box. For anything else, use an SSH tunnel (recommended — it also
encrypts):

```bash
# on the remote host:
QLI_PORT=17017 QLI_TOKEN=s3cret \
  LD_PRELOAD=/path/to/libqtinspectorprobe.so ./your-qt6-app

# on your workstation:
ssh -L 17017:127.0.0.1:17017 user@remote-host
./qtliveinspector    # connect to 127.0.0.1:17017, token "s3cret"
```

## Headless testing: qli_cli.py

`tools/qli_cli.py` speaks the probe protocol directly — useful for CI and
remote targets where no GUI is available:

```bash
python3 tools/qli_cli.py --host 192.168.1.10 tree | head
python3 tools/qli_cli.py props --name volumeSlider       # or: --node 42
python3 tools/qli_cli.py find clickButton
python3 tools/qli_cli.py locator --name clickButton
python3 tools/qli_cli.py highlight --name animatedBall --seconds 3
python3 tools/qli_cli.py set --name volumeSlider value 75   # write a property
python3 tools/qli_cli.py click --name clickButton           # click a button/item
python3 tools/qli_cli.py invoke --name clickButton clicked  # emit/call a method
```

Object-addressing commands accept either `--node <id>` (runtime id, shown by
`tree`/`find`) or `--name <objectName>`. The id form is exact; the name form
requires the objectName to exist and be unique — on duplicates the probe
replies with the conflicting node ids so you can pick one.
```

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

QtLiveInspector was bootstrapped by an AI coding assistant (Kimi, Moonshot
AI) in pair-programming with a human developer: the assistant designed and
wrote the initial implementation, the human built, ran and validated it on a
real Qt 6 setup. All code is original — no code was copied from KDAB
GammaRay or any other project; GammaRay served as conceptual inspiration
only.

