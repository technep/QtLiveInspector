# Changelog

All notable changes to QtLiveInspector. This project adheres to
[Semantic Versioning](https://semver.org/) for the probe/inspector/tools as
one release unit; the wire protocol has its own `major.minor` version
negotiated in the `hello` handshake.

## [Unreleased]

## [0.3.0] — 2026-11-07

### Added

- **Monkey mode for the input visualizer**: swap the touch circle for a
  procedurally drawn monkey head (no asset files) — the visual marker for
  `qli_monkey.py` stress runs. Enable via `QLI_INPUT_VIZ_MONKEY=1` or at
  runtime with `setInputVisualizer {monkey:true}`; `qli_monkey.py` toggles it
  automatically around a run. Press/hold/release animation is unchanged
  (hold gets an orange halo behind the monkey).
- **Standalone input visualizer library** `libqtinspectorinputviz.so`
  (`inputviz/`): the probe's `InputVisualizer` touch/mouse ripple as a pure
  `LD_PRELOAD` lib with **no network surface** — for showing touch feedback
  on devices without the inspector attached. `QLI_INPUT_VIZ=0` disables,
  `QLI_INPUT_VIZ_MULTITOUCH=1` shows all touch points (default: first only).
- **Two new sample targets** alongside quickdemo: `samples/quickviewdemo/`
  (same QML surface hosted in a plain `QQuickView` under `QApplication`) and
  `samples/widgetdemo/` (pure Qt Widgets, same `objectName`s) — so the
  integration suite exercises both QML application styles and the Widgets
  profile. New ctest cases `ProbeIntegration_quickview` and
  `ProbeIntegration_widgets`.
- **quickdemo is now a popup-overlay test target**: a modal Settings dialog
  (opened from the toolbar), a quick-actions popup, and a ComboBox whose
  dropdown delegates (`comboItem0..3`) live in the `QQuickOverlay` layer.
- Protocol **1.1**: `mouseDown` / `mouseUp` (granular press/release — opens
  a ComboBox without selecting, holds a slider; `mouseUp` without a target
  releases at the last `mouseDown`) and `mouseMove {x,y,buttons}` (global
  coordinates, delivered to the window under the point). CLI: `mouse-down`,
  `mouse-up`, `mouse-move`.
- Probe integration test covers the whole popup scenario: dialog
  accept/reject by click, popup actions, combobox delegate selection, and
  press/release button semantics.

### Fixed

- **Object tree and name resolution missed visually-parented items**:
  `Dialog`/`Popup`/dropdown content is attached to the scene via
  `parentItem()` without a QObject parent, so `QObject::children()` /
  `findChildren<>()` never saw it (comboBox delegates were the tell-tale).
  The tree walk and `resolveObject` now follow `QQuickItem::childItems()`
  alongside QObject parenthood.
- **Highlight and touch-visualization overlays rendered behind popup
  overlays**: `QQuickOverlay` sits at z=1000001; both overlays now use
  z=1e9.
- quickdemo: `lastAction` no longer reports a spurious `combo:*` at startup
  (`onCurrentTextChanged` fired during binding init; now `onActivated`).
- `setInputVisualizer` without an `enabled` key now leaves the enabled state
  untouched instead of defaulting it to true, so styling-only commands do
  not force-enable the visualizer.
- **Synthesized input used `boundingRect().center()`**, which is not the
  visual geometry for text editors (QQuickTextField reports text-content
  bounds — ~1×80 px when empty): clicks/presses on text fields landed at
  their left edge. All input paths now use `width()/height()` centers.
- **widgetdemo uses plain `QPushButton`s in its dialog, not
  `QDialogButtonBox`**: under the `offscreen` platform, standard buttons in a
  button box never propagate synthesized clicks (neither `QTest::mouseClick`
  nor probe `mouseDown`/`mouseUp` reach them) — a platform quirk, not a probe
  bug. Plain buttons work, so the dialog exercises the real click path.
- **Segfault on target exit with the standalone inputviz lib**: the
  `InputVisualizer` is parented to the `QApplication`, so on shutdown its
  destructor ran inside `~QApplication` and called
  `qApp->removeEventFilter(this)` on the mid-destruction app object → SIGSEGV
  in `QObject::removeEventFilter`. The destructor now skips filter detachment
  when `QCoreApplication::closingDown()` (member cleanup only — no further
  events are dispatched at that point). The full probe was unaffected because
  it creates the visualizer disabled.
- **Touch visualization tracked `event->globalPosition()`**, which is not
  trustworthy on multi-monitor setups (`QQuickItem::mapFromGlobal` routes
  through the screen containing the point — a constant 323 px shift was
  observed under XWayland). The visualizer now tracks SCENE coordinates
  from the first window-level delivery (item-level re-posts are ignored —
  Qt Quick rewrites event positions during delivery).
- `mouseMove` gains an item-relative form: `{node|objectName, dx?, dy?}`
  converts through the app's own transform chain, robust where external
  and Qt-internal global frames disagree.

## [0.2.0] — 2026-11-06

### Security (potentially breaking — see notes)

- **Probe binds 127.0.0.1 by default.** Previously it listened on all
  interfaces. To expose it on the network, start the target with
  `QLI_HOST=any` (or a specific address).
- **Token authentication is always on.** If `QLI_TOKEN` is unset the probe
  now generates a random token and prints it once to the target's log,
  instead of silently allowing unauthenticated access. An explicitly empty
  `QLI_TOKEN` restores development no-auth mode (logged loudly).
- **Read-only mode**: `QLI_READONLY=1` accepts only observation commands
  (`hello`, `tree`, `properties`, `screenshot`, `perfStats`, `isIdle`,
  `locator`) and rejects every mutating command with a clear error.
- **Audit log**: every authenticated mutating command is logged to the
  target's log with peer address and target object.
- **Note for existing users**: any workflow that connected without a token
  must now read the token from the target's log (`auth token: ...`) or set
  `QLI_TOKEN` explicitly on the target.

### Added

- Wire-protocol versioning (`1.0`), negotiated in the `hello` handshake —
  major-version mismatches now fail with an explicit error instead of
  mysterious breakage. Clients report `protocolVersion`; tools warn on
  older probes.
- Touch/mouse input visualization (`setInputVisualizer`) now works in
  **Qt Quick** applications: a `QQuickPaintedItem` ripple overlay is used
  when no top-level QWidget exists. Rings also paint immediately on input,
  without waiting for the animation timer tick.
- Test suite (ctest): `ToolSmokeTest` (tool compile + stub-probe sessions +
  token enforcement), and probe integration tests that inject the real probe
  into quickdemo offscreen — basic operation, read-only mode, no-auth mode,
  and a hostile-input robustness barrage.
- `examples/features/quickdemo.feature` — sample Gherkin feature referenced
  by the docs.
- `LICENSE` (MIT).
- CMake install rules: probe library, inspector GUI, tools, docs.

### Fixed

- `tools/qli_cli.py` crashed with a SyntaxError on startup (orphaned `elif`
  block); `touchvis`, `perf` and `idle` commands were unreachable.
- `tools/qli_mcp.py` crashed with a SyntaxError on startup (unterminated
  f-string in `get_perf_stats`) — the entire MCP server was unusable.
- Input visualizer: widget overlay painted global coordinates directly, so
  ripples were offset whenever the window was not at the screen origin.
- Input visualizer: touch ripples were only pushed on the 16 ms timer tick,
  so screenshots taken within one frame of a click showed nothing.
- `tests/` was never added to the build; `test_jsonutil` did not compile
  (missing link to `Qt6::Test`).
- `enable_testing()` was scoped to the tests subdirectory, so `ctest` from
  the build root found no tests.

## [0.1.0] — initial release

- In-probe shared library (three injection methods), inspector GUI,
- newline-JSON protocol, CLI, MCP server, BDD runner, monkey tester,
- quickdemo sample application.
