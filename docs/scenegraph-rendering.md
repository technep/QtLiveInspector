---
name: qt-scenegraph-rendering
description: Hard-won facts about Qt Quick scene-graph rendering in the QtLiveInspector probe (what renders reliably, what silently fails, threading rules, and how to verify overlays visually). Use when working on InputVisualizer, Highlighter, QuickTouchOverlay, or any probe-side QSG rendering — especially "circle/item doesn't render" or "edges not smooth" bugs.
---

# Qt Quick scene-graph rendering in the probe

Verified on Qt 6.8.3, RHI-OpenGL (llvmpipe) and offscreen; treat as
project-wide constraints, not environment quirks, unless re-verified.

## What renders reliably (use this recipe)

`QSGGeometryNode` + `QSGFlatColorMaterial` + **triangle strips**
(`DrawTriangleStrip`, non-indexed, 2·(n+1) vertices). This is the only
combination that has never failed. Reference implementation:
`makeBand()` in `probe/InputVisualizer.cpp`.

Smooth circles without textures or vertex colors — composite concentric
flat-color bands:

- fill: soft halo band (α~22) + solid core band (α~60)
- stroke: 1px fade-out (α~110) / solid (α255) / fade-in (α~110)
- band widths in **fixed pixels**, not proportional — reads as ~1px AA at
  any radius (release ripples grow to ~50px and stay smooth)
- adaptive segments `n = qBound(48, int(maxR*3), 128)` — facets < 0.7px

## What silently fails (do not re-try without re-verifying)

| Approach | Symptom |
|---|---|
| Triangle fans (`DrawTriangleFan`, topology 0x6) | Never render on some RHI backends; log says "Primitive topology 0x6 not supported". Use strips or indexed triangle lists. |
| `createTextureFromImage` + `QSGImageNode`/`QSGSimpleTextureNode` | **Silently invisible** — verified with an opaque-red texture through the minimal node path while a `QSGSimpleRectNode` in the same tree rendered. Qt's own `Image` items work because they use the internal texture-factory path. |
| `QSGVertexColorMaterial` + `ColoredPoint2D` gradients | Solid colors render; **per-vertex alpha gradients are ignored** (uniform quad). Don't use for feathering. |
| `QSGSimpleTextureNode` | Class doc: "only functional with the default or software backends" — prefer `createImageNode()` even though it also failed above. |

## Threading rules (enforced in `QuickTouchOverlay`)

- `updatePaintNode()` runs on the **render thread**: only read plain member
  data written before `update()`. Never call `mapFromGlobal()` / item
  transform queries there.
- Map global→item coordinates in `setPoints()` (GUI thread), store local
  coords, render thread consumes them. See `QuickTouchOverlay::setPoints`.

## QSG API gotchas

- `QQuickItem::boundingRect()` is NOT the visual geometry for text editors
  (QQuickTextField/TextInput return the text-content bounds — ~1×80 px for
  an empty field). Click/press/overlay code must use
  `QPointF(width()/2, height()/2)` instead. Clicks landed at the left edge
  for months without anyone noticing (still inside the item).
- `QQuickItem::mapFromGlobal()` routes through the SCREEN containing the
  point — on multi-monitor setups it can be the wrong screen (observed:
  a constant 323 px shift under XWayland). Never track by global frames;
  scene coordinates are immune.
- Qt Quick re-posts input events to items during delivery and may rewrite
  their positions — an application event filter sees several passes. Only
  the first (window/widget-level) delivery is authoritative.
- `QSGNode::setFlag(Flag)` takes a **single** flag — call it twice for
  `OwnsGeometry`/`OwnsMaterial` (`QFlags` won't compile).
- `updatePaintNode` rebuild pattern: `delete oldNode;` then return the new
  tree; return `nullptr` when idle to drop geometry/GPU memory.
- Keep ownership flags consistent or the scene graph leaks/crashes at
  teardown.
- `QQuickPaintedItem` allocates a full-window texture and rasterizes +
  uploads every frame — too heavy for embedded; use raw geometry.

## The probe runs inside customer processes

- Any probe bug is a host-app crash. Command dispatch is wrapped in
  try/catch (`ProbeController::onReadyRead`); keep it that way when adding
  commands.
- The integration tests (`ProbeIntegration_*` in ctest) catch startup
  crashes — e.g. they caught `setMultiTouch()` called before the
  `InputVisualizer` existed (null deref at boot). Run `ctest` after any
  probe change.

## How to verify overlays end-to-end

1. Unit-test state machines without a window: instantiate the class,
   `setEnabled(true)`, inject events with
   `QCoreApplication::sendEvent(qApp, &event)` — they pass through the
   application-wide event filter exactly like real input. See
   `tests/test_inputvisualizer.cpp` (uses `#define private public`).
2. Visual check: probe `screenshot` command returns base64 PNG
   (`ProbeClient.command("screenshot")`); ripples last 0.4s, so click then
   screenshot immediately. Zoom crops with PIL (`Image.NEAREST` shows raw
   pixels; `LANCZOS` for fair looks).
3. In-app bisection matrix: when a primitive doesn't render, render 2–3
   known-good/bad variants as siblings in the same tree in ONE build —
   position and data were both red herrings here; only the primitive type
   mattered.

## Desktop-input caveat (for real-input testing)

On GNOME Wayland with an XWayland demo: the demo may run on Wayland (no X
window — find windows via `_NET_WM_PID` under XWayland), a fullscreen
mutter "guard window" eats all XTEST input even above restacked windows,
and XWayland window coords can be negative (pointer warps get clipped).
Prefer injecting events via `sendEvent` (method 1) over python-xlib.

## Working with the running demo

- `pkill -x quickdemo` (never `pkill -f quickdemo` — it matches your own
  shell's command line and kills it).
- The probe auto-generates a token:
  `TOKEN=$(grep -oP 'auth token: \K[0-9a-f]+' /tmp/quickdemo.log)`, then
  `QLI_TOKEN=$TOKEN python3 tools/qli_cli.py ...`.
- Rebuild + restart is required for probe .so changes (it's preloaded).
