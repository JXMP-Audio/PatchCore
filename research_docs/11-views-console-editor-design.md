# 11 — Views, JX-CONSOLE and the piano-roll Editor

Status: draft for review (2026-10-29). Three sub-projects that build on each other; each gets its own
implementation plan before work starts.

## Goal

PatchCore behaves differently as a plugin and as a standalone app:

- **Plugin (in a DAW):** follows the DAW. The bottom transport only shows tempo, signature and position
  from the host (read-only, the DAW starts and stops). No Editor. The Mixer view is there only when a
  JX-CONSOLE is in the rack.
- **Standalone:** its own transport (tempo, signature, play/stop, record), an **Editor** (piano roll with
  loops, eight tracks, MIDI keyboard recording) and the **Mixer** view for the JX-CONSOLE.

## Decisions (from the questionnaires)

| Topic | Decision |
|---|---|
| Switching views | Top bar segment **Rack · Back · Editor · Mixer**, keys F1–F4. Plugin: Rack, Back, Mixer (only with a console) |
| Transport as plugin | Information only: host tempo, signature, bar.beat position, playing state |
| Transport standalone | As today (internal clock) plus REC for the Editor |
| Editor | Piano roll with loops (clips) |
| Editor tracks | 8 tracks = 8 MIDI channels |
| Editor → rack | Through Cardinal's **Host MIDI** modules (one per channel / track) |
| MIDI keyboard | Plays live into the selected track (and the rack); REC writes the notes into its loop, quantized |
| Mixer | A JXMP module **JX-CONSOLE**, at most **one per project**; the Mixer view controls it |
| Console sizes | **4, 8, 16, 32** channels; the Mixer view adapts to the size |
| Console channel | Mono input, pan, fader, mute, solo, 2 sends (pre/post), 3-band EQ (low, mid with frequency, high) |
| Console buses | Stereo master out, 2 send outs, 2 stereo returns |
| Metering | Two **analogue VU meters** (L and R) with a selector button: a chosen input channel or the stereo master |
| Second console | Cannot be added (the finder shows it as already in the project; one coming from a paste is disabled with a notice) |

## Sub-project A — Views and transport

- `patchcore::View { RACK, BACK, EDITOR, MIXER }` replaces the front/back pair; RACK/BACK keep the flip.
- Top bar segment and F1–F4; which segments exist depends on `isStandalone()` and on a console being present.
- Transport bar:
  - **Plugin:** read-only LCDs from the host time info Cardinal already has (`playing`, `bar`, `beat`,
    `beatsPerBar`, `beatType`, BPM). Play/stop/loop buttons hidden; the record button hidden.
  - **Standalone:** today's internal transport, plus REC (arms recording in the Editor) and a metronome
    toggle (exists as a button already).
- Shared song position for the Editor: bar/beat/tick from the internal transport (standalone).

## Sub-project B — JX-CONSOLE and the Mixer view

### Module

- Models `Console4`, `Console8`, `Console16`, `Console32` (browser names "Console 4/8/16/32", panel
  `JX-CONSOLE`). JXMP style, its own colour.
- All sockets on the back: N channel inputs, 2 stereo returns (A, B), stereo master out, 2 send outs.
- DSP per channel: EQ (low shelf ~100 Hz, peaking mid 200 Hz–5 kHz with gain, high shelf ~8 kHz; ±15 dB),
  fader (−∞…+6 dB), pan (constant power), mute, solo (solo-in-place: any solo mutes the non-soloed),
  send A/B (each pre- or post-fader). Master fader and master mute. Returns mixed into the master
  with their own level. Pure DSP functions tested with `g++` (EQ coefficients, pan law, solo logic,
  dB↔gain).
- The front of the module in the rack: compact (title, two small VU meters, master fader, the VU source
  selector, "open Mixer" hint); the full controls live in the Mixer view.
- Singleton: a static registry; adding a second one from the finder is blocked (shown greyed with
  "already in the project"); one created by paste/duplicate/patch load is bypassed and its panel says
  "second console – inactive".

### Mixer view

- A full-size console drawn over the rack area: one strip per channel (EQ knobs, send knobs, pan,
  mute/solo buttons, a fader with a dB scale, a small LED meter), the returns, the master section.
- Strip count follows the console size; 32 channels scroll horizontally.
- **Analogue VU meters** (L and R): needle with ballistics (300 ms integration), dB scale −20…+3, a
  selector button to meter the stereo master or one input channel (L shows the channel, R the same,
  or master L/R).
- The view edits the console module's parameters directly, so undo, patch saving and host automation
  keep working.
- Channel names (editable, saved with the module) and colours.

## Sub-project C — Editor (piano roll, standalone only)

- **Data:** 8 tracks; each track has a loop (clip) of 1–16 bars with notes (start tick, length, pitch,
  velocity), a MIDI channel (1–8 by default), mute/solo. Saved in the patch (PatchCore patch metadata).
- **Playback:** while the internal transport runs, the Editor sends note on/off for each track into the
  MIDI event stream that Cardinal hands to its modules (merged with the host/keyboard events before
  `Host MIDI` reads them). Loops repeat; a loop shorter than another just repeats sooner.
- **Recording:** the MIDI keyboard plays the selected track live; with REC armed, notes are written into
  its loop, quantized to the grid (off, 1/4, 1/8, 1/16, 1/32), overdub.
- **Piano roll UI:** keyboard on the left, grid with bars/beats, draw/erase/move/resize notes with the
  mouse, velocity lane, grid and loop-length selectors, track list with name, channel, mute/solo.
- **Rack side:** a Host MIDI module per used channel (MIDI → CV) into the JXMP voices; a template patch
  sets this up.

## Order

1. **A** — views and transport (small, needed by B and C).
2. **B** — JX-CONSOLE 4/8/16/32 + Mixer view + VU meters.
3. **C** — Editor (largest).

## Open points for the plans

- Console EQ frequencies and Q, VU meter look (classic cream face vs. dark).
- Editor: note colours per track, keyboard shortcuts, copy/paste of notes, loop length per track or
  global.
- Plugin mode: should the Mixer view also show automation of the DAW (read-only)? Default: no.
