# Standalone vs. Plugin and Audio Settings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PatchCore builds as a standalone app with an Audio Settings dialog (driver, device, sample rate,
buffer) and as two VST3 plugins (PatchCore, PatchCore FX) that show a thin read-only info bar instead of the
transport and have no audio settings.

**Architecture:** Mode is `rack::isStandalone()` (exists, `src/CardinalCommon.cpp:898`). The bottom bar
picks its layout from a small pure helper. Audio settings are split into a pure header
(`AudioDeviceSettings.hpp`: data, config file, fallback decisions — `g++` tested), small `// PatchCore:`
changes in the vendored DPF audio bridge (device/rate/buffer reopen for RtAudio, driver choice at start,
JACK autoconnect), and a PatchCore dialog. Device, rate and buffer change live (close → open → revert on
failure, as DPF already does for the buffer size); **switching the driver (PipeWire/JACK ↔ ALSA, WASAPI ↔
ASIO) restarts the app** with the current patch, because DPF opens a JACK client and a native stream through
different code paths at start-up.

**Tech Stack:** C++11, DPF (vendored), RtAudio 5.1 (in DPF), JACK via PipeWire, NanoVG/Rack widgets, `g++`
test runners (`src/patchcore/tests/run.sh`).

**Spec:** `research_docs/12-standalone-plugin-audio-design.md`

## Global Constraints

- Platforms: Linux and Windows. Plugin format: VST3 only.
- Linux drivers: PipeWire/JACK (shared) and ALSA (direct). Windows drivers: WASAPI and ASIO.
- Plugin variants: **PatchCore** (instrument, MIDI in, from `src/CardinalSynth`) and **PatchCore FX**
  (stereo effect, from `src/CardinalFX`); maker **JXMP Audio**; new unique IDs.
- MIDI: nothing new — Host MIDI modules; keep "Enable/Reconnect MIDI". The dialog is audio only.
- DPF: no fork repo; `dpf/` becomes a normal folder in PatchCore, `pugl` stays an upstream submodule.
  Every change in DPF files is marked `// PatchCore:`.
- No third-party trademarks in UI text; names: PatchCore, JXMP Audio.
- Never start GitHub Actions unless the user asks. Never write the user's `~/.config/Cardinal/` in tests:
  use `PATCHCORE_AUDIO_CONFIG=<scratch file>`.
- GUI checks: never click with xdotool on the user's visible workspace; captures via `PATCHCORE_CAPTURE`
  (see memory `gui-testing-hyprland`).
- `src/CardinalPlugin.cpp` is CRLF — keep it.
- Build: `make -j12 jack` (standalone), `make -j12 vst3` (plugins). After switching branches
  `touch plugins/plugins.cpp`.

## Review Focus

1. Saved sound card missing at start → app starts on the default output and shows the notice; no crash.
   (Task 5 test `missing output device`, Task 9 manual check.)
2. Sample rate changed in the dialog while playing → the Rack engine runs at the new rate (pitch stays
   correct, the bar shows the new rate). (Task 7 step "sample rate callback", Task 9 check.)
3. Driver switch with an unsaved or modified patch → after the restart the same patch is open, with its
   original file path. (Task 9 restart test.)
4. Plugin mode: no Audio Settings, no transport buttons; a click in the thin bar does nothing.
   (Task 2 layout test, Task 4 host check.)
5. Garbage or empty config file → defaults, no crash. (Task 5 test `parse garbage`.)

---

## Part 1 — Modes and builds

### Task 1: Bottom bar layout per mode (pure helper)

**Files:**
- Create: `src/patchcore/TransportLayout.hpp`
- Test: `src/patchcore/tests/test_transport_layout.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace rack { namespace patchcore {
  struct TransportLayout {
      float height;          // 74 standalone, 34 plugin
      bool buttons;          // transport buttons, tempo arrows
      bool editableTempo;    // tempo drag / arrows
      bool time;             // the TIME LCD
  };
  TransportLayout transportLayout(bool standalone);
  }}
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// src/patchcore/tests/test_transport_layout.cpp
#include <cassert>
#include <cstdio>
#include "../TransportLayout.hpp"

using namespace rack::patchcore;

int main() {
	const TransportLayout s = transportLayout(true);
	assert(s.height == 74.f && s.buttons && s.editableTempo && s.time);
	// Plugin: thin, read-only, the host owns the transport
	const TransportLayout p = transportLayout(false);
	assert(p.height == 34.f && !p.buttons && !p.editableTempo && !p.time);
	std::printf("transport layout: all tests passed\n");
	return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash src/patchcore/tests/run.sh`
Expected: FAIL, `TransportLayout.hpp: No such file or directory`

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/patchcore/TransportLayout.hpp
/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What the bottom bar shows in each mode. The standalone has its own transport; in a plugin the DAW owns
 * transport, tempo and audio, so the bar is a thin read-only display. No Rack dependency (tested).
 */
#pragma once

namespace rack {
namespace patchcore {

struct TransportLayout {
	float height;
	bool buttons;
	bool editableTempo;
	bool time;
};

inline TransportLayout transportLayout(bool standalone) {
	if (standalone)
		return TransportLayout{74.f, true, true, true};
	return TransportLayout{34.f, false, false, false};
}

} // namespace patchcore
} // namespace rack
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash src/patchcore/tests/run.sh`
Expected: `transport layout: all tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/patchcore/TransportLayout.hpp src/patchcore/tests/test_transport_layout.cpp
git commit -m "PatchCore: bottom bar layout per mode (standalone / plugin)"
```

### Task 2: Thin read-only bar in the plugin

**Files:**
- Modify: `src/patchcore/PatchCoreUI.cpp` (struct `TransportBar`: `step()`, `onButton()`, `draw()`,
  `createTransportBar()`)

**Interfaces:**
- Consumes: `transportLayout(bool)` from Task 1; `rack::isStandalone()` from `src/CardinalCommon.hpp`.

- [ ] **Step 1: Use the layout in `TransportBar`**

Add `#include "TransportLayout.hpp"` and `#include "../CardinalCommon.hpp"` (already included). In
`TransportBar` add a member `TransportLayout layout = transportLayout(rack::isStandalone());`.

In `createTransportBar()` replace `bar->box.size.y = 74.f;` with `bar->box.size.y = bar->layout.height;`.

In `step()`, when `!layout.buttons`, lay out a single row (everything vertically centred, `cy = box.size.y / 2`):

```cpp
	if (!layout.buttons) {
		const float cy = box.size.y * 0.5f;
		float x = 16.f;
		tempoRect = math::Rect(x, cy - 11.f, 92.f, 22.f);
		x += 92.f + 10.f;
		sigRect = math::Rect(x, cy - 11.f, 38.f, 22.f);
		x += 38.f + 10.f;
		posRect = math::Rect(x, cy - 11.f, 120.f, 22.f);
		x += 120.f + 10.f;
		playRect = math::Rect(x, cy - 6.f, 12.f, 12.f);  // playing indicator
		const float right = box.size.x - 16.f;
		masterRect = math::Rect(right - 130.f - kMasterW, cy - 11.f, kMasterW, 22.f);
		dspRect = math::Rect(masterRect.pos.x - 110.f, cy - 5.f, 82.f, 12.f);
		const float minX = playRect.pos.x + 30.f;
		showMaster = masterRect.pos.x >= minX;
		showDsp = showMaster && dspRect.pos.x >= minX;
		updateMeters();
		OpaqueWidget::step();
		return;
	}
```

(declare `math::Rect playRect;` with the other rects). Labels ("TEMPO", "SIG", "BARS") are drawn inside the
LCD on its left at 8 px instead of above it, because the bar has no room above.

- [ ] **Step 2: Read-only behaviour**

At the top of `onButton()`: `if (!layout.buttons) return;` — no tempo drag, no play/stop from the plugin.

In `draw()`, when `!layout.buttons`: draw the background, tempo, signature, position LCDs (same code with
the smaller rects and `drawLcd(vg, r, 15.f, …)`), a round lamp in `playRect` (green `nvgRGB(0x55, 0xe0,
0x7c)` while `context->playing`, dark `nvgRGB(0x1d, 0x2a, 0x20)` otherwise), DSP and MASTER as today, and on
the right `string::f("%s  ·  Host sync", getPluginFormatName())`. Skip buttons, tempo arrows and TIME.

- [ ] **Step 3: Build the standalone and check nothing changed there**

Run: `make -j12 jack 2>&1 | grep -E " error" ; echo $?` → no errors.
Capture (scratch `cap.sh`, workspace 3): `PATCHCORE_DEV_PATCH=patches/jxmp-showcase.vcv cap.sh /…/standalone.png`
Expected: the bar looks as before (74 px, buttons).

- [ ] **Step 4: Build the VST3 and check the thin bar**

Run: `make -j12 vst3 2>&1 | grep -E " error"` → no errors; `ls bin/*.vst3`.
Load `bin/CardinalSynth.vst3` in a host available in the container (check `command -v carla ardour8 ardour
reaper`); if none is available, ask the user to load it in Ardour. Expected: a 34 px bar with tempo, SIG,
BARS, play lamp, DSP, MASTER, `VST3 · Host sync`; clicking it does nothing.

- [ ] **Step 5: Commit**

```bash
git add src/patchcore/PatchCoreUI.cpp
git commit -m "PatchCore: thin read-only info bar in the plugin"
```

### Task 3: F1–F4 view keys

**Files:**
- Modify: `src/patchcore/TopBar.cpp` (`ViewSegment`), and where Tab is handled (search
  `GLFW_KEY_TAB` in `src/override/Scene.cpp`)

- [ ] **Step 1: Add the keys next to the Tab handling in `Scene::onHoverKey`**

```cpp
		// PatchCore: F1 Rack, F2 Back, F4 Mixer (F3 is reserved for the Editor, standalone only)
		if (patchcore::enabled() && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			switch (e.key) {
				case GLFW_KEY_F1: patchcore::showMixer(false); patchcore::setViewSide(patchcore::VIEW_FRONT); e.consume(this); return;
				case GLFW_KEY_F2: patchcore::showMixer(false); patchcore::setViewSide(patchcore::VIEW_BACK); e.consume(this); return;
				case GLFW_KEY_F4:
					if (patchcore::mixerAvailable())
						patchcore::showMixer(!patchcore::mixerShown());
					e.consume(this);
					return;
			}
		}
```

- [ ] **Step 2: Show the keys in the segment**

In `ViewSegment::items` set the key labels: Rack `"F1"` (Tab stays as the flip key in the help panel),
Back `"F2"`, Mixer `"F4"`.

- [ ] **Step 3: Build, capture the top bar, check the labels fit** (`make -j12 jack`, `cap.sh`).

- [ ] **Step 4: Commit**

```bash
git add src/override/Scene.cpp src/patchcore/TopBar.cpp
git commit -m "PatchCore: F1/F2/F4 switch Rack, Back and the Mixer"
```

### Task 4: PatchCore names and IDs; build only PatchCore targets

**Files:**
- Modify: `src/CardinalSynth/DistrhoPluginInfo.h`, `src/CardinalFX/DistrhoPluginInfo.h`,
  `src/Cardinal/DistrhoPluginInfo.h` (standalone name), `src/Makefile` (`vst3:` target),
  `run-patchcore.sh` (JACK client name in the `pw-link` lines)

- [ ] **Step 1: Instrument**

In `src/CardinalSynth/DistrhoPluginInfo.h`:

```cpp
#define DISTRHO_PLUGIN_BRAND   "JXMP Audio"
#define DISTRHO_PLUGIN_NAME    "PatchCore"
#define DISTRHO_PLUGIN_LABEL   "PatchCore"
#define DISTRHO_PLUGIN_URI     "https://github.com/JXMP-Audio/PatchCore#instrument"
#define DISTRHO_PLUGIN_CLAP_ID "audio.jxmp.patchcore"

#define DISTRHO_PLUGIN_AU_TYPE   aumu
#define DISTRHO_PLUGIN_BRAND_ID  JXMP
#define DISTRHO_PLUGIN_UNIQUE_ID PcIn
```

and delete `#define DPF_VST3_DONT_USE_BRAND_ID` (the VST3 class ID then comes from brand + unique ID, so it
differs from Cardinal's).

- [ ] **Step 2: Effect**

In `src/CardinalFX/DistrhoPluginInfo.h` the same with `NAME "PatchCore FX"`, `LABEL "PatchCoreFX"`,
`URI "…#fx"`, `CLAP_ID "audio.jxmp.patchcore.fx"`, `UNIQUE_ID PcFx`, and delete
`DPF_VST3_DONT_USE_BRAND_ID` if present.

- [ ] **Step 3: Standalone name**

In `src/Cardinal/DistrhoPluginInfo.h` set `DISTRHO_PLUGIN_BRAND "JXMP Audio"` and
`DISTRHO_PLUGIN_NAME "PatchCore"` (window title and JACK client name become "PatchCore"). In
`run-patchcore.sh` replace `Cardinal:audio_out_` with `PatchCore:audio_out_` (3 places). Keep the binary
file name `bin/Cardinal` (scripts and docs use it).

Check that nothing compares the plugin name: `grep -rn '"Cardinal"' src/*.cpp src/override src/patchcore`
— any hit that matches `getPluginName()`/`DISTRHO_PLUGIN_NAME` must be adjusted.

- [ ] **Step 4: VST3 target builds only the two PatchCore plugins**

In `src/Makefile` remove the line `	$(MAKE) vst3 -C Cardinal` from the `vst3:` target.

- [ ] **Step 5: Build and verify**

Run: `make -j12 jack && make -j12 vst3`
Expected: `bin/CardinalSynth.vst3`, `bin/CardinalFX.vst3` exist, no `bin/Cardinal.vst3` rebuilt.
Run `bin/Cardinal` via `cap.sh`: window title starts with "PatchCore". In a VST3 host (or ask the user):
the plugin list shows "PatchCore" and "PatchCore FX" by "JXMP Audio".

- [ ] **Step 6: Commit**

```bash
git add src/CardinalSynth/DistrhoPluginInfo.h src/CardinalFX/DistrhoPluginInfo.h src/Cardinal/DistrhoPluginInfo.h src/Makefile run-patchcore.sh
git commit -m "PatchCore names and IDs: PatchCore and PatchCore FX by JXMP Audio, VST3 builds only those"
```

---

## Part 2 — Audio settings (standalone)

### Task 5: Vendor DPF and add `AudioDeviceSettings.hpp` (pure, tested)

**Files:**
- Modify: `.gitmodules`, `dpf/` (submodule → folder)
- Create: `dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp`
- Create: `src/patchcore/tests/test_audio_settings.cpp`

**Interfaces:**
- Produces (namespace `patchcore_audio`, header-only, no DPF/Rack includes):
  ```cpp
  enum class Driver { Jack, Alsa, Wasapi, Asio };
  const char* driverKey(Driver);                       // "jack", "alsa", "wasapi", "asio"
  const char* driverName(Driver);                      // "PipeWire / JACK", "ALSA", "WASAPI", "ASIO"
  bool driverFromKey(const std::string&, Driver*);
  std::vector<Driver> platformDrivers();               // Linux {Jack, Alsa}, Windows {Wasapi, Asio}
  struct AudioSetup { Driver driver; std::string output, input; unsigned sampleRate, bufferSize; };
  //   output "" = system default; input "" = input off
  AudioSetup defaultSetup();                           // platformDrivers()[0], "", "", 48000, 256
  struct DeviceInfo { std::string name; unsigned outputs, inputs; std::vector<unsigned> sampleRates; };
  struct Choice { AudioSetup setup; std::string notice; };
  static const unsigned kBufferSizes[6];               // 64 … 2048
  unsigned nearest(const std::vector<unsigned>& options, unsigned wanted);
  Choice chooseSetup(const AudioSetup& saved, const std::vector<DeviceInfo>& devices);
  std::string serialize(const AudioSetup&);
  AudioSetup parse(const std::string& text);
  std::string configPath();                            // PATCHCORE_AUDIO_CONFIG or <config>/Cardinal/PatchCoreAudio.conf
  AudioSetup loadSetup();
  bool saveSetup(const AudioSetup&);
  double latencyMs(const AudioSetup&);
  ```

- [ ] **Step 1: Vendor DPF at its current commit**

```bash
git -C dpf status --short            # must be empty
git rm --cached dpf
git config -f .gitmodules --remove-section submodule.dpf
rm -rf dpf/.git                      # dpf/.git is a gitfile pointing into .git/modules/dpf
git rm --cached -r dpf/dgl/src/pugl-upstream 2>/dev/null || true
rm -rf dpf/dgl/src/pugl-upstream
git submodule add https://github.com/DISTRHO/pugl.git dpf/dgl/src/pugl-upstream
git -C dpf/dgl/src/pugl-upstream checkout <the commit recorded in the old dpf tree:
  git -C .git/modules/dpf ls-tree HEAD dgl/src/pugl-upstream>
rm -f dpf/.gitmodules
git add .gitmodules dpf
git commit -m "Vendor DPF (no fork repo): dpf/ is now a normal folder, pugl stays an upstream submodule"
```

Verify: `make -j12 jack` builds; `git submodule status | grep pugl` shows the same commit as before.

- [ ] **Step 2: Write the failing test**

```cpp
// src/patchcore/tests/test_audio_settings.cpp
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "../../../dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp"

using namespace patchcore_audio;

static DeviceInfo dev(const char* name, unsigned outs, unsigned ins, std::vector<unsigned> rates) {
	DeviceInfo d;
	d.name = name;
	d.outputs = outs;
	d.inputs = ins;
	d.sampleRates = rates;
	return d;
}

int main() {
	// Round trip
	AudioSetup s = defaultSetup();
	s.driver = Driver::Alsa;
	s.output = "Scarlett 2i2: USB Audio (hw:2,0)";
	s.input = "Scarlett 2i2: USB Audio (hw:2,0)";
	s.sampleRate = 96000;
	s.bufferSize = 128;
	const AudioSetup r = parse(serialize(s));
	assert(r.driver == Driver::Alsa && r.output == s.output && r.input == s.input && r.sampleRate == 96000 && r.bufferSize == 128);

	// parse garbage: defaults, no crash
	const AudioSetup g = parse("driver=banana\nsampleRate=-5\nbufferSize=abc\n\x01\x02 junk without equals\n");
	const AudioSetup d = defaultSetup();
	assert(g.driver == d.driver && g.sampleRate == d.sampleRate && g.bufferSize == d.bufferSize && g.output.empty());
	assert(parse("").sampleRate == 48000);

	// nearest
	assert(nearest({44100, 48000, 96000}, 88200) == 96000);
	assert(nearest({44100, 48000}, 44100) == 44100);
	assert(nearest({}, 48000) == 48000);

	const std::vector<DeviceInfo> devices = {
	    dev("Built-in Audio", 2, 2, {44100, 48000}),
	    dev("Scarlett 2i2: USB Audio (hw:2,0)", 2, 2, {44100, 48000, 88200, 96000}),
	};

	// The saved device exists and supports everything: kept, no notice
	Choice c = chooseSetup(s, devices);
	assert(c.setup.output == s.output && c.setup.sampleRate == 96000 && c.setup.bufferSize == 128 && c.notice.empty());

	// missing output device: default output, input off, a notice naming the card
	AudioSetup gone = s;
	gone.output = gone.input = "USB Thing";
	c = chooseSetup(gone, devices);
	assert(c.setup.output.empty() && c.setup.input.empty());
	assert(c.notice.find("USB Thing") != std::string::npos);

	// Unsupported rate → nearest the device supports; odd buffer → nearest allowed
	AudioSetup odd = s;
	odd.output = "Built-in Audio";
	odd.input.clear();
	odd.sampleRate = 96000;
	odd.bufferSize = 300;
	c = chooseSetup(odd, devices);
	assert(c.setup.sampleRate == 48000 && c.setup.bufferSize == 256);

	// PipeWire/JACK: the server decides the device; the setup passes through
	AudioSetup jack = defaultSetup();
	jack.driver = Driver::Jack;
	c = chooseSetup(jack, {});
	assert(c.setup.driver == Driver::Jack && c.notice.empty());

	// Config path override and file round trip
	setenv("PATCHCORE_AUDIO_CONFIG", "/tmp/jxmp-tests/audio-test.conf", 1);
	assert(configPath() == "/tmp/jxmp-tests/audio-test.conf");
	assert(saveSetup(s));
	assert(loadSetup().output == s.output);

	// Latency: 256 samples at 48 kHz ≈ 5.3 ms
	AudioSetup lat = defaultSetup();
	assert(latencyMs(lat) > 5.3 && latencyMs(lat) < 5.4);

	std::printf("audio settings: all tests passed\n");
	return 0;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `bash src/patchcore/tests/run.sh`
Expected: FAIL, `AudioDeviceSettings.hpp: No such file or directory`

- [ ] **Step 4: Write the implementation**

```cpp
// dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp
/*
 * PatchCore: audio device settings for the standalone — data, config file, fallback decisions.
 * No DPF or Rack dependency, tested in PatchCore src/patchcore/tests/test_audio_settings.cpp.
 * SPDX-License-Identifier: ISC (as DPF)
 */
#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace patchcore_audio {

enum class Driver { Jack, Alsa, Wasapi, Asio };

inline const char* driverKey(Driver d) {
	switch (d) {
		case Driver::Jack: return "jack";
		case Driver::Alsa: return "alsa";
		case Driver::Wasapi: return "wasapi";
		case Driver::Asio: return "asio";
	}
	return "jack";
}

inline const char* driverName(Driver d) {
	switch (d) {
		case Driver::Jack: return "PipeWire / JACK";
		case Driver::Alsa: return "ALSA";
		case Driver::Wasapi: return "WASAPI";
		case Driver::Asio: return "ASIO";
	}
	return "";
}

inline std::vector<Driver> platformDrivers() {
#ifdef _WIN32
	return {Driver::Wasapi, Driver::Asio};
#else
	return {Driver::Jack, Driver::Alsa};
#endif
}

inline bool driverFromKey(const std::string& key, Driver* out) {
	for (Driver d : platformDrivers()) {
		if (key == driverKey(d)) {
			*out = d;
			return true;
		}
	}
	return false;
}

/** What to open. `output` empty = the system default output; `input` empty = no audio input. */
struct AudioSetup {
	Driver driver;
	std::string output;
	std::string input;
	unsigned sampleRate;
	unsigned bufferSize;
};

inline AudioSetup defaultSetup() {
	return AudioSetup{platformDrivers()[0], "", "", 48000, 256};
}

struct DeviceInfo {
	std::string name;
	unsigned outputs;
	unsigned inputs;
	std::vector<unsigned> sampleRates;
};

/** The setup to open, and a message for the user when it differs from the saved one. */
struct Choice {
	AudioSetup setup;
	std::string notice;
};

static const unsigned kBufferSizes[6] = {64, 128, 256, 512, 1024, 2048};

inline unsigned nearest(const std::vector<unsigned>& options, unsigned wanted) {
	if (options.empty())
		return wanted;
	unsigned best = options[0];
	for (unsigned o : options) {
		const unsigned dBest = best > wanted ? best - wanted : wanted - best;
		const unsigned dO = o > wanted ? o - wanted : wanted - o;
		if (dO < dBest || (dO == dBest && o > best))
			best = o;
	}
	return best;
}

inline const DeviceInfo* findDevice(const std::vector<DeviceInfo>& devices, const std::string& name) {
	for (const DeviceInfo& d : devices)
		if (d.name == name)
			return &d;
	return nullptr;
}

inline Choice chooseSetup(const AudioSetup& saved, const std::vector<DeviceInfo>& devices) {
	Choice c{saved, ""};
	c.setup.bufferSize = nearest(std::vector<unsigned>(kBufferSizes, kBufferSizes + 6), saved.bufferSize);
	// PipeWire/JACK: the server owns the device and may keep its own rate
	if (saved.driver == Driver::Jack)
		return c;

	const DeviceInfo* out = saved.output.empty() ? nullptr : findDevice(devices, saved.output);
	if (!saved.output.empty() && out == nullptr) {
		c.notice = "Sound card " + saved.output + " not found, using the default output";
		c.setup.output.clear();
	}
	if (!saved.input.empty() && findDevice(devices, saved.input) == nullptr) {
		if (c.notice.empty())
			c.notice = "Input " + saved.input + " not found, audio input is off";
		c.setup.input.clear();
	}
	if (out != nullptr)
		c.setup.sampleRate = nearest(out->sampleRates, saved.sampleRate);
	return c;
}

inline std::string serialize(const AudioSetup& s) {
	std::ostringstream o;
	o << "driver=" << driverKey(s.driver) << "\n"
	  << "output=" << s.output << "\n"
	  << "input=" << s.input << "\n"
	  << "sampleRate=" << s.sampleRate << "\n"
	  << "bufferSize=" << s.bufferSize << "\n";
	return o.str();
}

inline bool parseUnsigned(const std::string& v, unsigned lo, unsigned hi, unsigned* out) {
	if (v.empty() || v.size() > 7)
		return false;
	unsigned n = 0;
	for (char ch : v) {
		if (ch < '0' || ch > '9')
			return false;
		n = n * 10 + (unsigned) (ch - '0');
	}
	if (n < lo || n > hi)
		return false;
	*out = n;
	return true;
}

/** Unknown keys and bad values are ignored (the default stays). */
inline AudioSetup parse(const std::string& text) {
	AudioSetup s = defaultSetup();
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
		if (k == "driver")
			driverFromKey(v, &s.driver);
		else if (k == "output")
			s.output = v;
		else if (k == "input")
			s.input = v;
		else if (k == "sampleRate")
			parseUnsigned(v, 8000, 384000, &s.sampleRate);
		else if (k == "bufferSize")
			parseUnsigned(v, 16, 8192, &s.bufferSize);
	}
	return s;
}

/** Next to Cardinal's own settings (the folder rack::asset::config uses), overridable for tests. */
inline std::string configPath() {
	if (const char* const p = std::getenv("PATCHCORE_AUDIO_CONFIG"))
		return p;
#ifdef _WIN32
	const char* const appdata = std::getenv("APPDATA");
	return std::string(appdata ? appdata : ".") + "\\Cardinal\\PatchCoreAudio.conf";
#else
	if (const char* const xdg = std::getenv("XDG_CONFIG_HOME"))
		return std::string(xdg) + "/Cardinal/PatchCoreAudio.conf";
	const char* const home = std::getenv("HOME");
	return std::string(home ? home : ".") + "/.config/Cardinal/PatchCoreAudio.conf";
#endif
}

inline AudioSetup loadSetup() {
	std::ifstream f(configPath().c_str());
	if (!f)
		return defaultSetup();
	std::stringstream ss;
	ss << f.rdbuf();
	return parse(ss.str());
}

inline bool saveSetup(const AudioSetup& s) {
	std::ofstream f(configPath().c_str(), std::ios::trunc);
	if (!f)
		return false;
	f << serialize(s);
	return (bool) f;
}

inline double latencyMs(const AudioSetup& s) {
	return s.sampleRate > 0 ? 1000.0 * s.bufferSize / s.sampleRate : 0.0;
}

} // namespace patchcore_audio
```

Note: in Step 4 verify that `configPath()`'s default folder equals what `rack::asset::config("")` returns
in the standalone (print both once in a debug run; adjust the default if Cardinal uses another folder).

- [ ] **Step 5: Run test to verify it passes**

Run: `bash src/patchcore/tests/run.sh`
Expected: `audio settings: all tests passed`

- [ ] **Step 6: Commit**

```bash
git add dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp src/patchcore/tests/test_audio_settings.cpp
git commit -m "Audio settings: setup data, config file and fallback decisions (tested)"
```

### Task 6: Driver choice at start-up

**Files:**
- Modify: `dpf/distrho/src/jackbridge/JackBridge.cpp` (`jackbridge_client_open`, ~line 940)
- Modify: `dpf/distrho/src/jackbridge/RtAudioBridge.hpp` (API defines, lines 26–46)

**Interfaces:**
- Consumes: `patchcore_audio::loadSetup()`, `Driver`.
- Produces: a process-wide `patchcore_audio::AudioSetup gPatchCoreSetup` and `std::string gPatchCoreNotice`
  (defined in `JackBridge.cpp`, declared `extern` in `AudioDeviceSettings.hpp` under
  `#ifdef PATCHCORE_AUDIO_GLOBALS`), read by Tasks 7–9.

- [ ] **Step 1: Compile ALSA (Linux) and WASAPI + ASIO (Windows) into RtAudio**

Replace the Linux branch of the API defines so ALSA is always available and PulseAudio is not used:

```cpp
#elif defined(DISTRHO_OS_WINDOWS) && !defined(_MSC_VER)
# define __WINDOWS_WASAPI__
# define __WINDOWS_MM__
# ifdef PATCHCORE_HAVE_ASIO        // PatchCore: ASIO added in Task 11
#  define __WINDOWS_ASIO__
# endif
# define RTAUDIO_API_TYPE WINDOWS_WASAPI
# define RTMIDI_API_TYPE WINDOWS_MM
#else
# if defined(HAVE_ALSA)            // PatchCore: ALSA direct, no PulseAudio fallback
#  define RTAUDIO_API_TYPE LINUX_ALSA
# endif
# ifdef HAVE_ALSA
#  define __LINUX_ALSA__
#  define RTMIDI_API_TYPE LINUX_ALSA
# endif
#endif
```

- [ ] **Step 2: Skip JACK when a native driver is saved**

At the start of `jackbridge_client_open` (non-direct branch):

```cpp
    // PatchCore: the saved driver decides; PipeWire/JACK first only when it is the chosen driver
    gPatchCoreSetup = patchcore_audio::loadSetup();
    const bool wantJack = gPatchCoreSetup.driver == patchcore_audio::Driver::Jack;
   #ifndef DISTRHO_OS_WASM
    if (wantJack && getBridgeInstance().client_open_ptr != nullptr)
        if (jack_client_t* const client = getBridgeInstance().client_open_ptr(client_name, static_cast<jack_options_t>(options), status))
            return client;
   #endif
```

If JACK was wanted but is not running, the native bridge opens (as today) and `gPatchCoreNotice` is set to
`"PipeWire / JACK is not running, using " + driverName(native driver)`.

- [ ] **Step 3: Build and check both start paths**

Run: `make -j12 jack`.
`printf 'driver=alsa\n' > $S/a.conf; PATCHCORE_AUDIO_CONFIG=$S/a.conf cap.sh $S/alsa.png` → log line
`RtAudio … ALSA` (add `d_stdout("PatchCore audio: %s", driverName(...))` in the open path) and audio plays
through ALSA's `default` device (PipeWire's ALSA plugin; exclusive `hw:` devices are tested in Task 9).
Without the config file: JACK client "PatchCore" appears in `pw-link -o`.

- [ ] **Step 4: Commit**

```bash
git add dpf/distrho/src/jackbridge/JackBridge.cpp dpf/distrho/src/jackbridge/RtAudioBridge.hpp dpf/distrho/src/jackbridge/AudioDeviceSettings.hpp
git commit -m "Audio: the saved driver decides between PipeWire/JACK and ALSA at start"
```

### Task 7: Device, rate and buffer for the native driver (live reopen)

**Files:**
- Modify: `dpf/distrho/src/jackbridge/NativeBridge.hpp` (add `sampleRateCallback`, virtuals)
- Modify: `dpf/distrho/src/jackbridge/RtAudioBridge.hpp` (`_open`, new `listDevices`, `reopen`)
- Modify: `dpf/distrho/src/jackbridge/JackBridge.cpp` (`jackbridge_set_sample_rate_callback` native
  branch, standalone utils)
- Modify: `dpf/distrho/DistrhoStandaloneUtils.hpp`, `dpf/distrho/src/DistrhoUtils.cpp` (stubs for plugins)

**Interfaces:**
- Produces (namespace DISTRHO, `DistrhoStandaloneUtils.hpp`):
  ```cpp
  std::vector<patchcore_audio::DeviceInfo> getAudioDevices(patchcore_audio::Driver driver);
  patchcore_audio::AudioSetup getAudioSetup();      // what is running now (real rate/buffer)
  std::string getAudioNotice();                       // start-up / error message, "" if none
  bool requestAudioSetup(const patchcore_audio::AudioSetup& setup, std::string* error);
  //   same driver: live reopen; different driver: returns false with *error = "restart" (Task 9 restarts)
  bool isAudioRunning();                              // false after a device was lost
  ```

- [ ] **Step 1: NativeBridge additions**

```cpp
    // PatchCore: sample rate changes reach the plugin like JACK's callback does
    JackSampleRateCallback sampleRateCallback = nullptr;
    void* jackSampleRateArg = nullptr;
    virtual std::vector<patchcore_audio::DeviceInfo> listDevices() { return {}; }
    virtual bool reopen(const patchcore_audio::AudioSetup&, std::string* error) { if (error) *error = "unsupported"; return false; }
    volatile bool running = true;
```

In `jackbridge_set_sample_rate_callback`, add the native branch:

```cpp
    if (usingNativeBridge) {   // PatchCore
        nativeBridge->sampleRateCallback = srate_callback;
        nativeBridge->jackSampleRateArg = arg;
        return true;
    }
```

- [ ] **Step 2: RtAudioBridge — open the chosen device**

Add members `patchcore_audio::AudioSetup setup = gPatchCoreSetup;` and use it in `_open`:

```cpp
        // PatchCore: the chosen device by name (empty = default), rate and buffer
        auto deviceId = [&](const std::string& name, bool output) -> unsigned {
            const unsigned n = rtAudio->getDeviceCount();
            for (unsigned i = 0; i < n; ++i) {
                const RtAudio::DeviceInfo info = rtAudio->getDeviceInfo(i);
                if (info.probed && info.name == name && (output ? info.outputChannels : info.inputChannels) > 0)
                    return i;
            }
            return output ? rtAudio->getDefaultOutputDevice() : rtAudio->getDefaultInputDevice();
        };
```

- `outParams.deviceId = setup.output.empty() ? rtAudio->getDefaultOutputDevice() : deviceId(setup.output, true);`
- input: `withInput = !setup.input.empty()`; `inParams.deviceId = deviceId(setup.input, false)`.
- `nextBufferSize = setup.bufferSize;` and the `48000` in `openStream` becomes `setup.sampleRate`.
- Remove `RTAUDIO_ALSA_USE_DEFAULT` when a named ALSA device is chosen (keep it for the default).
- After a successful open set `setup.sampleRate = sampleRate; setup.bufferSize = bufferSize;` (real values).
- The RtAudio error callback (`openStream`'s last argument, today `nullptr`) sets `running = false` on
  `RTAUDIO_DEVICE_DISCONNECT` / stream errors.

- [ ] **Step 3: `listDevices()` and `reopen()`**

```cpp
    std::vector<patchcore_audio::DeviceInfo> listDevices() override
    {
        std::vector<patchcore_audio::DeviceInfo> out;
        RtAudio* const rt = handle.get();
        if (rt == nullptr) return out;
        const unsigned n = rt->getDeviceCount();
        for (unsigned i = 0; i < n; ++i) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(i);
            if (!info.probed) continue;
            out.push_back({info.name, info.outputChannels, info.inputChannels, info.sampleRates});
        }
        return out;
    }

    bool reopen(const patchcore_audio::AudioSetup& wanted, std::string* const error) override
    {
        const patchcore_audio::AudioSetup previous = setup;
        const uint previousRate = sampleRate;
        deactivate();
        close();
        setup = wanted;
        bool ok = _open(!wanted.input.empty());
        if (!ok) {
            if (error) *error = "Could not open " + (wanted.output.empty() ? std::string("the default output") : wanted.output);
            setup = previous;
            _open(!previous.input.empty());
        }
        if (bufferSizeCallback != nullptr)
            bufferSizeCallback(bufferSize, jackBufferSizeArg);
        if (sampleRate != previousRate && sampleRateCallback != nullptr)
            sampleRateCallback(sampleRate, jackSampleRateArg);
        running = true;
        activate();
        return ok;
    }
```

`requestBufferSizeChange` becomes `{ patchcore_audio::AudioSetup s = setup; s.bufferSize = n; return reopen(s, nullptr); }`.

- [ ] **Step 4: Standalone utils**

In `JackBridge.cpp` (inside `#ifndef JACKBRIDGE_SKIP_NATIVE_UTILS`):

```cpp
std::vector<patchcore_audio::DeviceInfo> getAudioDevices(const patchcore_audio::Driver driver)
{
    if (usingNativeBridge && driver == gPatchCoreSetup.driver)
        return nativeBridge->listDevices();
    return {};   // PipeWire/JACK: the server owns the devices; another native API: listed after a restart
}

patchcore_audio::AudioSetup getAudioSetup()
{
    patchcore_audio::AudioSetup s = gPatchCoreSetup;
    if (usingNativeBridge) {
        s = static_cast<RtAudioBridge*>(nativeBridge)->setup;
    } else if (usingRealJACK) {
        s.driver = patchcore_audio::Driver::Jack;
    }
    return s;
}

std::string getAudioNotice() { return gPatchCoreNotice; }

bool requestAudioSetup(const patchcore_audio::AudioSetup& setup, std::string* const error)
{
    const patchcore_audio::Driver current = usingRealJACK ? patchcore_audio::Driver::Jack : gPatchCoreSetup.driver;
    if (setup.driver != current) {
        patchcore_audio::saveSetup(setup);
        if (error) *error = "restart";
        return false;
    }
    if (usingRealJACK) {
        patchcore_audio::saveSetup(setup);
        return jackbridge_set_buffer_size(gJackClient, setup.bufferSize);   // PipeWire honours the quantum
    }
    const bool ok = nativeBridge->reopen(setup, error);
    if (ok)
        patchcore_audio::saveSetup(getAudioSetup());
    return ok;
}

bool isAudioRunning() { return !usingNativeBridge || nativeBridge->running; }
```

(`gJackClient`: store the client returned by `jackbridge_client_open` in a static.) Add no-op versions in
`DistrhoUtils.cpp` next to the existing stubs (`return {}` / `false` / `""`), so plugin builds link.

- [ ] **Step 5: Build and test by hand**

Run: `make -j12 jack`. With `PATCHCORE_AUDIO_CONFIG=$S/a.conf` (driver=alsa) start the showcase patch,
then (temporary debug env `PATCHCORE_DEV_AUDIO_RATE=44100` handled in `devStep()` at frame 200 by calling
`requestAudioSetup` — remove it before committing) capture: the bar shows `44.1 kHz`, the JX-OSC pitch in a
Scope/tuner module stays the same (the engine got the new rate). Check `bin/Cardinal` keeps running.

- [ ] **Step 6: Commit**

```bash
git add dpf/distrho
git commit -m "Audio: choose device, sample rate and buffer for ALSA/WASAPI, live reopen with revert"
```

### Task 8: PipeWire/JACK — connect the outputs in the app

**Files:**
- Modify: `dpf/distrho/src/DistrhoPluginJACK.cpp` (after `jackbridge_activate`)
- Modify: `run-patchcore.sh` (drop the `pw-link` block)

- [ ] **Step 1: Autoconnect after activation (real JACK only)**

```cpp
        // PatchCore: connect the first two outputs to the system playback, as other apps do under PipeWire
        if (! jackbridge_is_ok_native())   // real JACK client
        {
            if (const char** const ports = jackbridge_get_ports(fClient, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                                JackPortIsPhysical | JackPortIsInput))
            {
                for (int i = 0; i < 2 && i < DISTRHO_PLUGIN_NUM_OUTPUTS && ports[i] != nullptr; ++i)
                    jackbridge_connect(fClient, jackbridge_port_name(fPortAudioOuts[i]), ports[i]);
                jackbridge_free(ports);
            }
        }
```

(`jackbridge_is_ok_native()`: a tiny helper in `JackBridge.cpp` returning `usingNativeBridge`.)

- [ ] **Step 2: Remove the linking from `run-patchcore.sh`** (the `pw-link`/`pactl` block and its help
  lines); the script only launches.

- [ ] **Step 3: Verify**: `run-patchcore.sh demo` on workspace 3 → `pw-link -l | grep PatchCore` shows the
  outputs connected to the default sink; the user hears the demo.

- [ ] **Step 4: Commit**

```bash
git add dpf/distrho/src/DistrhoPluginJACK.cpp dpf/distrho/src/jackbridge/JackBridge.cpp run-patchcore.sh
git commit -m "Audio: PatchCore connects its outputs under PipeWire/JACK itself"
```

### Task 9: Audio Settings dialog, menu, restart on driver change, start-up notice

**Files:**
- Create: `src/patchcore/AudioSettings.cpp`
- Modify: `src/patchcore/PatchCore.hpp` (`void showAudioSettings();`, `void showNotice(std::string)`)
- Modify: `src/override/MenuBar.cpp` (Engine menu)
- Modify: `src/patchcore/TopBar.cpp` (notice strip)
- Modify: `src/CardinalUI.cpp` (restore after restart)
- Modify: `src/Makefile.cardinal.mk` or the file list that compiles `src/patchcore/*.cpp` (add the new file)

**Interfaces:**
- Consumes: Task 7 utils; `patchcore_audio::*`.
- Produces: `rack::patchcore::showAudioSettings()`, `rack::patchcore::showNotice(const std::string&)`,
  `rack::patchcore::restartApp()`.

- [ ] **Step 1: Menu**

In `MenuBar.cpp` Engine menu replace the `isUsingNativeAudio()` block's "Enable Audio Input" and "Buffer
Size" items with (keep "Enable/Reconnect MIDI"):

```cpp
		if (isStandalone())
			menu->addChild(createMenuItem("Audio Settings…", "", []() { patchcore::showAudioSettings(); }));
```

- [ ] **Step 2: The dialog widget**

`AudioSettings.cpp`: an `OpaqueWidget` (420 × 330 px) centred over the scene, drawn with
`draw::bevelBox`/`theme()` like the quick search, closing on Esc or Close. Rows (label left, control right):

| Row | Control | Values |
|---|---|---|
| Driver | dropdown (`ui::Menu` on click) | `platformDrivers()` → `driverName()` |
| Output | dropdown | "System default" + `getAudioDevices(driver)` with `outputs > 0` |
| Input | dropdown | "Off" + devices with `inputs > 0` |
| Sample rate | dropdown | the output device's `sampleRates` ∩ {44100, 48000, 88200, 96000}; PipeWire/JACK: those four |
| Buffer | dropdown | `kBufferSizes` |
| Latency | text | `string::f("%.1f ms", latencyMs(edit))` |
| buttons | Test tone · ASIO panel… (ASIO only) · Apply · Close | |
| error | red text line | last error |

State: `patchcore_audio::AudioSetup edit = getAudioSetup();` changed by the dropdowns.

Apply:

```cpp
	void apply() {
		std::string error;
		if (requestAudioSetup(edit, &error)) {
			this->error.clear();
			return;
		}
		if (error == "restart") {
			async_dialog_message("PatchCore restarts to switch the audio driver. The patch stays open.",
			                     []() { restartApp(); });
			return;
		}
		this->error = error;
		edit = getAudioSetup();   // back to what runs
	}
```

Test tone: a `std::atomic<int> testToneFrames` in `CardinalPluginContext` (add the field in
`src/CardinalPluginContext.hpp` / `include/plugincontext.hpp` next to `outputPeaks`); `CardinalPlugin.cpp`
`run()` adds a 440 Hz sine at −12 dBFS to outputs 0/1 while it is > 0 (0.5 s), after `stepBlock` (CRLF!).
ASIO panel: `showAsioPanel()` util (Task 11), hidden on Linux.

- [ ] **Step 3: Restart on a driver change**

```cpp
void restartApp() {
	const std::string tmp = system::join(asset::user(""), "patchcore-restart.vcv");
	const std::string original = APP->patch->path;
	APP->patch->save(tmp);
#ifdef ARCH_WIN
	// re-launch ourselves with the patch, then quit
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	SetEnvironmentVariableA("PATCHCORE_RESTORE_PATH", original.c_str());
	const std::wstring cmd = L"\"" + std::wstring(exe) + L"\" \"" + string::UTF8toUTF16(tmp) + L"\"";
	STARTUPINFOW si = {sizeof(si)};
	PROCESS_INFORMATION pi;
	if (CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		APP->window->close();
#else
	setenv("PATCHCORE_RESTORE_PATH", original.c_str(), 1);
	char exe[4096];
	const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		execl(exe, exe, tmp.c_str(), (char*) nullptr);
	}
#endif
}
```

After the patch loads (where the standalone loads its argument in `CardinalUI.cpp`), if
`PATCHCORE_RESTORE_PATH` is set: `APP->patch->path = value` (empty → untitled), `system::remove(tmp)`,
`unsetenv`. Unsaved-changes state is kept by marking the patch modified when it was modified before
(pass `PATCHCORE_RESTORE_MODIFIED=1`).

- [ ] **Step 4: Start-up notice**

`TopBar`: a slim amber strip under the top bar with the text of `getAudioNotice()` (if not empty) and a
close ×; also `showNotice()` for other messages. Hidden in plugins (the util returns "").

- [ ] **Step 5: Verify (Review Focus 1–3)**

- Saved missing card: `printf 'driver=alsa\noutput=USB Thing\n' > $S/m.conf` → start → amber notice
  "Sound card USB Thing not found, using the default output", sound plays.
- Rate change: dialog → 44.1 kHz → Apply → bar shows 44.1 kHz, pitch unchanged.
- Driver switch: open showcase, move a knob (modified), Driver → PipeWire/JACK → Apply → OK → app
  restarts with the same patch, same file name in the title, still marked modified; the JACK client is
  connected.
- Captures with `cap.sh` (`PATCHCORE_DEV_AUDIO_DIALOG=1` opens the dialog at frame 3; keep this dev hook,
  like `PATCHCORE_DEV_MIXER`).

- [ ] **Step 6: Commit**

```bash
git add src/patchcore src/override/MenuBar.cpp src/CardinalUI.cpp src/CardinalPlugin.cpp include/plugincontext.hpp src/Makefile.cardinal.mk
git commit -m "Audio Settings dialog: driver, devices, rate, buffer, test tone; restart on driver change; start-up notice"
```

### Task 10: Real engine info and "Audio stopped"

**Files:**
- Modify: `src/patchcore/PatchCoreUI.cpp` (right-hand info text of the standalone bar)

- [ ] **Step 1:** Replace the info text for the standalone:

```cpp
		const patchcore_audio::AudioSetup a = getAudioSetup();
		std::string rate = string::f("%g kHz  ·  %u smp  ·  %s", context->sampleRate / 1000.0, context->bufferSize,
		                             patchcore_audio::driverName(a.driver));
		if (!a.output.empty())
			rate += ": " + a.output;
		if (!isAudioRunning())
			rate = "Audio stopped  ·  Engine → Audio Settings…";
```

Long device names are cut with "…" to 260 px (`nvgTextBounds` loop). The "Audio stopped" line is drawn in
`t.rec` red.

- [ ] **Step 2: Verify** by capture (info text) and by unplugging a USB card while ALSA plays (user, or
  skip if no USB card) — no crash, red text, Apply in the dialog brings sound back.

- [ ] **Step 3: Commit**

```bash
git add src/patchcore/PatchCoreUI.cpp
git commit -m "Transport bar: real driver/device info, Audio stopped notice"
```

### Task 11: Windows — WASAPI and ASIO

**Files:**
- Create: `dpf/distrho/src/jackbridge/rtaudio/asio/` (ASIO SDK GPLv3 sources: `asio.cpp`, `asiodrivers.cpp`,
  `asiolist.cpp`, `iasiothiscallresolver.cpp` and headers, from RtAudio 5.1.0's `include/` + Steinberg ASIO
  SDK 2.3.4 GPLv3 release; licence files alongside)
- Modify: `dpf/Makefile.plugins.mk` (Windows: `-DPATCHCORE_HAVE_ASIO`, compile the ASIO sources, `-lole32`)
- Modify: `dpf/distrho/src/jackbridge/JackBridge.cpp` (`showAsioPanel()` util)

- [ ] **Step 1:** Add the sources and licence; `PATCHCORE_HAVE_ASIO` on Windows builds.
- [ ] **Step 2:** `RtAudioBridge` opens `RtAudio(gPatchCoreSetup.driver == Driver::Asio ? RtAudio::WINDOWS_ASIO
  : RtAudio::WINDOWS_WASAPI)`.
- [ ] **Step 3:** `showAsioPanel()` → `ASIOControlPanel()` when the driver is ASIO.
- [ ] **Step 4: Build check**: `command -v x86_64-w64-mingw32-g++` in the distrobox. If present:
  `make -j12 jack WINDOWS=true CROSS_COMPILING=true` (Cardinal's documented mingw build) and check that
  `bin/Cardinal.exe` links. If not present: report it; a Windows build runs only through GitHub Actions
  **after the user asks**.
- [ ] **Step 5: Commit**

```bash
git add dpf
git commit -m "Audio on Windows: WASAPI and ASIO (ASIO SDK under GPLv3)"
```

### Task 12: Docs and handoff

**Files:**
- Modify: `research_docs/STATUS.md`, `research_docs/12-standalone-plugin-audio-design.md` (status
  "implemented", note the restart on driver change), `README.md` of PatchCore (build: `make vst3` gives
  PatchCore and PatchCore FX; Audio Settings), the collection's `README.md` row if the description changes,
  `web/index.html` PatchCore card (standalone + VST3).

- [ ] **Step 1:** Update the docs as listed.
- [ ] **Step 2:** Run all tests: `bash src/patchcore/tests/run.sh && bash plugins/JXMP/tests/run.sh`.
- [ ] **Step 3:** Commit, push PatchCore, bump the submodule in PluginCollection, push.

```bash
git add -A research_docs README.md && git commit -m "Docs: standalone vs VST3, audio settings"
git push origin <branch>
```
