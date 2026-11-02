# 14 — JXMP modules, third set: scope, clock, rhythm, drums, effects, utilities

Status: approved design (2026-11-01). Each module gets its own short section in the plan before work starts;
order below. Rules for all modules: [10 — design guide](10-jxmp-design-guide.md) (JXMP style, sockets on the
back, legends, wear, serial plate, naming `JX-xxx` on the panel and a plain browser name), and the per-module
procedure: spec section → pure-logic tests → module → screenshot check → merge + bump. DSP may be adapted from
plugins/Fundamental (GPL-3.0-or-later) with attribution.

## Decisions (questionnaires 2026-11-01)

| Topic | Decision |
|---|---|
| Scope | JX-SCP: 2 channels + trigger, X–Y mode, a subtle phosphor afterglow; **no** intensity/focus knobs, **no** pass-through outputs |
| Scope look | **Round green CRT**, styled after an old Czechoslovak lab oscilloscope — no brand name or logo on the panel |
| Sound modules | JX-REV (reverb), JX-FLD + JX-RNG, JX-CHR (chorus/ensemble), JX-DRM (drums) |
| Utilities / rhythm | JX-CLK (clock), JX-EUC (Euclidean rhythms), JX-LGC + JX-SW (logic, switch), JX-PLY (poly utilities) |
| Drums | **One module** JX-DRM with four voices |
| Folder / ring mod | **Two modules** JX-FLD and JX-RNG |
| Reverb | Plate / hall / room, no freeze or shimmer |
| Clock | Standalone: follows the PatchCore transport. A CLOCK IN socket (on the back, like every JXMP socket) for the pulses of Cardinal's Host MIDI module. (A second CLOCK IN on the front was tried and removed, 2026-11-02.) |

## Modules

### JX-SCP — Scope (24 HP), browser "Scope"

- Look: a round CRT with a curved glass highlight and a bezel ring, a graticule on the circle (8 × 8 divisions
  clipped to the round screen), grey sheet metal with screws, black bakelite knobs. Green phosphor trace (CH1)
  and amber trace (CH2). A subtle afterglow: the trace fades over ~150 ms (fixed, no knob).
- Controls: TIME/DIV (0.1 ms … 1 s), per channel VOLTS/DIV (0.1 … 5 V) and POSITION; TRIG LEVEL, TRIG SLOPE
  (rising/falling), TRIG SOURCE (CH1 / CH2 / EXT); MODE switch Y–T / X–Y (X–Y: CH1 horizontal, CH2 vertical).
- Sockets (back): IN 1, IN 2, TRIG IN. No outputs.
- Polyphonic inputs: the first channel is drawn (a later option may draw all voices).
- DSP/logic to test: trigger detection with hysteresis, buffer capture per sweep, time/volt scaling.

### JX-CLK — Clock

- Tempo source, in this order: CLOCK IN when patched (24 PPQN pulses, e.g. from Host MIDI's clock output;
  tempo measured from the pulse spacing), otherwise the PatchCore transport (standalone: internal transport;
  plugin: host tempo/play state through Cardinal's context), otherwise its own TEMPO knob when the transport
  is stopped and RUN is on.
- CLOCK IN is on the back; PPQN (24 / 4 / 1) on the front.
- Outputs (back): ×2, 1/4 (beat), /2, /4, /8, /16, RESET (on start), RUN (gate). SWING knob (applies to the
  ×2 and 1/16-style even subdivisions).
- Logic to test: PPQN tempo measurement, divider/multiplier phase, swing timing, reset on start.

### JX-EUC — Euclidean rhythms

- Four tracks; per track STEPS (1–16), HITS (0–steps), ROTATE, and a trigger output; an accent output for
  the first hit. CLOCK and RESET inputs (normalled to JX-CLK through cables, nothing hidden).
- Logic to test: Bjorklund distribution, rotation, reset.

### JX-DRM — Drums (one module, ~24 HP)

- Four voices: KICK (pitch sweep + body), SNARE (tone + noise), HAT (closed/open, choke), CLAP (bursts +
  tail). Each: TRIG and ACCENT inputs, TUNE, DECAY, TONE/COLOUR; individual outputs plus a MIX output.
- Logic to test: envelope shapes, hat choke, accent gain.

### JX-REV — Reverb

- Algorithm switch PLATE / HALL / ROOM; SIZE, DECAY, PREDELAY, TONE, MIX; stereo in (mono normalled) and
  stereo out; CV for MIX and DECAY. Meant for a JX-CONSOLE send/return.
- DSP: a feedback delay network or Dattorro plate; test decay time vs setting and stability.

### JX-CHR — Chorus / ensemble

- Mode CHORUS / ENSEMBLE, RATE, DEPTH, MIX; mono or stereo in, stereo out.

### JX-FLD — Wavefolder

- FOLD amount, SYMMETRY, CV for both, in/out. Logic to test: folding transfer function, bounded output.

### JX-RNG — Ring modulator

- Two inputs (carrier, modulator), MIX dry/wet, output. Logic to test: product, mix law.

### JX-LGC — Logic

- AND, OR, XOR, NOT outputs from two gate inputs; a comparator with THRESHOLD (input above threshold → gate).

### JX-SW — Sequential switch

- 4→1 and 1→4, stepped by a trigger input, with reset and a LENGTH (2–4) setting.

### JX-PLY — Poly utilities

- SPLIT (poly → 8 mono), MERGE (8 mono → poly), voice COUNT display/output, a 1→4 MULT.

## Colours (stripe; checked on screen when each module is made)

| Module | Colour |
|---|---|
| JX-SCP | phosphor mint `#5ff0a0` |
| JX-CLK | coral `#ff7f6a` |
| JX-EUC | rose `#e05a8a` |
| JX-DRM | brick `#b5462e` |
| JX-REV | ice blue `#8fc8f0` |
| JX-CHR | lavender `#b9a2ee` |
| JX-FLD | lime `#b6e04a` |
| JX-RNG | copper `#c07a3e` |
| JX-LGC | steel `#9aa6b4` |
| JX-SW | sand `#d6c08a` |
| JX-PLY | navy `#3f5fa8` |

## Order

1. JX-SCP (most wanted, and it helps tune everything after it).
2. JX-CLK, JX-EUC (the rhythm base).
3. JX-DRM.
4. JX-REV, JX-CHR.
5. The small ones: JX-FLD, JX-RNG, JX-LGC, JX-SW, JX-PLY.

The queue around this set: finish branch `standalone-audio` (full build, VST3 check, merge) and the
piano-roll Editor ([11](11-views-console-editor-design.md), sub-project C) — the user decides the order.
