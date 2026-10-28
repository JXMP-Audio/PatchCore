# 04 — Sided modules and the Patchbay

Status: approved design (2026-10-28) · Follows [02-ui-plan.md](02-ui-plan.md), replaces its
"sockets and cables live only on the back" decision with a per-module rule.

## Goal

PatchCore gets two kinds of modules:

- **Basic modules**: every module that exists today (Cardinal's own and all third-party
  plugins). They stay exactly as they are: sockets and cables on the front.
- **JXMP modules**: our own modules. Their sockets and cables are on the back, except for
  ports the module author marks as front ports.

A **Patchbay** module connects the two worlds: whatever is plugged into front jack *n*
comes out of back jack *n*, and the other way round.

## Decisions

| Question | Decision |
|---|---|
| Back of a basic module | A blank plate (name, sticker, power header), no jacks |
| Cable from a front jack straight to a back jack | Not allowed. Front ↔ back only goes through a Patchbay |
| Which ports of a JXMP module are on the front | Decided by the module author in code. Default: back |
| Patchbay direction | Bidirectional per channel, one visible jack per channel and side ("smart jack") |
| Patchbay signals | Anything: audio, CV, gate, polyphony up to 16 channels |
| Patchbay sizes | Three modules: 4, 8 and 16 channels |
| The "Front cables" setting | Removed. The Classic theme keeps today's look (everything on the front); the PatchCore look uses the new rule |

## Architecture

### Port sides (`include/patchcore/SidedModuleWidget.hpp`)

```cpp
namespace rack::patchcore {
enum PortSide { SIDE_FRONT = 0, SIDE_BACK };

/** Implemented by the widgets of JXMP modules. Widgets without it are basic modules: every port on the front. */
struct SidedModuleWidget {
	virtual ~SidedModuleWidget() {}
	/** Side of an input or output. Called every frame from the UI thread: keep it cheap and constant. */
	virtual PortSide portSide(engine::Port::Type type, int portId) const { return SIDE_BACK; }
};

/** Side of `port`, SIDE_FRONT for basic modules (src/patchcore). */
PortSide portSide(app::PortWidget* port);
/** True when the module widget has at least one back port. */
bool hasBackPorts(app::ModuleWidget* mw);
}
```

A module opts in on its **widget**: `struct MyWidget : app::ModuleWidget, patchcore::SidedModuleWidget`.
It is the widget, not the module, because module browser previews have no module instance
but must still know their sides. The UI finds the interface with `dynamic_cast`. The header lives
in `include/` so plugins can use it. Rack (`src/Rack`) and third-party plugins are not changed.

### View sync (`src/override/ModuleWidget.cpp`)

`ModuleWidget_syncPatchCoreView` works per port instead of globally:

- **Front view:** hide every `PortWidget` whose side is `SIDE_BACK`. Everything else stays.
- **Back view:** hide everything except `PortWidget`s whose side is `SIDE_BACK`. A basic
  module therefore shows an empty generated plate.
- **Blanking caps** (panel-colored circles over sockets printed in the SVG) are drawn only over
  back ports, so basic modules are never covered.
- The back plate's jack labels (`BackPanel.cpp`) list only back ports.
- Classic theme: no hiding at all, as today.

`hideFrontPorts()` and `settings.frontCables` are removed together with the View menu item
in `MenuBar.cpp`. A `frontCables` key left in an old `PatchCore.json` is ignored.

### Cables (`src/override/CableWidget.cpp`)

- A cable belongs to the side of its ports. It is drawn and hit-tested only while the rack
  shows that side. The cable being dragged follows the side of the port it started from.
- Ports on the other side are hidden, so a cross-side cable cannot be made in the UI.
- Cables that load from a patch with ends on different sides (only possible from a hand-edited
  patch) stay connected and are drawn on the front, so nothing silently breaks.

### JXMP plugin (`plugins/JXMP/`)

A new built-in plugin, registered like `plugins/Cardinal/` in `plugins/Makefile` and
`plugins/plugins.cpp`. Brand "JXMP Audio", slug `JXMP`. All our modules live here. Panel art
is our own SVG in `plugins/JXMP/res/`.

## Patchbay

### Modules

| Model slug | Name | Channels | Width |
|---|---|---|---|
| `Patchbay4` | Patchbay 4 | 4 | 4 HP |
| `Patchbay8` | Patchbay 8 | 8 | 4 HP |
| `Patchbay16` | Patchbay 16 | 16 | 8 HP (two columns) |

One `Patchbay<N>` template implements all three.

### Ports

Channel *k* has four Rack ports: front input, front output, back input, back output.
`portSide()` returns `SIDE_FRONT` for the front pair and `SIDE_BACK` for the back pair, so the
Patchbay is visible on both sides. On each side the input and output of a channel sit at the
same position under one visible **smart jack**. Front jack *k* and back jack *k* have the same
position and number.

### Direction

Each channel is `FREE`, `FORWARD` (front → back) or `REVERSE` (back → front).

- The first cable decides:
  - a cable from another module's output into the front jack → `FORWARD`;
  - a cable from the front jack into another module's input → `REVERSE`;
  - the same rules mirrored for the back jack.
- When a drag starts on a free jack, the role is decided on drop, by the type of the target port.
- While any cable is on the channel the direction is locked. Each smart jack accepts only cables
  that fit its current role: the input side takes one cable, the output side any number.
- When the last cable is removed the channel is `FREE` again.
- The direction is derived from the connected ports in `process()` (which input is connected),
  so nothing extra is saved in the patch.

### Signal

`process()` copies the voltages and the channel count of the connected input to the opposite
output, unchanged (polyphony up to 16). Like every Rack cable this adds one sample of delay.

### Panel

Each channel shows its number and an LED with the direction arrow (→ / ←), lit by the signal
level. The back is the generated plate with the same numbers at the same positions.

### Smart jack implementation

`SmartJack` is a widget that draws one socket and owns the hidden input and output
`PortWidget`s at its position. It forwards mouse and drag events (`onDragStart`, `onDragHover`,
`onDragDrop`, `onButton`) to the port that matches the current role and the incomplete cable.

**Risk:** Rack has no notion of two ports at one position. The first implementation step is a
spike that checks whether forwarding the events works with Rack's cable dragging (including
picking up an existing cable and Ctrl/Shift drag). **Fallback** if it doesn't work reliably:
one visible jack plus a small direction switch per channel on the front panel.

## Testing

- Unit-style checks in a debug build: `portSide()` for basic and JXMP modules, the direction
  state machine of the Patchbay (FREE → FORWARD/REVERSE → FREE).
- Manual checks in the standalone build (`PATCHCORE_DEV_*` aids for screenshots):
  - an existing patch with only basic modules looks and works the same as before;
  - the back of a basic module is a blank plate;
  - patch basic VCO → Patchbay front 1 → back 1 → JXMP module, and the reverse;
  - polyphonic cable through a Patchbay keeps its channel count;
  - save and reload: cables and directions come back;
  - Classic theme still shows everything on the front.

## Out of scope

- The first real JXMP instrument or effect (comes after this, using `SidedModule`).
- Cables drawn "through" the rack between sides.
- User-switchable port sides.
