# 07 — Realistic cables and jacks

Status: approved design (2026-10-29) · PatchCore look only, the Classic theme keeps today's cables

## Goal

Cables that look and move like real patch cables: real plugs, a cable that hangs and swings
like a rope, a soft shadow on the panel, a short "click" when a plug goes in. Users pick the
colour and the type of each cable.

## Decisions

| Question | Decision |
|---|---|
| Plug on the front (basic modules, Eurorack sockets) | **3.5 mm mini jack**: knurled metal barrel, coloured rubber boot |
| Plug on the back (JXMP modules, our rack) | **6.3 mm studio jack**: chrome barrel, longer body, coloured boot |
| Plug size | 3.5 mm plugs sized to the Eurorack socket. 6.3 mm plugs need bigger sockets: JXMP back sockets get a 6.3 mm socket graphic, and back layouts use a pitch that fits it |
| Rendering | Procedural NanoVG (no images), sharp at every zoom, any colour |
| Physics | Rope simulation (Verlet chain) with gravity, length and stiffness |
| Extras | Shadow on the panel; plug-in animation |
| Colour | Chosen by the user |
| Cable types | Standard (rubber), LED, Braided (textile), Coiled (spiral); any of them with a classic or a **stackable** plug |
| Where to choose | View menu: default colour (or "cycle" as today) and default type for new cables. Right-click on a plug: colour and type of that cable. Saved in the patch |
| Classic theme | Unchanged |

## Architecture

### Rope simulation (`src/patchcore/CableRope.hpp`, pure, tested)

No Rack dependency (own `Vec2`), so `g++` tests run it alone.

```cpp
namespace patchcore {
struct RopeParams {
	int points = 20;          // including both ends
	float slack = 1.12f;      // rope length / end distance, at least a minimum length
	float minLength = 60.f;   // px at zoom 1
	float gravity = 900.f;    // px/s²
	float damping = 0.985f;   // velocity kept per step
	int iterations = 8;       // constraint passes per step
	float stiffness = 0.15f;  // bending resistance, 0..1
};
struct Rope {
	void reset(Vec2 a, Vec2 b, const RopeParams&);   // straight sag, no motion
	void step(Vec2 a, Vec2 dirA, Vec2 b, Vec2 dirB, float dt, const RopeParams&);
	bool asleep() const;                               // settled, no work until the ends move
	const std::vector<Vec2>& points() const;
};
}
```

- Ends are pinned to the plugs. The second and second-to-last points are pulled along the plug
  direction, so the cable leaves the plug straight.
- Fixed sub-steps (1/120 s), at most 4 per frame, so a slow frame never explodes.
- `asleep()` when no point moved more than 0.05 px for 0.5 s and the ends did not move.
  A sleeping rope costs nothing. Moving an end wakes it.
- Types change parameters: Braided is stiffer (`stiffness 0.35`), Coiled is shorter and
  springier (`slack 1.0`, `minLength 40`, lower damping).

### Drawing (`src/patchcore/CableDraw.cpp`)

- **Cable:** a smooth path through the rope points (Catmull-Rom to Bézier). Tube shading as today
  (dark rim, body, highlight towards the top-left light), width by plug size and polyphony.
  - Standard: the shading above.
  - Braided: plus a fine diagonal cross-hatch along the path (two dashed strokes at an offset).
  - Coiled: a helix drawn around the path (a sinusoid of fixed pitch, front/back halves shaded
    differently). The physics path stays the centre line.
  - LED: the boot and a thin core line glow by the signal level of the output (the existing
    voltage read in `CableWidget::drawLayer`), colour = cable colour.
- **Plugs** (drawn in plug coordinates, rotated to the cable direction):
  - `drawMiniJack` 3.5 mm: knurled barrel (stripes), short collar, boot.
  - `drawStudioJack` 6.3 mm: chrome barrel with two highlight bands, longer body, boot.
  - The plug type follows the side of its socket (`portSide()`): front = mini, back = studio.
- **Shadow:** each cable and plug is first drawn as a soft dark stroke offset towards the
  bottom-right (two strokes, decreasing alpha), on the cable shadow layer that already exists.
- **Plug-in animation:** when a cable becomes complete, its plugs scale 1.15 → 1.0 over 120 ms
  with a short highlight flash. Lifting a plug scales it to 1.08.

### Cable style (`src/override/CableWidget.cpp`, settings)

- `CableWidget` gets a `patchcore::CableType type` next to its colour. It is saved in the
  cable's JSON as `"patchcoreType": "standard" | "led" | "braided" | "coiled"`; a missing key
  means the default type at load time, so old patches load unchanged.
- `patchcore::Settings` gets `cableType` (default for new cables) and `cableColorMode`
  (`cycle` = Rack's colour cycling, or a fixed colour stored as hex).
- New cables take the defaults when they are created (`RackWidget::getNextCableColor()` is
  used for `cycle`).

### Menus

- **View → PatchCore → Cables:** default type (4 radio items) and default colour (Cycle, then
  the colour swatches from Rack's cable colour list).
- **Right-click on a plug** (`PlugWidget::onButton` in the `CableWidget` override): a menu with
  the cable's colour swatches and the 4 types. Changes go into undo history as a cable colour
  change, like Rack's own colour change.

### JXMP back sockets

`plugins/JXMP` gets a `StudioSocket` port widget: a 6.3 mm socket graphic (bigger nut, drawn in
NanoVG or an SVG in `plugins/JXMP/res/`). The Patchbay's back jacks and every JXMP module's
sockets use it. Front jacks of the Patchbay stay Eurorack-sized (3.5 mm plugs from basic
modules land there).

### Stackable plugs

A stackable plug has a socket on its back, so another plug can go into it, like real stackable
patch cables. Plugs on one jack form a **tower**.

- **Attribute, not a type:** every cable type can have a classic or a stackable plug
  (`"patchcoreStackable": true` in the cable JSON). Right-click on a plug toggles it.
- **The rule, everywhere in the PatchCore look (front and back):** a jack holds one plug. A second
  cable on the same jack only goes in on top of a stackable plug.
- **Automatic change:** when a cable is dropped on an output that already has a classic plug,
  that plug turns stackable and the new plug goes on top. The change is part of the same undo
  step.
- **Outputs only.** An input takes one cable, as in Rack. Ruling: stacking on an input would mean
  a cable that starts somewhere other than its logical source port.
- **Order:** the tower order is the order of the cables on the port (oldest at the bottom).
  Each plug above is drawn slightly larger and offset towards the light, with a shadow on the plug
  below, so the tower reads as height.
- **Pulling a plug pulls the tower:** dragging the jack (the bottom plug) moves every cable of the
  tower; dropped on another output, the whole tower moves there. Dragging a higher plug of the
  tower takes that plug and everything above it. This needs plug hit-testing: `PlugWidget`
  events in the `CableWidget` override, and the drag/drop of several cables in a new
  `src/override/PortWidget.cpp` override (with its diff in `src/override/diffs/`).
- **Old patches** with several cables on one output load with all but the top plug stackable.
- **Classic theme:** unchanged (Rack's own unlimited cables per output, no towers).

## Performance

- A rope point costs a few multiplications per constraint pass: 100 awake cables × 20 points ×
  8 passes × up to 4 sub-steps is ~64 k updates per frame, fine on desktop; sleeping ropes cost
  nothing.
- The success check is the plan's measurement on a 50-module example patch: no visible FPS
  drop against the current cables with the default settings.

## Testing

- `g++` tests (`src/patchcore/tests/`, run by a script like `plugins/JXMP/tests/run.sh`):
  - a rope between two fixed ends settles below the straight line (sag) and falls asleep;
  - the rope length stays within 2 % of the target after settling;
  - moving an end wakes the rope;
  - a huge `dt` is clamped (no NaN, no explosion);
  - Coiled sags less than Standard for the same ends.
- Screenshots via `PATCHCORE_CAPTURE` of front (mini jacks) and back (studio jacks), every cable
  type.
- By the user: dragging, swinging when moving a module, right-click menu, save/reload keeps type
  and colour, Classic unchanged.

## Out of scope

Cable-to-cable collision, cable labels, per-cable thickness setting, stacking on inputs.
