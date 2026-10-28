# 02 — PatchCore UI plan: Reason-inspired frontend for Cardinal

Status: draft v0.1 (2026-10-28) · Mockups: `mockups/index.html`

## Goal

Give Cardinal a realistic, hardware-like workspace inspired by Reason. Modules should
look as if they are mounted in a real studio rack, and the workflow should be
coherent: browser on the left, rack in the center, transport at the bottom, and a
back panel for patching.

We do this **without breaking module compatibility.** Third-party panels, widgets
and patches stay untouched.

## Design principles

1. **The chrome is quiet, the hardware is loud.** The app chrome is a neutral dark
   grey (as in Reason 14), so module panels carry the color.
2. **Realism lives in the frame, not in the modules.** We get the look from rails,
   case, screws, shadows, cables and lighting, not by repainting third-party SVGs.
3. **Everything you need is one keystroke away.** `Tab` flips the rack, `B` toggles
   the browser, `F` folds a row, `Space` starts and stops the transport.
4. **Always backwards compatible.** A "Classic" theme keeps today's Cardinal look.
   Patch files stay identical. New layout metadata is optional and namespaced.
5. **Performance first.** Static frame art is cached in framebuffers, so there is no
   per-frame cost on software GL or wasm.

## Information architecture

```
┌───────────────────────────── Top bar (app menu · nav buttons · patch name · CPU) ──┐
│┌── Browser ──┐┌────────────────── Rack view ─────────────────────┐┌─ Navigator ─┐│
││ Search      ││  [19" frame]                                       ││ minimap of  ││
││ Locations   ││   Row 1: Euro case (3U) — modules w/ screws       ││ rack rows   ││
││ Tags        ││   Row 2: Euro case (3U)                            ││ + viewport  ││
││ Results     ││   Row 3: Host I/O "1U utility" strip               ││ rectangle   ││
││ Preview     ││  FRONT  ⇄ (Tab) ⇄  BACK (cables, jacks, labels)    ││             ││
│└─────────────┘└────────────────────────────────────────────────────┘└─────────────┘│
├──────────────────────── Transport (LCD tempo/sig/pos · play/stop/rec · DSP meter) ─┤
└────────────────────────────────────────────────────────────────────────────────────┘
```

## Feature set (by phase)

### Phase 0 — Foundations
- **Theme system** (`ThemeTokens`): colors, materials and sizes in a single table,
  with Classic and PatchCore Dark themes. Stored in Cardinal settings.
- **Asset pipeline** for frame art: rail, screw, rack ear and case textures as SVG,
  with PNG fallbacks at 1x and 2x, in `src/Cardinal/res/patchcore/`.
- **Framebuffer caching** for static decoration (`widget::FramebufferWidget`).

### Phase 1 — The rack looks real (highest visual payoff)
- **Rack frame renderer:** draw 19" rack ears and vertical side posts with screw
  holes, aluminium rails above and below each row, and a dark recess behind empty HP.
- **Module mounting:** each `ModuleWidget` gets two or four drawn screws (unless the
  panel already has screws), a contact shadow on the rail, and an ambient-occlusion
  gradient at its edges.
- **Lighting pass:** an optional subtle vignette and top-left gloss gradient over
  the panels (with an adjustable "Panel sheen" amount), plus a light bloom on LEDs
  (the existing halo setting).
- **Cables 2.0 (rendered on the back view only):** thicker cables with a tube gradient and a highlight line, molded
  plug heads, realistic sag, and a short wobble when a module moves. Optionally,
  each cable takes its color from the port type (audio, CV, gate, poly).
- **Selection look:** a thin accent outline and a lifted shadow, as Reason does.

### Phase 2 — Workspace chrome
- **Top bar:** replaces `MenuBar.cpp`. App menu, then nav buttons (Rack, Back, Mixer
  [later], Browser), patch name in an LCD capsule, then the CPU/DSP meter.
- **Docked Browser:** a left panel that replaces the full-screen modal. It has
  search, locations (Favorites, Recent, Brands, Tags), a result list with panel
  thumbnails, a preview card, and drag-to-rack. The modal remains available in the
  Classic theme.
- **Transport bar:** a bottom strip with LCD-style tempo, signature and position
  (tied to host time when running as a plugin, and to an internal clock when
  standalone), play/stop/record, the master level meter, and the DSP load.
- **Navigator:** a right-edge minimap of the rack with a draggable viewport.

### Phase 3 — Back panel
- **Tab flip:** an animated flip (scale-X squash at 150 ms) between the front and the
  back of the rack.
- **Back panel rendering:** a generated back for every module: a dark sheet-metal
  plate, its name stencilled vertically, a serial sticker, and *only the jacks*,
  shown at the same positions as on the front and labelled with port names from
  `PortInfo`. We generate this automatically, so no plugin changes are needed.
- **Sockets live only on the back (decided):** the front panel shows no sockets.
  Every `PortWidget` is hidden on the front and drawn on the generated back plate.
  In the mockup, panels close up the gaps where the sockets were.
  *Caveat for real modules:* third-party SVG panels have the socket rings and port
  labels printed into the artwork. Hiding `PortWidget`s removes the jack nuts, but
  the printed graphics stay. Options: (a) cover each port box with a small
  panel-colored "blanking cap" sampled from the panel, (b) render panels from SVG
  with port-area masks, (c) accept the printed rings. The mockup assumes (a) or (b).
- **Cables live only on the back (decided):** as in Reason, the front view draws no
  cables at all. Patching, unpatching and
  inspecting cables happen only on the back. The front stays an uncluttered
  performance surface for knobs, sliders and displays. A Classic theme setting can
  bring back today's front-panel cables for users who prefer them.

### Phase 4 — Workflow features
- **Fold:** collapse a row, or a single module, to a 1U nameplate strip.
- **Containers ("Combinator-like"):** a group frame around a selection, with a
  name, color and four macro knobs mapped through Cardinal's existing parameter
  mapping. Saved as optional patch metadata.
- **Auto-route:** a new audio module auto-cables to the selected module's outputs,
  or to Host Audio.
- **Inspector / tool window:** a floating panel that shows the parameters of the
  selected module (name, value, unit) and lets you edit them precisely.
- **Track/Mixer view (stretch goal):** a channel-strip overview generated from the
  Host Audio and mixer modules.

## Technical approach (where code lands)

| Area | Files (current) | Change |
|---|---|---|
| Scene layout | `src/override/Scene.cpp` | Add docked Browser, TopBar, TransportBar and Navigator; shrink the `RackScrollWidget` rect |
| Menu → Top bar | `src/override/MenuBar.cpp` | New `TopBar` widget; keep menus as dropdowns |
| Module chrome | `src/override/ModuleWidget.cpp`, `src/CardinalModuleWidget.cpp` | `drawShadow` → mount shadow + screws; back-panel draw path |
| Rack background | `src/Rack/src/app/RackWidget.cpp`, `RailWidget.cpp` (submodule) | Override via `src/override` copy or a diff in `src/override/diffs/` |
| Cables | `src/Rack/src/app/CableWidget.cpp` | New render routine, behind the theme flag |
| Browser | `src/custom/Browser.cpp` | Docked layout mode |
| Settings / theme | `src/override/common.cpp`, Cardinal settings | `patchcore.theme`, `patchcore.backCablesOnly`, … |
| Assets | `src/Cardinal/res/` | New `patchcore/` folder |

The Rack submodule is still being cloned. File names under `src/Rack` come from the
upstream VCV Rack 2 layout and must be verified once the clone finishes.

## Risks & open questions

- **Panels that already draw screws or rails:** detect them with SVG id heuristics,
  or provide a per-plugin allow/deny list.
- **Software GL / wasm performance:** cache static layers and keep the cable shading
  cheap (two strokes, not a gradient mesh).
- **Upstream merges:** keep Rack changes as small, isolated patches.
- **The Back panel needs port positions only.** `PortWidget` boxes already provide
  them.
- **Trademarks:** we are *inspired by* Reason. We do not copy Reason assets, device
  designs or names.

## Success criteria

- Side-by-side screenshots read as a "studio rack", not a "web canvas".
- No measurable FPS regression on a 50-module patch with the default settings.
- Every existing patch loads and works identically. The Classic theme is a
  pixel-for-pixel match with today's look.
