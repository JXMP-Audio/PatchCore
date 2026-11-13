# JX-SMPL — Sampler

24 HP · JXMP Audio · [all modules](README.md)

16 sample slots in the spirit of the FastTracker 2 sample editor. Every slot holds one WAV from your computer: your own drums, a guitar note, a vocal, a pad. GATE n plays slot n, V/OCT n sets its note. Without a V/OCT cable a slot plays at its original pitch. The samples are saved inside the patch, so the patch works on another computer and in a DAW project.

<img src="img/jx-smpl-front.png" alt="JX-SMPL front" height="380"> <img src="img/jx-smpl-back.png" alt="JX-SMPL back (sockets)" height="380">

## Front panel

| Control | What it does |
|---|---|
| Display | The selected slot: number, name, root note ("C-4") and fine tune, mode and loop, file rate and length. The waveform shows START / END (lines with S / E handles below; what they cut off is darkened) and the loop (flags at the top, shaded area; dimmed while the loop does not apply); drag a handle in the lower half or a flag in the upper half to move it. The row 1–16 shows every slot: filled = has a sample, bright = playing, white frame = selected; click one to select it. Drop a WAV file on the module to load it into the selected slot. |
| CH | Selects the slot 1–16 that the display and the knobs below show. |
| LOAD | Opens a file dialog: the WAV goes into the selected slot. |
| PLAY | Plays the selected slot while held. |
| MODE | ONE-SHOT: a gate plays the sample to the end (drums). GATE: plays while the gate is high, then fades in 10 ms; a loop holds the tone. |
| START, END | The part of the sample that plays. |
| LOOP | OFF, FWD (from LOOP END back to LOOP ST), PING (back and forth). Only in GATE mode: turning LOOP ST / LOOP END otherwise shows "LOOP: GATE + FWD/PING". |
| LOOP ST, LOOP END | The loop. It is at least 16 samples long and always inside START…END (pushed against S or E it stops there and the display says so). |
| ROOT | The note the sample sounds at, FastTracker style (C-4 = 0 V on V/OCT). A guitar E recorded at E-2: set ROOT to E-2, and V/OCT plays it in tune. |
| FINE | Fine tune, ±100 cents. |
| LEVEL | Level of the slot, 100 % = the sample as recorded. |
| PAN | Position of the slot in the MIX. For a stereo sample it works as a balance. |

The knobs below CH always belong to the selected slot: turn CH and they jump to that slot's values.

## Sockets (back, `Tab` turns the rack)

| Socket | What it does |
|---|---|
| GATE 1–16 | A rising edge plays the slot from START (a new hit fades the old sound out in 2 ms). |
| V/OCT 1–16 | The note (0 V = C-4). Without a cable the slot plays at its original pitch. |
| OUT 1–16 | The slot alone, mono, about ±5 V for a full-scale sample. Patching it takes the slot out of the MIX. |
| MIX L, R | All slots whose own OUT is not patched, panned. |

## Loading samples

- WAV files: 8, 16, 24 or 32 bit or float, mono or stereo (more channels: the first two), any sample rate.
- Up to 60 s per slot; a longer file is shortened and the display says CUT TO 60 S.
- Right click the module: Load… and Clear slot for the selected slot. Initialize empties all slots.

## How to use it

- Your own drum machine: JX-EUC GATE 1–4 → GATE 1–4 of a kick, snare, hat and clap; MIX → the console.
- A melody from one note: a guitar note in slot 7 in GATE mode with a loop, JX-SEQ CV → V/OCT 7, GATE → GATE 7.
- One drum through an effect: patch its OUT to JX-DLY; it leaves the MIX, the rest stays.
