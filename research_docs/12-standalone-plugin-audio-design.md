# 12 — Standalone vs. plugin, audio device settings

Status: draft for review (2026-11-01). Extends sub-project A of [11](11-views-console-editor-design.md).
The Editor (sub-project C) gets its own spec later.

## Goal

PatchCore ships as a **standalone app** and as **VST3 plugins**, and each shows only what makes sense there:

- **Standalone:** its own transport, the Editor (later), the Mixer, and an **Audio Settings** dialog to choose
  the driver, the sound card, the sample rate and the buffer size.
- **Plugin:** the DAW (Cubase, Ardour, FL Studio…) owns transport, audio device and MIDI. No Editor, no audio
  settings, only a thin read-only info bar at the bottom.

## Decisions (questionnaire 2026-11-01)

| Topic | Decision |
|---|---|
| Plugin bottom bar | **Thin info bar**, read-only: host tempo, signature, position, playing state, DSP and MASTER meters |
| Plugin views | Rack · Back · Mixer (Mixer only with a JX-CONSOLE in the rack) |
| Standalone views | Rack · Back · Editor · Mixer (F1–F4); Editor appears with sub-project C |
| Audio settings place | Menu **Engine → Audio Settings…** opens a dialog (standalone only) |
| Plugin formats | **VST3** only |
| Platforms | **Linux** and **Windows** |
| Linux drivers | **PipeWire/JACK** (shared) and **ALSA** (direct) |
| Windows drivers | **WASAPI** and **ASIO** |
| Plugin variants | **PatchCore** (instrument, MIDI in) and **PatchCore FX** (stereo effect) |
| Names | PatchCore / PatchCore FX, maker **JXMP Audio**; the standalone is PatchCore |
| Start-up | Remember the last audio setup and reconnect; a missing device falls back to the default output with a notice |
| MIDI | **Nothing new.** MIDI goes through Cardinal's Host MIDI modules (notes, CC, gates…), which already work; the standalone keeps today's JACK MIDI / "Enable/Reconnect MIDI" path. The dialog is audio only |
| DPF | **No fork repo.** `dpf/` stops being a submodule and becomes a normal folder in the PatchCore repo (vendored); its nested `pugl` stays an upstream submodule |

## Part 1 — Modes and builds

### Mode

- `patchcore::isStandalone()` (in `src/patchcore/PatchCore.hpp`): true for the JACK/native standalone build,
  false in a plugin host. Everything mode-dependent asks this one function.

### Top bar

- Standalone: Rack · Back · Editor · Mixer, F1–F4. Until the Editor exists its segment is not shown.
- Plugin: Rack · Back · Mixer.

### Bottom bar

- Standalone: today's transport bar (tempo, signature, transport buttons, position, time, DSP, MASTER, engine
  info on the right). The engine info shows the real state: `48 kHz · 256 smp · ALSA: <device>`.
- Plugin: a thin bar (about half the height) with read-only LCDs: host tempo, signature, bar.beat position,
  a playing indicator; DSP and MASTER meters; `VST3 · Host sync` on the right. No buttons.

### Menus

- Standalone: Engine → Audio Settings… (Part 2). Cardinal's "Buffer Size" and "Enable Audio Input" entries go
  away (the dialog replaces them); "Enable/Reconnect MIDI" stays.
- Plugin: no audio entries.

### Builds

- Standalone **PatchCore** (Linux, Windows) from the current `src/Cardinal` JACK/native target.
- VST3 **PatchCore**: instrument with MIDI input and stereo output, from `src/CardinalSynth`.
- VST3 **PatchCore FX**: stereo effect, from `src/CardinalFX`.
- `DISTRHO_PLUGIN_BRAND` "JXMP Audio", names as above, **new unique IDs** (VST3 class IDs, brand/plugin
  codes), so a DAW never confuses PatchCore with Cardinal.
- Other Cardinal variants (Mini, MiniSep, Native, Remote, LV2/CLAP/VST2/AU targets) are not built by default.
- Windows: cross-compile (mingw) or GitHub Actions — Actions are started only when the user asks.

## Part 2 — Audio device settings (standalone)

### DPF in the tree

- Convert the `dpf` submodule into a normal folder at the same commit (`git rm --cached dpf`, add the files,
  keep `dpf/dgl/src/pugl-upstream` as a submodule of PatchCore with its upstream URL).
- PatchCore changes to DPF are marked `// PatchCore:` and kept small; new logic lives in new files.

### Architecture

Three units, each testable on its own:

1. **`AudioDeviceSettings.hpp`** (new, in `dpf/distrho/src/jackbridge/`): plain data and decisions, no I/O.
   - `AudioSetup { driver, outputDevice, inputDevice (or none), sampleRate, bufferSize }`
   - `DriverInfo`, `DeviceInfo { id, name, channels, sampleRates }`
   - `chooseSetup(saved, available)` → what to open: the saved setup if its device exists and supports the
     rate/buffer, otherwise the nearest rate/buffer, otherwise the driver's default output (and a notice).
   - JSON read/write of `AudioSetup`.
2. **Bridge changes** (`JackBridge.cpp`, `RtAudioBridge.hpp`): enumerate drivers and devices; `reopen(setup)`
   stops the engine, closes the current stream (JACK client or RtAudio stream), opens the new one, restarts;
   if opening fails it reopens the previous working setup and reports the error.
   - PipeWire/JACK: a JACK client; rate and buffer are requested (PipeWire may keep its own rate — the dialog
     then shows the real one); outputs are linked to the default sink, as `run-patchcore.sh` does today
     (the script's linking moves into the app).
   - ALSA: RtAudio `LINUX_ALSA` with the chosen `hw:` device, exclusive.
   - WASAPI / ASIO: RtAudio `WINDOWS_WASAPI` / `WINDOWS_ASIO`; the ASIO SDK (GPLv3) is added to the tree.
   - Exposed to the app through `DistrhoStandaloneUtils.hpp` (`getAudioDrivers()`, `getAudioDevices(driver)`,
     `getAudioSetup()`, `requestAudioSetup(setup)`, `showAsioPanel()`).
3. **Dialog** (`src/patchcore/AudioSettings.cpp`): a PatchCore-styled window over the rack:
   driver, output device, input device (with "off"), sample rate (44.1 / 48 / 88.2 / 96 kHz as the device
   allows), buffer (64–2048), resulting latency in ms, **Test tone** (a short beep on the outputs),
   **ASIO panel…** (ASIO only), **Apply** / **Close**. Errors from `reopen` appear in the dialog.

### Persistence

- The applied `AudioSetup` is saved in `PatchCore.json` (app settings, not the patch).
- On start: `chooseSetup(saved, available)`; a fallback shows a notice in the top bar
  ("Sound card <name> not found, using the default output").

### Error handling

- Opening fails → back to the previous setup, error text in the dialog.
- Device unplugged while running → the stream stops; the transport bar shows "Audio stopped" and the dialog
  offers to reconnect (no crash, the patch keeps its state).

## Testing

- `g++` tests for `AudioDeviceSettings.hpp`: missing device, unsupported rate/buffer, nearest values, JSON
  round trip, fallback notice.
- `g++` tests for mode-dependent layout helpers (which segments and which bar for standalone/plugin).
- Screenshots of the standalone (dialog, info text) — on Hyprland workspace 3, see the memory notes.
- By hand on the user's PC: PipeWire, ALSA to the sound card, buffer change while playing, unplugging.
- VST3: load PatchCore and PatchCore FX in Ardour (Linux), check the thin bar, no Audio Settings, MIDI through
  Host MIDI. Windows build checked when the user runs it.

## Order

1. Part 1 — mode function, top/bottom bar per mode, menus, VST3 names/IDs and build targets.
2. Part 2 — vendor DPF, `AudioDeviceSettings.hpp` + tests, bridge reopen (Linux first: PipeWire/JACK, ALSA),
   dialog, persistence; then Windows (WASAPI, ASIO).

## Open points for the plan

- Exact thin-bar height and layout in the plugin.
- Whether the Windows build runs through mingw cross-compile in the distrobox or only through Actions.
