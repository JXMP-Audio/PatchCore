# JXMP modules

Built-in modules by JXMP Audio for PatchCore. Their sockets are on the back of the rack
(press `Tab`); basic modules connect to them through a Patchbay. The look is described in
[research_docs/10-jxmp-design-guide.md](../../research_docs/10-jxmp-design-guide.md).

| Panel | Browser name | Width | What it does |
|---|---|---|---|
| JX-OSC | VCO | 18 HP | Oscillator: sine/triangle/saw/square, FM (lin/exp), PWM, hard/soft sync, octave switch, morph between the shapes into MIX, sub oscillator −1/−2 octaves (own SUB output, mixed into MIX by SUB LEVEL; at 0 it is off), unison up to 7 voices with detune. Polyphonic. |
| JX-FLT | VCF | 14 HP | Ladder filter: lowpass/bandpass/highpass/notch at 12 or 24 dB, resonance, drive, keytracking, envelope input with bipolar amount, CV for cutoff/resonance/drive; OUT plus fixed LPF/HPF. Polyphonic. |
| JX-LFO | LFO | 10 HP | Low-frequency oscillator: sine/triangle/saw/square, rate 0.004 Hz–1 kHz (follows an external clock), rate CV, pulse width and PWM, unipolar/bipolar, invert, reset. Polyphonic. |
| JX-AMP | VCA | 8 HP | Two VCAs with level knobs, linear or exponential response; channel 2 is normalled to channel 1's input. Polyphonic. |
| JX-ENV | ADSR | 14 HP | Envelope with CV for each stage, curve (linear to exponential), ADSR / AD / loop modes, push button, ENV, inverted and end-of-cycle outputs. Polyphonic. |
| JX-PB4 / PB8 / PB16 | Patchbay 4 / 8 / 16 | 4 / 4 / 8 HP | Passes channels between the front and the back. Plug something into front jack *n* and it comes out of back jack *n*, or the other way round. The first cable decides the direction; the LEDs show it (green → back, yellow → front). |

DSP of JX-OSC, JX-FLT and the JX-AMP gain law is based on VCV Fundamental (GPL-3.0-or-later).
Fonts: Barlow and Barlow Condensed (SIL Open Font License 1.1, see `res/fonts/`).

Tests for pure logic: `plugins/JXMP/tests/run.sh`.
