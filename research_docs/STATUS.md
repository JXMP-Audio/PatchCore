# PatchCore — status and next steps

Handoff notes, updated at the end of every work session (rule: keep this file current before a session
or weekly limit ends). Newest first.

## 2026-11-02 (evening) — merged into main (READ THIS FIRST in a new chat)

### State: `standalone-audio`, `jxmp-set3` and `tutorials` are merged into `main` and pushed

- Built (`make jack`, `make vst3`) and all tests pass: `src/patchcore/tests/run.sh` (5 programs),
  `plugins/JXMP/tests/run.sh` (23). Pre-merge review of the whole branch done, its Important findings fixed
  (f9895b9: scope triple buffer and trigger ring, reverb/chorus NaN, clock 1 PPQN, buffers off the audio thread).
- Tutorials: 6 lessons, **Learn** button in the top bar and Help → Tutorials (details below).
- Keys: F1 Rack, or with a selected module (Shift+click) its manual; F2 Back; F3 Mixer; F4 kept for the Editor.
- `docs/manual/jx-*.md`: one page per JXMP module (today's description; the real manual is to be written per
  module), linked from `plugins/JXMP/plugin.json` (manualUrl, F1) and from the module list in README.md.
- PluginCollection pointer bumped and the PatchCore card on the web updated (text; the screenshot is still the old
  one, `docs/screenshots/patchcore-rack.png`).

### Later the same evening (7e6cf78 and after)

- Deferred review minors fixed: Euclid/Switch save their step, chorus mode glide, stereo reverb input.
- `PATCHCORE_DEV_CHECK=1 ./bin/Cardinal`: self-check of every JXMP panel (widgets, names, tooltips, overlaps, panel
  edge) plus a DSP fuzz (random inputs and knobs, NaN burst, CPU load); logs "DEV CHECK DONE: n problem(s)" and
  closes. It found a crash: a NaN at JX-OSC MORPH indexed outside an array (fixed, test added) and 8 missing
  tooltips (added). Now: 31 models, 0 problems.
- Code check done (8379901): the core UI read by hand (QuickSearch, TopBar, MixerView, Navigator; Tutorial and
  AudioSettings were reviewed before) and every module's process() scanned for allocation / locks (none). Fixed: the
  Mixer open state was global (opened in every plugin instance), quick-search hover and favourites, Navigator colours
  and legend, LCD clip.
- Second pass (7c9367a): clang-tidy (bugprone, clang-analyzer) over JXMP and src/patchcore found nothing real;
  TopBar compiler warnings fixed. PATCHCORE_DEV_CHECK also saves/loads every module (toJson → fromJson → toJson
  must match) and checks every shipped patch (patches/jxmp-*.vcv, lesson start.vcv): all clean.
  Next: the build the user announces, then the screen checks, then the Editor.

### Not checked yet (needs the user at the screen)

- Engine → Audio Settings dialog: device switch, rate change, driver switch restart, missing-card notice.
- VST3 in a host: no host is installed (Reaper/Bitwig/Carla/Ardour); the bundles export the VST3 entry points and
  resolve their libraries. Ask the user to install a host (or allow pluginval).
- Lessons 2–6 stepped through by hand; the user still finds drums vs melody in lessons 5/6 "not sitting together"
  (left as is on the user's word).
- New screenshots for the web card.

### Deferred (pre-merge review minors)

JX-EUC / JX-SW step not saved with the patch (restart from step 1 after a reload); JX-CHR click when switching
chorus/ensemble (base delay jumps 7 → 10 ms); JX-REV sums its input to mono (say so in spec 14 or make it stereo).

### Next

1. The checks above with the user; new screenshots; rebalance `patches/jxmp-workbench.vcv` if the user wants.
2. Standalone Editor (piano roll, doc 11 sub-project C, F4): brainstorm a spec with the user first.
3. Manuals per module (`docs/manual/`).
4. Windows build (WASAPI + ASIO SDK sources).
5. Later: JX808 panel polish (collection memory).

## 2026-11-02 — earlier that day

### State: everything pushed, nothing merged

- Branch `standalone-audio` (spec 12 / plan 13): sound card settings in the standalone, thin plugin bar,
  PatchCore + PatchCore FX VST3, DPF vendored, review fixes. Built for jack and used daily; VST3 and Windows
  not checked since the review fixes.
- Branch `jxmp-set3` (on top of `standalone-audio`, HEAD 51fe67f), spec [14](14-jxmp-modules-set3-design.md):
  all 11 modules of the third set are done and checked by screenshots front/back:
  JX-SCP (accepted, Hantek DSO2D15 behaviour, see below), JX-CLK, JX-EUC, JX-DRM, JX-REV, JX-CHR, JX-FLD,
  JX-RNG, JX-LGC, JX-SW, JX-PLY. Also on the branch: menus cleaned up (dark only, one-click switching,
  highlight), selection only with Shift+click / Ctrl+drag, console VU = post-fader, scope draws a smooth line
  with peak-detect bands for audio at slow time bases.
- `patches/jxmp-workbench.vcv` = the showcase, rebuilt 2026-11-02 and liked by the user ("supr"): Am-F-C-G
  one chord per bar, 100 BPM, uses every JXMP module (pad with chorus + reverb, folded pluck bass, ring-mod
  bell arpeggio through PLY split + SW, drums from CLK > EUC > DRM with the clap via LGC AND, scope on the
  bass). Generator script logic: one chord per bar from SEQ (clocked /4) > ATT offsets (third +0.2917 V,
  fifth +0.5667 V, octave +1 V) > PLY merge > QNT A minor. `./run-patchcore.sh` opens it and rebuilds first.
  Levels are a first guess (not measured): the user may ask to rebalance.
- VU needles "dying": investigated, not a bug (dips were the old showcase sequence's gateless steps).
- Tests in `plugins/JXMP/tests` (scope, clock, euclid, drums, reverb, chorus, shapers) are written, NOT run:
  user rule = no builds/tests/Actions until the user says "build".
- The 9 deferred review minors of `standalone-audio` (M2–M6, M8, M9, M11, M12) are all fixed in 48e6e6c (checked
  again 2026-11-02); M3 (JACK buffer is server-wide) and M5 (a card PipeWire holds cannot be listed under ALSA)
  are limits of JACK/ALSA, the dialog hint and the notice say so. Nothing is left to fix before the build.
- PluginCollection submodule pointer not bumped (it follows `main`; branches are not merged yet).

### Tutorials: built, checked by ear with the user (branch `tutorials`, holds all three branches)

Built 2026-11-02 (`make jack`, `make vst3`), all tests pass (`src/patchcore/tests/run.sh`,
`plugins/JXMP/tests/run.sh`). 6 lessons in `plugins/JXMP/res/tutorials` (Help → Tutorials and the **Learn** button in
the top bar). Each lesson starts where the one before ended and adds only the modules it teaches; lesson 1 teaches the
quick search with `@jxmp` / `#purpose`. Voice tuned by ear with the user (bass octave, 110 BPM, eighths, decay 300 ms,
sustain 60 %, release 210 ms, cutoff 520 Hz, resonance 40 %). Lesson 5 teaches RESET (clock RESET → both sequencers,
RUN off/on); JX-CLK now restarts when its RESET jack gets patched (lines sequencers up when a patch opens).
The first test run found and fixed: JX-CHR delay one sample short, JX-SCP notch at the trigger point, test_clock baseline.

Open: the user still finds that drums and melody in lessons 5/6 "do not sit together" (timing is aligned in code;
cause not found, left as is on the user's word 2026-11-02). Not yet checked: lessons 2–6 stepped through by hand,
VST3 in a host.

### JX-SCP: accepted (2026-11-02, "super, paráda")

Behaves as the user's Hantek DSO2D15 with an analogue look: square tube, thin bezel; trigger point always the
centre; only whole pictures (double-buffered); NORM holds, AUTO waits for slow triggers; level kept on screen,
dashed level line; TIME/DIV redraws at once (1 kHz 10 s memory for slower, zoom of the last picture for
faster). No roll, no auto level, no top marker, no phosphor fade (all tried and rejected). The user said the
drawing was "still a bit buggy"; the smooth line + peak bands (97c1fe3) is the answer, not yet confirmed by
the user. Test patch: patches/jxmp-scope-test.vcv.

### Next (in this order unless the user says otherwise)

1. Ask the user how the new showcase sounds / looks; rebalance levels if needed (check the MASTER meter).
2. DONE 2026-11-02 (build + tests on `tutorials`); still open from this item: GUI checks of the audio dialog and VST3 in a host. Was: `make -j12 jack && make -j12 vst3`, run `src/patchcore/tests/run.sh` and
   `plugins/JXMP/tests/run.sh`, fix failures; GUI checks with the user on workspace 3 (audio dialog, driver
   switch, VST3 in a host).
3. Final review, merge `standalone-audio` then `jxmp-set3` into `main`, push, bump PluginCollection
   (`./update-plugins.sh`), update the PatchCore card in `web/index.html` (new screenshots in `docs/screenshots/`).
4. Standalone Editor (piano roll, sub-project C of doc 11): needs its own spec (brainstorm with the user first).
5. Windows build: WASAPI + ASIO (SDK sources added with the first Windows build).
6. Later: JX808 panel polish (collection memory).

### Working notes

- Screenshots: `cap.sh` (scratchpad, recreate if lost) with PATCHCORE_DEV_PATCH/ZOOM/OFFSET/VIEW env vars;
  window goes to workspace 3, which renders only while the user looks at it (check `hyprctl activeworkspace`).
  If the user is on another workspace, just launch `./run-patchcore.sh` for them instead.
- Kill the app with `pkill -x Cardinal` (never `pkill -f bin/Cardinal`: it kills the shell).

## 2026-11-01 (evening) — standalone vs VST3, audio settings

### In progress: branch `standalone-audio` (pushed, NOT merged, NOT BUILT)

Spec [12](12-standalone-plugin-audio-design.md), plan [13](13-plan-standalone-plugin-audio.md); all 12 tasks
written. The user said: **no builds, no tests, no GitHub Actions until they ask for a build** — so everything
after Task 6 is uncompiled. Tasks 1–6 were built and checked (layout test, standalone + VST3 build, ALSA and JACK
start paths).

- Plugin: thin read-only bar (`TransportLayout.hpp`), names PatchCore / PatchCore FX by JXMP Audio, new IDs,
  `make vst3` builds only those two. F1/F2/F4 keys (later F1/F2/F3, F4 for the Editor).
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
uncompiled. The deferred minors below were fixed later in 48e6e6c. Deferred minors (see `.superpowers/sdd/13-plan-standalone-plugin-audio/progress.md`): Linux restart
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
