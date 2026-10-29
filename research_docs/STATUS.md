# PatchCore — status and next steps

Handoff notes, updated at the end of every work session (rule: keep this file current before a session
or weekly limit ends). Newest first.

## 2026-10-29

### Done (on `main`, pushed, collection bumped)

- Sided modules (basic modules front, JXMP back), Patchbay 4/8/16, rack flip (Tab).
- JXMP modules in the 80s style (design guide [10](10-jxmp-design-guide.md)): OSC, FLT, AMP, ENV, LFO, NSE,
  MIX, SEQ, DLY, QNT, RND, SLW, ATT, Patchbay. Tooltips with descriptions on every jack.
- Back side: title centred, serial plate centred under the stripe on every module.
- Quick search (right-click on the rack / Enter) with `@brand` and `#purpose` filters.
- Mouse wheel turns knobs (fine by default); stepped knobs move one step per notch.
- Mouse on modules: **Shift+click** selects (Delete removes), **Shift+drag** moves, **Shift+right-click**
  opens the module menu; without Shift the controls always work.
- Launcher `run-patchcore.sh` (in this repo; on the host it enters the `CLAUDE` distrobox):
  `./run-patchcore.sh demo`.
- Plan for views / console / editor: [11](11-views-console-editor-design.md).

### In progress: JX-CONSOLE (branch `console`, pushed, NOT merged)

Built and the tests pass, only partly checked on screen.

- `plugins/JXMP/src/dsp/Console.hpp` + `tests/test_console.cpp`: fader law (0.75 = 0 dB, top +6 dB),
  constant-power pan, mute/solo, RBJ EQ (low shelf 100 Hz, mid peak 200 Hz–5 kHz, high shelf 8 kHz, ±15 dB),
  VU ballistics (0 VU = 5 V sine) and needle mapping. Tests pass.
- `plugins/JXMP/src/Console.cpp`: models Console4/8/16/32 (18 HP), all sockets on the back
  (inputs in rows of 8, then RET A L/R, RET B L/R, SEND A/B, MAIN L/R). Front: two cream VU meters,
  meter source knob + readout, master mute, master fader. Second console: silent, red notice on the panel.
  `ConsoleView`: the Mixer view content (strips: HIGH/MID/FREQ/LOW, SEND A/B, PAN, M/S, fader, LED
  meter; master: VU meters, meter source, RET A/B, send PRE/POST toggles, mute, master fader), zoomed to
  the view height, strips scroll horizontally.
- Core: `include/patchcore/Mixer.hpp` (MixerProvider, showMixer, singleton registry),
  `src/patchcore/MixerView.cpp` (overlay over the rack area; Esc/Tab close it), Scene + top bar
  "Mixer" segment (enabled when a console is in the rack), the quick search refuses a second console
  ("already in the project"). `ModuleWidget`: a console keeps plain clicks, so a double-click opens the
  Mixer; a drag moves a module only after Shift+press.
- JX-MIX stays as it is (user: the console is extra, JX-MIX is enough in places).

### Note: building after switching branches

`main` has no console in `plugins/JXMP/plugin.json`; a binary built on `console` and then rebuilt on
`main` can keep stale objects and crash at start ("Plugin defines module Console4 … not defined in
manifest"). After switching branches run `touch plugins/plugins.cpp` (or `make clean` in plugins) before
`make`. Until `console` is merged, build and run from the `console` branch.

### Next

1. Checked on screen (evening 2026-10-29): front, back and the Mixer view (opened from the top bar)
   render without text collisions, VU needles and the LED meters move. Still to check: double-click on
   the module, fader drag and wheel, Esc/Tab, a second console; in the test the channel 2 LED meter
   (square wave from JX-OSC) looked dark — check. Then merge `console` into `main`, push, bump.
2. Add JX-CONSOLE to the design guide (colour indigo `#5b6ee1`), plugins/JXMP/README.md, the module
   list in the README, and a demo patch that uses it.
3. Open points from [11](11-views-console-editor-design.md): channel names/colours in the Mixer view,
   EQ Q, plugin mode (Mixer stays, Editor hidden).
4. Sub-project A: views (Rack · Back · Editor · Mixer, F1–F4) and the transport — read-only host sync
   as a plugin, own transport + REC standalone.
5. Sub-project C: piano-roll Editor (8 tracks = MIDI channels 1–8, loops, MIDI keyboard REC) through
   Host MIDI modules.

### Later / parked

- JX808 panel polish (colliding lines, missing legends) — see the collection memory.
- Realistic cables: cancelled, branch `realistic-cables` kept locally only.
