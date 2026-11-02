# PatchCore — status and next steps

Handoff notes, updated at the end of every work session (rule: keep this file current before a session
or weekly limit ends). Newest first.

## 2026-11-02 (night) — third JXMP set in progress

### Branches (all pushed, NOT merged; builds only when the user asks, the launcher rebuilds itself)

- `standalone-audio`: spec 12 / plan 13 done + review fixes; also selection only with Shift+click / Ctrl+drag,
  scratches never cross, console VU = channel post-fader, LED meters red only at 0 dBFS with a dBFS scale.
- `jxmp-set3` (on top of `standalone-audio`): spec [14](14-jxmp-modules-set3-design.md).
  - [x] JX-SCP scope (round CRT, trigger, AUTO/NORM, X-Y, afterglow, live slow sweeps, trace clipped to the circle)
  - [x] JX-CLK clock (CLOCK IN front/back one side at a time, transport, own tempo, x4…/16, swing)
  - [x] JX-EUC Euclidean rhythms (4 tracks, display, gate + accent)
  - [x] JX-DRM drums (kick, snare, hat open/closed, clap; accent; outs + mix)
  - [x] JX-REV reverb (plate/hall/room FDN, RT60, predelay, tone, mix)
  - [x] JX-CHR, JX-FLD, JX-RNG, JX-LGC, JX-SW, JX-PLY (2026-11-02 night, checked by screenshots front/back;
        all in patches/jxmp-workbench.vcv rows 3-4)
  - JX-SCP drawing: a smooth line through each point; audio at a slow time base shows as a peak-detect band
    (min..max bars, the line breaks there) instead of a jagged trace. A point counts as a swing only when its
    range is wider than the travel to its neighbours, so steep LFO slopes stay one line.
  - Menus cleaned up (no light mode, no Cardinal/Rack-only options), one-click menu switching.
  - VU "drops" investigated: DSP logging and 300 screen grabs showed no dropouts, only the dips of the
    showcase sequence (a step without gate every ~6 s).
- `run-patchcore.sh` with no argument opens `patches/jxmp-workbench.vcv` (showcase + console, scope, clock,
  euclid — add every new module there) and rebuilds first when sources are newer.

### JX-SCP: accepted by the user (2026-11-02, "super, paráda") at 3059185 — behaves as the user's Hantek
DSO2D15 with an analogue look: square tube, thin bezel; trigger point always the centre; only whole pictures
(double-buffered); NORM holds, AUTO waits for slow triggers; level kept on screen, dashed level line; TIME/DIV
redraws at once (1 kHz 10 s memory for slower, zoom of the last picture for faster). No roll, no auto level,
no top marker, no phosphor fade (all tried and rejected). Test patch: patches/jxmp-scope-test.vcv.

### (old) Open bug (user, 00:25): JX-SCP with an LFO still drifts right→left every sweep

Fix b2725d9 (AUTO waits 1.5× the trigger spacing) did not help the user. Not reproduced yet. Hypotheses to check
first, in the running app with the user watching workspace 3:
1. The trigger never fires (TRIG'D dark): trigger SOURCE is still CH1 (default; in the workbench CH1 = VCA audio,
   CH2 = LFO), or the LFO is unipolar (0…10 V) and LEVEL = 0 V never gets crossed (hysteresis ±0.05 V). Then AUTO
   free-runs and the picture drifts — real scopes do the same. If so: make it simpler (auto trigger level at the
   middle of the signal when nothing triggers, and/or AUTO falls back to the triggering channel).
2. The live slow-sweep drawing (sweep ≥ 0.1 s) mixing the buffer and the last frame.
Tests in plugins/JXMP/tests (scope, clock, euclid) are written but have not been run (user rule).

### Next

1. The scope drift bug above.
2. Next modules: JX-CHR, then JX-FLD, JX-RNG, JX-LGC, JX-SW, JX-PLY (add each to the workbench).
   JX-CLK: CLOCK IN only on the back now (user: the front one was a strange idea).
3. Later: full build + tests + VST3 check, merge `standalone-audio` then `jxmp-set3`, bump the collection.

## 2026-11-01 (evening) — standalone vs VST3, audio settings

### In progress: branch `standalone-audio` (pushed, NOT merged, NOT BUILT)

Spec [12](12-standalone-plugin-audio-design.md), plan [13](13-plan-standalone-plugin-audio.md); all 12 tasks
written. The user said: **no builds, no tests, no GitHub Actions until they ask for a build** — so everything
after Task 6 is uncompiled. Tasks 1–6 were built and checked (layout test, standalone + VST3 build, ALSA and JACK
start paths).

- Plugin: thin read-only bar (`TransportLayout.hpp`), names PatchCore / PatchCore FX by JXMP Audio, new IDs,
  `make vst3` builds only those two. F1/F2/F4 keys.
- DPF vendored (`dpf/` is a normal folder, `pugl` a submodule). `AudioDeviceSettings.hpp` (tested) holds
  setup, config file `PatchCoreAudio.conf` next to `PatchCore.json` (`PATCHCORE_AUDIO_CONFIG` overrides),
  fallback decisions.
- The main variant keeps DPF's native RtAudio fallback now (ALSA); the saved driver decides at start.
- RtAudioBridge: device by name, rate, buffer, live `reopen()` with revert, sample-rate callback, error →
  "Audio stopped". Utils: `getAudioDevices/getAudioSetup/getAudioNotice/requestAudioSetup/isAudioRunning/showAsioPanel`.
- JACK: the app connects its outputs to the default sink (pactl), `run-patchcore.sh` only launches.
- `src/patchcore/AudioSettings.cpp` (per variant): dialog, test tone, notice strip, `restartApp()`.
- Windows ASIO: wiring only; the SDK sources are added with the first Windows build.

Also on the branch: a plain click on a module panel no longer selects it (the rack's selection box now
needs a 6 px drag), and scratches in the patina never cross each other.

Final review (fresh reviewer) done: the Critical (link) and all Important findings are fixed in 5e1db44,
uncompiled. Deferred minors (see `.superpowers/sdd/13-plan-standalone-plugin-audio/progress.md`): Linux restart
skips Rack's normal shutdown; JACK ignores the chosen rate and its buffer change is server-wide; ALSA default
output + named input opens the default input; a card held by PipeWire is not listed under ALSA; config parse
keeps `\r`; info-text truncation cuts UTF-8; CardinalRemote lacks AudioSettings.cpp; main and Synth variants
share the name in LV2/CLAP; ASIO single-file include risky on 32-bit MinGW.

### Next

1. When the user asks for a build: `make -j12 jack && make -j12 vst3`, fix compile errors, run
   `src/patchcore/tests/run.sh` and `plugins/JXMP/tests/run.sh`, then the GUI checks with the user
   watching workspace 3: standalone bar, top bar F-keys labels, Audio Settings dialog, ALSA device switch,
   rate change (pitch), driver switch restart (patch path + unsaved kept), missing-card notice; VST3 in a host.
2. Final whole-branch review, merge `standalone-audio` into `main`, push, bump the collection.
3. Then the Editor (sub-project C of doc 11) — needs its own spec.
3b. Third JXMP module set, design [14](14-jxmp-modules-set3-design.md): JX-SCP first (round green CRT scope),
   then JX-CLK + JX-EUC, JX-DRM, JX-REV + JX-CHR, the small ones. Order against the Editor: the user decides.
4. JX-CONSOLE docs (design guide colour, plugins/JXMP/README.md, module list, demo patch).

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
