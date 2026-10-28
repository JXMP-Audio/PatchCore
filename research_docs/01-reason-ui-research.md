# 01 — Reason UI research

Source pages reviewed (2026-10-28):
- https://www.reasonstudios.com/ (home)
- https://www.reasonstudios.com/reason (product page)
- https://www.reasonstudios.com/new-in-14 (Reason 14 changes)

The marketing pages say little about the UI itself. Most of the notes below come
from Reason's long-standing, well-known interface and are marked **[known UI]**.
What the pages say directly is marked **[page]**.

## 1. Brand positioning (what the product page sells)

- **[page]** Hero tagline: *"The rack. the instruments. the legacy. your sound."*
- **[page]** "The Rack is the heart of Reason". You wire up instruments, effects and
  *Player* MIDI effects to build sounds. "Reason is the wires and the workflow."
- **[page]** Devices are "hand-crafted in Sweden": synths, samplers, drum machines,
  audio effects. Sounds are "categorized and tagged for fast exploration".
- **[page]** Reason runs either as a plugin (Reason Rack) or as a full DAW.
- **[page]** Section rhythm on the product page: *What is Reason → From inspiration to
  release → The heart of Reason → A complete DAW → Patches, packs, patterns →
  Musical machines → Reason Hub*.

**Takeaway for PatchCore:** Cardinal already has the same core idea: a rack, wires,
and a plugin/standalone build. What Reason adds is the **presentation**. The rack
looks like real studio hardware, and browsing, transport and the rack all sit in one
coherent workspace.

## 2. Reason 14 changes that matter for us

- **[page]** *"One track mind"*: a new **Track Panel** holds the signal chain, levels
  and sends, so you can open the rack *in place* from a track.
- **[page]** *"Everything sounds better when it's pitched black"*: **Dark Mode is the
  default** everywhere (sequencer, mixer). The UI follows the theme setting.
- **[page]** New **navigation buttons** give quick access to the Track Panel, Edit
  Area, Groove Mixer and other views.
- **[page]** Track folders, smarter clips, a better piano roll, and automatic tempo
  detection on audio import.
- **[page]** New device: RV-9 Reverb Station, with nine algorithms. Its panel is
  orange and gray, which shows that each device keeps its own color identity.

## 3. Signature Reason UI concepts [known UI]

| Concept | What it is in Reason | Equivalent in Cardinal/VCV today |
|---|---|---|
| **19" rack** | Devices are horizontal 19" units (1U/2U/3U…) stacked vertically between metal rails with rack ears and screws | Eurorack modules (3U tall, N×HP wide) placed in rows on a flat grid background |
| **Front / back flip (Tab)** | Front shows controls. The back shows jacks, cable routing, labels, serial stickers and "device ID" plates | No equivalent. Cables are drawn over the front panels |
| **Cables** | Thick, coloured, sagging cables with shaded plugs. They wiggle when you move a device. Color can follow the source | Already present: cable tension and opacity settings live in `MenuBar.cpp` |
| **Auto-routing** | New devices auto-cable to the selected device or the mixer | Partially present (adding a module next to another). No auto-patch to audio out |
| **Combinator** | A container device that wraps a chain, with macro knobs and buttons and a collapsible panel | No container. The closest thing is selection + "save selection" |
| **Fold / minimise** | Each device folds to a thin 1U strip with its name, so the rack stays readable | No folding |
| **Rack columns** | Several rack columns side by side | One unbounded 2D canvas |
| **Browser (left)** | A docked browser with locations, categories, tags, patches, preview and drag-to-rack | A full-screen modal module browser (`Browser.cpp`) |
| **Transport (bottom)** | Hardware-style transport with LCD-like counters, tempo, signature, loop, click, CPU/DSP meter | Engine menu, plus Host Time inside modules |
| **Mixer (SSL-style)** | A console view with channel strips, insert FX and sends | Mixing happens through modules only |
| **Players** | MIDI-effect devices that sit on top of an instrument | MIDI modules plus sequencer modules |
| **Tool window / inspector** | A floating tool window for the current selection | Context menus |
| **Navigator** | A minimap of the rack on the right edge | None (scrolling and zoom only) |

## 4. Visual language [known UI]

- **Materials:** brushed and anodised aluminium rails, black powder-coat rack ears,
  Phillips screws, subtle bevels and drop shadows under each device. Each device has
  its own panel finish (wood ends, coloured metal, etc.).
- **Light:** soft top-left light source, an inner shadow at the rack rail, and
  specular highlights on knobs.
- **Displays:** green, amber or blue LCD/LED segments for transport counters,
  patch-name windows and the CPU meter.
- **Typography:** condensed, uppercase, small-caps labels screen-printed on panels.
  The application chrome (browser, toolbar) uses a clean sans.
- **Color:** the chrome stays neutral dark grey (the Reason 14 dark default) so the
  hardware panels carry all the color. Accents are sparse: a selection outline and
  the record button's red.
- **Feedback:** a selected device gets a thin highlight frame. Hovering a jack
  highlights it. A dragged cable leaves a "ghost" path.

## 5. Constraints on our side

- Cardinal draws everything with **NanoVG** through Rack's widget tree (`Scene` →
  `RackScrollWidget` → `RackWidget` → `ModuleWidget`s). The Reason look has to be
  built from NanoVG paths, gradients and images (SVG/PNG), not CSS.
- Module panels are third-party **SVGs** that we must not change. Realism therefore
  comes from the *surroundings*: rails, case, shadows, screws, cable rendering,
  chrome, and an optional rendering pass over panels (gloss and vignette).
- Eurorack modules are **vertical** (3U × N HP). Reason devices are horizontal 19"
  units. We map the two this way: one **rack row = one 3U "Euro case" inside a 19"
  rack frame**, with rails top and bottom, rack ears left and right, and screws on
  every module.
- Cardinal ships as a plugin (VST2/VST3/CLAP/LV2/AU) and also as standalone and wasm,
  so the UI must scale and must stay fast on software GL.
- `src/Rack` is a submodule, still being cloned when this was written. UI changes go
  through `src/override/*` and Cardinal's own files, not by editing Rack directly,
  where possible.
