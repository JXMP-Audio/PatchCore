# PatchCore — status and next steps

Handoff notes, updated at the end of every work session (rule: keep this file current before a session
or weekly limit ends). Newest first.

## 2026-11-01

### Done (on `main`, pushed, collection bumped)

- JX-CONSOLE checked on screen and merged into `main`: double-click opens the Mixer, fader drag and wheel
  (verified in the saved patch), Esc back to the rack, a second console shows the red INACTIVE notice. The
  "dark channel 2 LED meter" was the old test patch, not a bug.
- Wear (patina) is clipped to the panel: chips and scratches no longer spill onto neighbouring modules.
- The Classic (plain Cardinal) look is removed: no View menu toggle, an old `"theme": "classic"` is ignored.
  `PATCHCORE_DEV_THEME` is gone.
- Meters read real levels: console VU with IEC 60268-17 ballistics and a selectable 0 VU reference (module
  menu: −9 dBFS Rack level / −14 / −18 dBFS EBU R68, printed on the face); channel LED meters are peak dBFS
  with hold; the transport MASTER meter has a dBFS scale, hold and CLIP, peaks are collected between frames;
  meters are left out when the window is too narrow instead of overlapping the time display.
- `run-patchcore.sh`: `--update`, `--uninstall`, `patchcore` command via `--install`.
- `PATCHCORE_DEV_MIXER=1` opens the Mixer view (no mouse automation needed).

### Not verified on screen

- The new transport MASTER meter in a wide window (the capture window stayed 914 px). Check it running.

### Testing note

- Captures: the user's desktop is Hyprland (Lua config, 0.56): never click with xdotool on the visible
  screen. Workspace 3 is free for test windows, but Hyprland does not render hidden workspaces, so a capture
  there only runs while the user looks at workspace 3. No Xvfb in the container; do not install system packages.
- A cable between a back port and a front port (only from a hand-edited patch) is drawn on the front by
  design (doc 04): route JXMP outputs to Host Audio through JX-PB in test patches.

### Next — to plan together with the user

1. Standalone audio device settings: today the app is a JACK client (PipeWire picks the device, DPF offers
   only buffer size, and only in native mode). Device / sample rate selection means the DPF RtAudio bridge
   (`dpf/distrho/src/jackbridge/`, a separate submodule) or our own device dialog.
2. Standalone Editor (piano roll, doc 11 sub-project C) and views/transport (sub-project A).
3. VST/plugin build without the Editor, without the transport bar and without audio settings (the host
   provides them).
4. Docs for JX-CONSOLE: design guide colour (indigo `#5b6ee1`), plugins/JXMP/README.md, module list, a demo patch.

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

### Console, later the same evening

- MUTE/SOLO now light in the Mixer view (it draws the light layer): MUTE red, SOLO orange, channels
  silenced by another channel's solo blink their MUTE red. Square rubber buttons that light as a whole.
- Look: patina on the strips and the master, section titles cut into the dividers (EQ, SENDS, PAN,
  SOURCE, RETURNS, SENDS, OUTPUT), knob scales, cream scribble strips "CH n", red master fader cap.
- Editors: `compile_flags.txt` in `plugins/JXMP` and `src` (clangd finds the Rack headers; the
  "undeclared identifier" errors were only the editor).

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
