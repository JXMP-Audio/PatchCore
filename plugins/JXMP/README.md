# JXMP modules

Built-in modules by JXMP Audio for PatchCore. Their sockets are on the back of the rack
(press `Tab`); basic modules connect to them through a Patchbay. The look is described in
[research_docs/10-jxmp-design-guide.md](../../research_docs/10-jxmp-design-guide.md).

| Panel | Browser name | Width | What it does |
|---|---|---|---|
| JX-OSC | VCO | 18 HP | Oscillator: sine/triangle/saw/square, FM (lin/exp), PWM, hard/soft sync, octave switch, morph between the shapes into MIX, sub oscillator −1/−2 octaves (own SUB output, mixed into MIX by SUB LEVEL; at 0 it is off), unison up to 7 voices with detune. Polyphonic. |
| JX-FLT | VCF | 14 HP | Ladder filter: lowpass/bandpass/highpass/notch at 12 or 24 dB, resonance, drive, keytracking, envelope input with bipolar amount, CV for cutoff/resonance/drive; OUT plus fixed LPF/HPF. Polyphonic. |
| JX-LFO | LFO | 10 HP | Low-frequency oscillator: sine/triangle/saw/square, rate 0.004 Hz–1 kHz (follows an external clock), rate CV, pulse width and PWM, unipolar/bipolar, invert, reset. Polyphonic. |
| JX-NSE | Noise | 6 HP | Noise in seven colours (white, pink, red, violet, blue, gray, black) with a level knob and level CV. |
| JX-MIX | Mixer | 14 HP | Four channels with level, CV (0–10 V) and a mute button each, master level up to 200 %, level LEDs. Polyphonic. |
| JX-SEQ | SEQ | 18 HP | 8-step CV (0–10 V) and gate sequencer: gate button per step, internal tempo 30–300 BPM or external clock, length 1–8, run (button or gate input), reset. |
| JX-DLY | Delay | 10 HP | Mono delay 1 ms–10 s with feedback, tone (dark ↔ thin) in the repeats, dry/wet mix, CV for all four, OUT and WET outputs; turning TIME glides like tape. |
| JX-QNT | Quantizer | 8 HP | Quantizes V/oct to ten scales (chromatic, major, minor, modes, pentatonics, blues, whole tone) on any root (knob or 1 V/oct CV), octave shift, trigger on every note change, note LEDs on a little keyboard. Polyphonic. |
| JX-RND | Random | 8 HP | Random voltages: sample & hold of noise or of the IN socket, a smooth output gliding between values, internal rate 0.03–256 Hz with CV or a clock input, level, unipolar/bipolar, trigger out. |
| JX-SLW | Slew | 8 HP | Slew limiter / glide: separate rise and fall times (0 or 1 ms–10 s) with CV, linear or exponential, rise/fall LEDs. Polyphonic. |
| JX-ATT | Attenuverter | 10 HP | Four attenuverters (gain ±2×) with offsets (±10 V); an unpatched input takes the row above, the first a 10 V constant, so empty rows are manual voltage sources. Polarity LEDs. Polyphonic. |
| JX-AMP | VCA | 8 HP | Two VCAs with level knobs, linear or exponential response; channel 2 is normalled to channel 1's input. Polyphonic. |
| JX-ENV | ADSR | 14 HP | Envelope with CV for each stage, curve (linear to exponential), ADSR / AD / loop modes, push button, ENV, inverted and end-of-cycle outputs. Polyphonic. |
| JX-CONSOLE | Console 4 / 8 / 16 / 32 | 18 HP | The project's main mixer (one per project; a second one stays silent with a notice). Per channel: 3-band EQ (low shelf 100 Hz, mid 200 Hz–5 kHz, high shelf 8 kHz, ±15 dB), sends A/B (pre/post), constant-power pan, mute/solo, fader to +6 dB, peak LED meter in dBFS. Two stereo returns, stereo master, two send outputs, all sockets on the back. Front: two VU meters (IEC ballistics; 0 VU = −9, −14 or −18 dBFS in the module menu), meter source, master mute and fader. Double-click (or F4) opens the Mixer view with all controls. |
| JX-SCP | Scope | 24 HP | Two-channel oscilloscope with a square green CRT, behaving as a digital scope: trigger point in the centre (level, rise/fall, source CH1/CH2/EXT, AUTO/NORM, dashed level line), whole pictures, X-Y mode, TIME/DIV 0.1 ms-1 s redrawn at once. |
| JX-CLK | Clock | 12 HP | Master clock: CLOCK IN (24/4/1 PPQN, e.g. Host MIDI), else the transport, else its own tempo with RUN; x4, x2, beat, /2, /4, /8, /16 with swing, RESET, RUN; BPM window. |
| JX-EUC | Euclid | 14 HP | Four Euclidean rhythm tracks (steps, hits, rotate), gate and accent outputs, pattern display. |
| JX-DRM | Drums | 18 HP | Kick, snare, hi-hat (closed/open, choke) and clap with tune, decay, tone, trigger and accent; outputs per voice and a mix. |
| JX-REV | Reverb | 12 HP | Stereo plate / hall / room reverb: size, decay 0.2-12 s, predelay, tone, mix, CV. |
| JX-CHR | Chorus | 10 HP | Stereo chorus and ensemble: rate, depth, mix, CV. |
| JX-FLD | Folder | 6 HP | Wavefolder: fold up to 10x, symmetry, fold CV amount. Polyphonic. |
| JX-RNG | Ring | 6 HP | Ring modulator A x B with dry/wet. Polyphonic. |
| JX-LGC | Logic | 8 HP | AND, OR, XOR, NOT A of two gates, and a comparator with threshold. Polyphonic. |
| JX-SW | Switch | 8 HP | Sequential switch 4 > 1 and 1 > 4, length 2-4, clock and reset. |
| JX-PLY | Poly | 10 HP | Poly split (1 > 8), merge (8 > 1), voice count, mult 1 > 4. |
| JX-PB4 / PB8 / PB16 | Patchbay 4 / 8 / 16 | 4 / 4 / 8 HP | Passes channels between the front and the back. Plug something into front jack *n* and it comes out of back jack *n*, or the other way round. The first cable decides the direction; the LEDs show it (green → back, yellow → front). |

DSP of JX-OSC, JX-FLT, JX-LFO, JX-NSE and the JX-AMP gain law is based on VCV Fundamental (GPL-3.0-or-later).
Fonts: Barlow and Barlow Condensed (SIL Open Font License 1.1, see `res/fonts/`).

Tests for pure logic: `plugins/JXMP/tests/run.sh`.
