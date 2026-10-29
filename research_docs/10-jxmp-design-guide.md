# 10 — JXMP design guide

Status: living document (2026-10-29). Every JXMP module follows it. The reference implementation is
JX-OSC (`plugins/JXMP/src/Osc.cpp`); the style code is `plugins/JXMP/src/JXMPStyle.hpp` and
`plugins/JXMP/src/JXMPWidget.hpp`.

## Direction

80s Japanese synthesizer panels, clean and professional, with a little real-world wear. Inspired by
a reference photo the user picked: dark matte panels, bold italic titles, scales around knobs,
metal toggles with LEDs, outputs on light fields. No third-party names or logos.

## Naming

| Where | What | Example |
|---|---|---|
| Front title | JX code, bold italic | `JX-OSC` |
| Browser / `plugin.json` name | generic function | `VCO` |
| Brand | `JXMP Audio` | |
| Back | serial plate `S/N XXXX-XXXX` (from the module id) | `S/N 92FD-5B26` |

## Panel

- Matte anthracite `#2a2b2f`, darker top and bottom bands `#1e1f22`.
- Under the title a **double stripe** in the module colour (2.4 mm + 0.6 mm).
- **Sections**: a thin horizontal divider line (`#5c5f66`) with the section title in the module colour
  cut into it. No boxes.
- Four black screws (two on narrow modules), `JXMP AUDIO` in the bottom band.
- **Wear (patina)**, different on every unit (seeded by its serial): faint scratches, rub marks, worn
  lighter edges and **chipped paint along the edges and corners** showing bare aluminium.
- Width: give controls room. If labels get close, widen the module (JX-OSC went from 14 to 18 HP).

## Module colours

| Module | Colour |
|---|---|
| JX-OSC (VCO) | orange `#ee7b1f` |
| JX-FLT (VCF) | blue `#3a8dde` |
| JX-AMP (VCA) | green `#3cb46e` |
| JX-ENV (ADSR) | yellow `#f2c230` |
| JX-PB (Patchbay) | grey `#8a8f98` |
| JX-LFO (LFO) | violet `#9a6bd6` |
| JX-NSE (Noise) | white-grey `#c9cbd0` |
| JX-MIX (Mixer) | red `#d8443a` |
| JX-SEQ (SEQ) | cyan `#2fb8c4` |
| JX-DLY (Delay) | teal `#4ac0a0` |
| JX-QNT (Quantizer) | magenta `#c94f9c` |
| JX-RND (Random) | amber `#e0a43a` |
| JX-SLW (Slew) | olive `#8fae3a` |
| JX-ATT (Attenuverter) | slate `#6f86a8` |
| future modules | a new colour each, not reused |

## Knob caps — what each colour means

Knobs: black ridged body, **coloured semi-matt cap with a soft gloss** and machining rings, pointer line
(dark on light caps, white on dark caps). The cap colour tells what the knob does, the same in every module:

| Cap | Meaning | Examples |
|---|---|---|
| **Module colour** | the module's main sound parameter(s) | FREQ, MORPH (OSC); CUTOFF (FLT); LEVEL (AMP) |
| **White** | levels, times, amounts of sound | PW, DETUNE, SUB LEVEL, A/D/S/R |
| **Grey** | CV / modulation amounts (attenuverters) | FM, PWM, MORPH CV, CV AMOUNT |
| **Black** | selectors and stepped knobs | OCTAVE, VOICES, MODE |

Sizes: 15 mm main knob, 9–10 mm standard, 7 mm for CV amounts.

## Scales and legends

- A scale around every knob, **in the parameter's real values** (`knob(..., min, max, marks)`).
- Numbers **only at both ends and the centre** (e.g. FREQ `16 · 262 · 4k`, PW `1 · 50 · 99`,
  bipolar `− · 0 · +`); the other marks are plain ticks. No units on the panel (the tooltip has them).
- Knob names **above** the knob, all names of a row on one line.
- Icons instead of words where it is clearer: waveforms around MORPH, LP/BP/HP/N for filter modes,
  an envelope shape.
- Fonts (SIL OFL, bundled): **Barlow Bold Italic** for titles, **Barlow SemiBold** for names and section
  titles, **Barlow Medium** for scale numbers.

## Switches

Chrome bat-handle toggles: hex nut with facets on a washer, threaded bushing, tapered lever with a
rounded tip and a highlight, a shadow on the panel. The upward lever is drawn long, the downward one
foreshortened. An **LED next to every position** (green when selected). Position legends above and
below the switch.

## Back side

Same anthracite with patina, the title in bold italic centred (as on the front), the colour stripe, a metal
serial plate centred under the stripe on every module. Jack names under each jack in Barlow; **outputs sit on a light field** (`#c9cbcf`, dark text),
inputs have light text. All JXMP sockets are on the back (Patchbay front jacks are the exception).

## Plan (as of 2026-10-29)

- [x] Sided modules, JXMP plugin, Patchbay 4/8/16
- [x] Voice set: JX-AMP (8 HP), JX-ENV (14 HP), JX-OSC (18 HP), JX-FLT (14 HP), all in the JXMP style
- [x] Patchbay in the JXMP style (front numbers/LEDs, styled back with only back jacks; skip zero-size SmartPort halves)
- [x] Voice check: VCO → VCF → VCA with ADSR through Patchbays, save/reload, plugins/JXMP/README.md
- [ ] Next modules, modelled on the basic Fundamental set (DSP may be adapted from plugins/Fundamental, GPL-3, with attribution), one at a time, each: spec section here → pure-logic tests → module → screenshot check → merge + bump:
  1. [x] JX-LFO (LFO, 10 HP) — colour violet `#9a6bd6`: sin/tri/saw/sqr outputs, rate (Hz scale), fine/offset/invert, PW, reset/sync, poly
  2. [x] JX-NSE (Noise, 6 HP) — colour white-grey `#c9cbd0`: white/pink/red/violet/blue/grey/black outputs, level
  3. [x] JX-MIX (Mixer, 14 HP) — colour red `#d8443a`: 4 channels level + mute, master, poly sum
  4. [x] JX-SEQ (8-step sequencer, 18 HP) — colour cyan `#2fb8c4`: 8 steps of CV + gate toggles, clock/reset/run, length
  5. [x] JX-DLY (Delay, 10 HP) — colour teal `#4ac0a0`: time, feedback, tone, mix, CV
- [x] Second set (2026-10-29), same procedure:
  1. [x] JX-QNT (Quantizer, 8 HP) — magenta `#c94f9c`: ten scales on any root, root CV, octave shift, trigger on note change, note LEDs
  2. [x] JX-RND (Random, 8 HP) — colour amber `#e0a43a`: sample & hold of noise or an input, smooth random, internal rate or clock, unipolar/bipolar
  3. [x] JX-SLW (Slew / glide, 8 HP) — colour olive `#8fae3a`: rise and fall times, linear/exponential, for portamento on V/OCT
  4. [x] JX-ATT (Attenuverters, 10 HP) — colour slate `#6f86a8`: four attenuverters with offsets, chained (an unpatched input is a constant)
- [ ] Next (2026-10-29): views (Rack · Back · Editor · Mixer) and plugin/standalone transport, JX-CONSOLE 4/8/16/32 with the Mixer view and VU meters, piano-roll Editor — design draft [11](11-views-console-editor-design.md)
- Cancelled: realistic cables (07/08) — the original cables stay
