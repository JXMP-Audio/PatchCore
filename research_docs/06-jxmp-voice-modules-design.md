# 06 — JXMP voice modules: VCO, VCF, VCA, ADSR

Status: approved design (2026-10-28) · Builds on [04-sided-modules-patchbay-design.md](04-sided-modules-patchbay-design.md)

## Goal

The first set of JXMP modules: a complete subtractive voice. The DSP comes from the
Fundamental modules shipped in `plugins/Fundamental` (GPL-3.0-or-later, same license as
PatchCore), extended with features the user chose. Panels, names and layout are our own.
All sockets are on the back; the front holds only controls and lights.

## Rules for every JXMP module

| Where | What | Example |
|---|---|---|
| Front panel title | Short JX code | `JX-OSC` |
| Name in PatchCore (`plugin.json` `name`, browser) | Generic function name | `VCO` |
| Brand | `JXMP Audio` | |
| Back side | Serial plate: JX code + serial number | `JX-OSC · S/N 3F2A-91C4` |
| Panel | Uniform JXMP style: dark metal, a colour stripe per module type, title on top, `JXMP AUDIO` at the bottom | |
| Sockets | All `SIDE_BACK`. Exceptions only where a spec says so (the Patchbay) | |
| Trademarks | None from third parties in names, panels or text | |

The Patchbay follows the same rules: front titles `JX-PB4`, `JX-PB8`, `JX-PB16`, browser names
`Patchbay 4/8/16`, a serial plate on the back.

## Common base

### Serial plate (`include/patchcore/SidedModuleWidget.hpp`, `src/patchcore/BackPanel.cpp`)

`SidedModuleWidget` gets a second virtual method:

```cpp
/** Text for the serial plate on the generated back. Empty: the generic sticker. */
virtual std::string serialLabel() const { return ""; }
```

`drawBackPanel` draws the serial plate with this text when it is not empty. JXMP widgets
return `"<JX code> · S/N <serial>"`. The serial comes from `jxmp::serialNumber(int64_t moduleId)`
(`plugins/JXMP/src/Serial.hpp`): `XXXX-XXXX` in upper-case hex, derived from the module id, so
it stays the same across save/load. Browser previews (no module) show `S/N ----`.

### Panel and widget base (`plugins/JXMP/src/JXMPWidget.hpp`)

- `JXMPPanel`: a widget that draws the panel in NanoVG (plate, stripe, title, footer). Text is
  drawn in code because nanosvg does not render SVG text. The panel background still comes from
  a plain SVG per width (`res/Panel<N>HP.svg`), so `panelColor()` and the blanking caps keep
  working.
- `JXMPModuleWidget : app::ModuleWidget, patchcore::SidedModuleWidget`: takes the JX code,
  stripe colour and width; `portSide()` returns `SIDE_BACK`; `serialLabel()` as above.
- Control labels on the front are drawn by the widget next to each knob.

Colours: JX-OSC orange `#ee7b1f`, JX-FLT blue `#3a8dde`, JX-AMP green `#3cb46e`,
JX-ENV yellow `#f2c230`, JX-PB grey `#8a8f98`.

### DSP sources

Code taken from `plugins/Fundamental/src/{VCO,VCF,VCA,ADSR}.cpp` is copied into
`plugins/JXMP/src/` with a header naming the original authors and the license. New features
live in small headers of pure functions (`plugins/JXMP/src/dsp/*.hpp`) that the `g++` test
runner from the Patchbay plan tests without Rack.

## Modules

All modules are polyphonic up to 16 channels: the channel count follows the main input
(V/OCT, audio input or gate), as in Fundamental.

### VCO — JX-OSC

Base (from Fundamental VCO): sine, triangle, saw and square outputs, frequency and fine knobs,
FM input with amount, pulse width with PWM input and amount, hard sync input, analog/digital
character, band-limited waveforms.

Added:

- **Octave switch** −2…+2.
- **Morph**: one knob plus CV morphs sine → triangle → saw → square into the **MIX** output.
  `jxmp::morph(sin, tri, saw, sqr, m)` with `m` in 0…3 crossfades neighbours.
- **Sub oscillator**: square at −1 or −2 octaves (switch), own **SUB** output, and a SUB
  level knob that adds it to MIX. Derived from the main phase, so it stays in tune and in sync.
- **Unison**: 1–7 voices per channel (knob) with a **DETUNE** knob (0–50 cents spread). The
  voices are summed into each output, normalised by `1/sqrt(n)`. `jxmp::unisonOffsets(n, cents)`
  returns symmetric offsets (sum 0, the outermost at ±cents).

Back sockets: V/OCT, FM, PWM, SYNC, MORPH CV in; SIN, TRI, SAW, SQR, SUB, MIX out.

### VCF — JX-FLT

Base (from Fundamental VCF): 4-stage ladder, cutoff, resonance, drive, cutoff CV with
attenuverter, resonance and drive CV, 2× oversampling.

Added:

- **Mode** switch LP / BP / HP / NOTCH and **slope** switch 12 / 24 dB, one **OUT**.
  The response is a mix of the ladder stages (`u` = input minus feedback, `s0…s3` = stages),
  `jxmp::ladderTap(mode, slope, u, s)`:

  | Mode | 12 dB | 24 dB |
  |---|---|---|
  | LP | `s1` | `s3` |
  | BP | `2·s0 − 2·s1` | `4·s1 − 8·s2 + 4·s3` |
  | HP | `u − 2·s0 + s1` | `u − 4·s0 + 6·s1 − 4·s2 + s3` |
  | NOTCH | `u − 2·s0 + 2·s1` | `u − 4·s0 + 6·s1 − 4·s2 + 2·s3` |

  The fixed LPF and HPF outputs of the original are kept too.
- **Keytrack**: V/OCT input and a 0–100 % knob: cutoff moves by `keytrack · V/OCT` octaves.
- **Envelope**: ENV input with a bipolar amount knob (±5 octaves at ±100 % for a 10 V envelope).

Back sockets: IN, V/OCT, ENV, CUTOFF CV, RES CV, DRIVE CV in; OUT, LPF, HPF out.

### VCA — JX-AMP

Base (from Fundamental VCA): two channels, each with a level knob, CV input and output;
channel 2's input is normalled from channel 1's.

Added:

- **Response** switch per channel: linear / exponential. The exponential curve is Fundamental
  VCA's law, `jxmp::vcaGain(cv01, exp)`: `cv01` for linear, `(50^x − 1) / 49` for exponential.

Back sockets: IN 1, CV 1, IN 2, CV 2 in; OUT 1, OUT 2 out.

### ADSR — JX-ENV

The envelope core is a scalar per-channel state machine `jxmp::Envelope` in
`dsp/Envelope.hpp`, written for these features and following the timing ranges and exponential
segments of Fundamental's ADSR.

- **A, D, S, R** knobs with CV inputs, gate and retrigger inputs, LED per stage.
- **Curve** knob −1…+1 bends A, D and R (0 = Fundamental's shape, −1 more linear,
  +1 more exponential). `jxmp::curve(x, shape)` maps 0…1 → 0…1 monotonically.
- **Mode** switch: ADSR / AD (ignores gate length, runs attack then decay to 0) / LOOP
  (repeats attack-decay while the gate is high, or free-running when no gate is connected).
- **INV** output (`10 V − env`) and **EOC** output: a 1 ms 10 V trigger at the end of release
  (ADSR), at the end of decay (AD) and at the end of every cycle (LOOP).

Back sockets: GATE, RETRIG, A CV, D CV, S CV, R CV in; ENV, INV, EOC out.

## Testing

Pure functions are tested with `plugins/JXMP/tests/run.sh` (added by the Patchbay plan):

- `serialNumber`: same id → same text, format `XXXX-XXXX`.
- `morph`: endpoints equal the pure shapes, `m` between shapes crossfades linearly.
- `unisonOffsets`: n = 1 → {0}; symmetric, sum 0, outermost ±cents.
- `ladderTap`: at DC with all stages equal to the input, LP and NOTCH return the input, BP and HP
  return 0, for both slopes.
- `vcaGain`: 0 → 0, 1 → 1, exponential below linear in between.
- `curve`: 0 → 0, 1 → 1, monotonic for shapes −1, 0, 1.
- `Envelope`: ADSR reaches sustain and releases; AD ignores an early gate-off; LOOP retriggers;
  EOC fires once per end.

By hand in the standalone build: a VCO → VCF → VCA voice with ADSR on both, played from MIDI-CV
via a Patchbay; polyphony with 4 voices; save and reload restores every setting.

## Out of scope

LFO, Noise, Mixer and other modules; presets; panel artwork beyond the uniform style.
