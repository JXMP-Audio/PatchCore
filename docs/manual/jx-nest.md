# JX-NEST — Plugin host

24 HP · JXMP Audio · [all modules](README.md)

One VST3, CLAP or LV2 plugin inside the patch: an effect (a compressor, an EQ, a reverb) or an instrument (JX808, a synth). Audio goes through it from cables, its own window opens with OPEN UI, notes come from the DAW or from GATE / V/OCT cables, and up to 8 of its parameters move with CV. The patch keeps the plugin with all its settings.

Not in the browser-based PatchCore (WebAssembly).

## Front panel

| Control | What it does |
|---|---|
| Display | The plugin's name, tempo and ▶ / ■, IN / OUT meters, and 4 of the 8 CV slots: name of the parameter, a bar (the tick = the value set in the plugin, the filled part = where CV takes it now, "CV" = a cable drives the slot) and the plugin's own value text. ▲ ▼ choose a slot. The display is worked with the buttons only (the mouse wheel over it scrolls the rack). Drop a plugin file (.vst3, .clap, .lv2) on the module to load it, or a folder to add it to the search. |
| ▲ ▼ ≡ ✓ | The menu, as in JX-SMPL: ≡ opens it, ▲ ▼ move, ✓ runs, ≡ goes back. ✓ on a slot opens the slot menu. |
| OPEN UI | Opens the plugin's own window (again: closes it). |
| BYPASS | The plugin is skipped: each output plays the input that feeds the same plugin port (L to L, R to R), delayed as much as the plugin (64 samples), so switching blends over 10 ms without a click. Notes held while bypassed are ended when the plugin comes back. |
| CV DEPTH 1–8 | How much CV moves the parameter of slot n: ±100 %. At 100 %, +10 V moves it over the whole range (or the slot's RANGE). Without a cable nothing moves. |

## Sockets (back)

12 inputs and 12 outputs with no fixed job: each one gets a role in **Menu › I/O › TABLE**. The label under a socket shows its role; ✗ = the loaded plugin has no such port (the socket is silent).

| Input role | What it does |
|---|---|
| PLUGIN AUDIO IN | Audio into one of the plugin's inputs, by its name (MAIN L / R, SIDECHAIN …). ±5 V = full scale. |
| CV SLOT 1–8 | CV for that slot's parameter. |
| GATE, V/OCT, VELOCITY | A note to the plugin: GATE starts and ends it, V/OCT is its pitch (0 V = C-4, MIDI 60), VELOCITY 0–10 V (unpatched 100). |
| GATE » NOTE | A gate plays one fixed note (36 = a kick on a drum machine): one input per drum. |

| Output role | What it does |
|---|---|
| PLUGIN AUDIO OUT | One of the plugin's outputs, by its name (MAIN L / R, the separate outputs of a drum machine). |

A new module: IN 1–2 → the plugin's inputs 1–2, OUT 1–2 ← its outputs 1–2. Roles stay when you load another plugin.

## The menu

| Folder | Item | What it does |
|---|---|---|
| PLUGIN | LOAD | The list of found plugins: ALL / INSTRUMENTS / EFFECTS (▲ on the first row changes the tab), ✓ loads. |
| | RESCAN | Searches the plugin folders again (after you installed something). |
| | ADD FOLDER | Type the path of a folder with plugins; it is searched from now on, by every JX-NEST. |
| | REMOVE PLUGIN | Takes the plugin out. |
| I/O | TABLE | The roles of the 24 sockets: ▲ ▼ choose, ✓ opens the choices from what the plugin offers. |
| MIDI | ALL, 1–16 | Which MIDI channel from PatchCore's MIDI input (the DAW, a keyboard) goes to the plugin. |
| CV SLOTS | CLEAR ALL SLOTS | Empties the 8 slots. |

Slot menu (✓ on a slot): **LEARN** (opens the plugin window; move a knob there and the slot takes it), **CHOOSE PARAMETER** (a list), **RANGE MIN / MAX** (the part of the parameter's range CV covers), **REMOVE**.

## Where plugins are found

Only in the standard folders and the ones you add (no searching of whole disks, no reading of DAW settings):

- Linux: VST3 `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`; CLAP `~/.clap`, `/usr/lib/clap`; LV2 `~/.lv2`, `/usr/lib/lv2`, `/usr/local/lib/lv2`; VST2 `~/.vst`, `/usr/lib/vst`.
- Windows: VST3 `C:\Program Files\Common Files\VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`; CLAP in the same two places (`…\CLAP`).

Every plugin is tested in a separate program (carla-discovery, shipped with PatchCore), so a broken plugin cannot crash PatchCore while searching. The list is remembered; RESCAN refreshes it.

## Good to know

- The plugin runs inside PatchCore: a plugin that crashes takes PatchCore (and the DAW) with it.
- JX-NEST adds 64 samples of delay (about 1.3 ms at 48 kHz): PatchCore works one sample at a time, plugins in blocks.
- A patch with a plugin this computer does not have shows PLUGIN NOT INSTALLED HERE; its settings are kept in the patch and come back by themselves when you install it and RESCAN (or ADD FOLDER).
- Every change (load, role, slot, LEARN) can be undone. Undoing a role, slot or MIDI channel change leaves the plugin playing (its window stays open).

## Examples

- JX808 from the rack: JX-CLK ×4 → IN 3 with the role GATE » NOTE 36 (kick), another output → IN 4 GATE » NOTE 38 (snare); OUT 1–2 → the mixer.
- A compressor on a drum bus: the mix → IN 1–2, OUT 1–2 → the output; an LFO → IN 5 as CV SLOT 1, slot 1 LEARNed to the threshold.
