# JXMP voice modules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Four JXMP modules — VCA (JX-AMP), ADSR (JX-ENV), VCO (JX-OSC), VCF (JX-FLT) — with all sockets on the back, a uniform JXMP panel, a serial plate on the back, and the extensions from the spec.

**Architecture:** A shared base (`plugins/JXMP/src/JXMPWidget.hpp`) draws the uniform panel and implements `SidedModuleWidget` (all ports back, serial label). New DSP features are pure headers in `plugins/JXMP/src/dsp/` tested by `plugins/JXMP/tests/run.sh`; oscillator and ladder cores are copied from `plugins/Fundamental/src` with attribution. Each module is one `.cpp` in `plugins/JXMP/src/`, registered in `plugins/plugins.cpp` and `plugins/JXMP/plugin.json`.

**Tech Stack:** C++11 (Cardinal's standard), VCV Rack 2 API, NanoVG, `g++` test runner.

**Spec:** [06-jxmp-voice-modules-design.md](06-jxmp-voice-modules-design.md)

## Global Constraints

- Naming rule: front title `JX-AMP` / `JX-ENV` / `JX-OSC` / `JX-FLT`; `plugin.json` names `VCA`, `ADSR`, `VCO`, `VCF`; brand `JXMP Audio`; serial plate on the back `"<JX code> · S/N XXXX-XXXX"`.
- Every port of these modules is `SIDE_BACK`. Front = knobs, switches, buttons, lights only.
- Panel colours: JX-OSC `#ee7b1f`, JX-FLT `#3a8dde`, JX-AMP `#3cb46e`, JX-ENV `#f2c230`, JX-PB `#8a8f98`.
- Copied Fundamental code keeps a header: `Based on VCV Fundamental (GPL-3.0-or-later), <file>, Copyright (C) VCV and contributors.`
- No third-party trademarks in names, panels or text.
- Polyphony up to 16 channels; channel count follows the main input (VCA: IN; ENV: GATE; OSC: V/OCT; FLT: IN).
- Tests compile with `-std=c++11` (change `plugins/JXMP/tests/run.sh` from c++17 in Task 1). Build `make -j12 jack` → `bin/Cardinal`.
- Internal structs in an anonymous namespace (all plugins link into one binary).
- Commit messages end with the session's `Co-Authored-By` / `Claude-Session` lines.

## Review Focus

1. **Polyphony edges**: 0-channel (disconnected) main input → outputs mono 0 V, no NaN; 16 channels with unison 7 stays real-time (JX-OSC) — Task 4 Step 6 measures CPU via the module CPU meter.
2. **Extreme knob/CV values**: resonance max with drive max (JX-FLT), attack 0 / release 10 s (JX-ENV), detune max with 7 voices — no NaN/inf, outputs bounded (±12 V) — tests in Tasks 3–5.
3. **Save/reload**: every param restores; switches keep their position. Task 6.
4. **Bypass**: `configBypass` routes main in → main out where it makes sense (VCA, VCF).
5. **Browser preview** (no module): widgets must not dereference `module` — Task 1 base handles `module == NULL`.

---

### Task 1: Common base and serial plate

**Files:**
- Modify: `include/patchcore/SidedModuleWidget.hpp` (+ `serialLabel()`), `src/patchcore/BackPanel.cpp` (draw it)
- Create: `plugins/JXMP/src/Serial.hpp`, `plugins/JXMP/src/JXMPWidget.hpp`, `plugins/JXMP/tests/test_jxmp_base.cpp`
- Create: `plugins/JXMP/res/Panel6HP.svg`, `Panel10HP.svg`, `Panel14HP.svg` (same style as `Panel4HP.svg`)
- Modify: `plugins/JXMP/src/Patchbay.cpp` (serial label), `plugins/JXMP/tests/run.sh` (`-std=c++11`)

**Interfaces:**
- Produces: `std::string jxmp::serialNumber(int64_t moduleId)`; `virtual std::string patchcore::SidedModuleWidget::serialLabel() const` (default `""`); `struct JXMPModuleWidget : app::ModuleWidget, patchcore::SidedModuleWidget` with ctor-time `setup(engine::Module*, const char* code, NVGcolor stripe, int hp)` and helpers `label(math::Vec mmPos, const char* text, float size = 7.f)`; `JXMPModuleWidget::serialLabel()` returns `code + " · S/N " + serialNumber(module ? module->id : -1)`.

- [ ] **Step 1: Test** `plugins/JXMP/tests/test_jxmp_base.cpp`:

```cpp
#include "Serial.hpp"
#include <cassert>
#include <cstdio>

int main() {
	assert(jxmp::serialNumber(-1) == "----");
	const std::string a = jxmp::serialNumber(1234567890123LL);
	assert(a == jxmp::serialNumber(1234567890123LL));
	assert(a.size() == 9 && a[4] == '-');
	for (size_t i = 0; i < a.size(); ++i)
		assert(i == 4 || (a[i] >= '0' && a[i] <= '9') || (a[i] >= 'A' && a[i] <= 'F'));
	assert(jxmp::serialNumber(1) != jxmp::serialNumber(2));
	std::puts("jxmp base: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL** (`Serial.hpp` missing).

- [ ] **Step 3: Implement** `Serial.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace jxmp {

/** Serial number shown on the back plate: XXXX-XXXX hex from the module id (mixed, so neighbours differ). */
inline std::string serialNumber(int64_t moduleId) {
	if (moduleId < 0)
		return "----";
	uint64_t x = (uint64_t) moduleId;
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%04X-%04X", (unsigned) ((x >> 16) & 0xffff), (unsigned) (x & 0xffff));
	return buf;
}

} // namespace jxmp
```

Add to `SidedModuleWidget` (needs `#include <string>`):

```cpp
	/** Text for the serial plate on the generated back. Empty: the generic sticker. */
	virtual std::string serialLabel() const {
		return "";
	}
```

`BackPanel.cpp`, in `drawBackPanel` where the sticker is drawn: if the widget is a `SidedModuleWidget` with a non-empty `serialLabel()`, draw instead a brushed-metal plate (rounded rect, light grey gradient, 0.6 px dark border, two tiny rivets) near the bottom, sized to the text (mono font, 7 px, uppercase, dark grey), rotated vertically when the module is narrower than the text. Keep the generic sticker for everything else.

`JXMPWidget.hpp`: `JXMPModuleWidget` loads `res/Panel<hp>HP.svg` with `createPanel`, adds a `JXMPPanelDecor` `TransparentWidget` child (box = panel size) that draws the stripe (full width, 3 mm high at y = 14 mm, the module colour), the title (centred at y = 8 mm, 11 px, uppercase, `#e8eaee`), and `JXMP AUDIO` at y = 123 mm (7 px, `#8a8f98`), plus every `label()` registered by the module (7 px, `#c8ccd2`, centred). Text uses `asset::system("res/fonts/DejaVuSans.ttf")`.

Patchbay: `PatchbayWidget::serialLabel()` → `string::f("JX-PB%d · S/N ", N) + jxmp::serialNumber(module ? module->id : -1)`.

Panels: copy `Panel4HP.svg` to 6/10/14 HP (`width` = hp × 5.08 mm, viewBox, rects scaled).

- [ ] **Step 4: Run tests, expect** `jxmp base: all tests passed` (and the patchbay tests); build; screenshot the back of the Patchbay test patch: serial plates `JX-PB4 · S/N …`.

- [ ] **Step 5: Commit** `git commit -m "JXMP: serial plate on the back, shared panel base"`.

---

### Task 2: JX-AMP (VCA)

**Files:** Create `plugins/JXMP/src/dsp/Vca.hpp`, `plugins/JXMP/src/Vca.cpp`, `plugins/JXMP/tests/test_vca.cpp`; modify `plugin.json`, `plugin.hpp`, `plugins/plugins.cpp`.

**Interfaces:** Produces `float jxmp::vcaGain(float cv01, bool exponential)`; model `modelJXMPVCA` slug `VCA`.

- [ ] **Step 1: Test** `test_vca.cpp`:

```cpp
#include "dsp/Vca.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
	assert(jxmp::vcaGain(0.f, false) == 0.f);
	assert(jxmp::vcaGain(1.f, false) == 1.f);
	assert(std::fabs(jxmp::vcaGain(0.f, true)) < 1e-6f);
	assert(std::fabs(jxmp::vcaGain(1.f, true) - 1.f) < 1e-6f);
	for (float x = 0.05f; x < 1.f; x += 0.05f)
		assert(jxmp::vcaGain(x, true) < jxmp::vcaGain(x, false));
	assert(jxmp::vcaGain(-3.f, false) == 0.f && jxmp::vcaGain(5.f, true) == 1.f);  // clamped
	std::puts("vca: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `dsp/Vca.hpp` — the same exponential law as Fundamental's VCA (base 50):

```cpp
#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

/** Gain for a 0..1 control value. Exponential uses Fundamental VCA's law: (50^x - 1) / 49. */
inline float vcaGain(float cv01, bool exponential) {
	const float x = std::min(std::max(cv01, 0.f), 1.f);
	if (!exponential)
		return x;
	return (std::pow(50.f, x) - 1.f) / 49.f;
}

} // namespace jxmp
```

`Vca.cpp`: params `LEVEL1/2` (0..1, default 1, "%"), `RESPONSE1/2` (`configSwitch` {"Linear", "Exponential"}, default 1); inputs `IN1, CV1, IN2, CV2`; outputs `OUT1, OUT2`; lights `LEVEL1/2` (level meter). Per channel `ch` of IN (≥1): `gain = level * (CV connected ? vcaGain(cv/10, exp) : 1)`; CV may be mono or poly (`getPolyVoltage(c)`). IN2 is normalled to IN1 when unconnected (spec). `configBypass(IN1, OUT1)`, `configBypass(IN2, OUT2)`. Widget: `JXMPModuleWidget` 6 HP, colour `#3cb46e`, two rows each with a big level knob (`RoundBigBlackKnob`), a `CKSS` response switch and a `SmallLight<GreenLight>`; port positions for the back: IN1/CV1/OUT1 in the upper half, IN2/CV2/OUT2 in the lower half (`PJ301MPort`, they only show on the back). Labels: `LEVEL 1`, `LIN · EXP`, `LEVEL 2`.

- [ ] **Step 4: Run tests, build, screenshot front/back** (patch: VCO → Patchbay → VCA back).
- [ ] **Step 5: Commit** `git commit -m "JXMP: JX-AMP (VCA)"`.

---

### Task 3: JX-ENV (ADSR)

**Files:** Create `plugins/JXMP/src/dsp/Envelope.hpp`, `plugins/JXMP/src/Envelope.cpp`, `plugins/JXMP/tests/test_envelope.cpp`; register.

**Interfaces:** Produces `float jxmp::curve(float x, float shape)`; `struct jxmp::Envelope { enum Mode { ADSR, AD, LOOP }; enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE }; float process(float dt, bool gate, bool retrig, bool gateConnected, float a, float d, float s, float r, float shape, Mode mode); Stage stage; float value; bool eoc; }` — `a/d/r` in seconds, `s` 0..1, `eoc` true for the sample that ends a cycle; model `modelJXMPADSR` slug `ADSR`.

- [ ] **Step 1: Test** `test_envelope.cpp`:

```cpp
#include "dsp/Envelope.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

static int run(Envelope& e, float seconds, bool gate, Envelope::Mode m, int* eocs = nullptr, bool gateConnected = true) {
	const float dt = 1.f / 1000.f;
	int n = 0;
	for (float t = 0; t < seconds; t += dt) {
		e.process(dt, gate, false, gateConnected, 0.01f, 0.02f, 0.5f, 0.05f, 0.f, m);
		assert(std::isfinite(e.value) && e.value >= -1e-4f && e.value <= 1.0001f);
		if (e.eoc)
			++n;
	}
	if (eocs)
		*eocs = n;
	return n;
}

int main() {
	for (float s : {-1.f, 0.f, 1.f}) {
		assert(std::fabs(curve(0.f, s)) < 1e-6f && std::fabs(curve(1.f, s) - 1.f) < 1e-6f);
		for (float x = 0.f; x < 0.99f; x += 0.01f)
			assert(curve(x + 0.01f, s) >= curve(x, s));
	}
	assert(curve(0.3f, 1.f) > curve(0.3f, -1.f));

	// ADSR reaches sustain, then releases to 0 with one EOC
	Envelope e;
	run(e, 0.2f, true, Envelope::ADSR);
	assert(e.stage == Envelope::SUSTAIN && std::fabs(e.value - 0.5f) < 0.01f);
	int eocs = 0;
	run(e, 0.3f, false, Envelope::ADSR, &eocs);
	assert(e.value < 0.01f && eocs == 1);

	// AD ignores an early gate-off and ends at 0 with one EOC
	Envelope ad;
	ad.process(0.001f, true, false, true, 0.01f, 0.02f, 0.5f, 0.05f, 0.f, Envelope::AD);
	run(ad, 0.2f, false, Envelope::AD, &eocs);
	assert(ad.value < 0.01f && eocs == 1);

	// LOOP with the gate held cycles several times
	Envelope lp;
	run(lp, 0.5f, true, Envelope::LOOP, &eocs);
	assert(eocs >= 10);
	// LOOP free-runs when no gate is connected
	Envelope fr;
	run(fr, 0.5f, false, Envelope::LOOP, &eocs, false);
	assert(eocs >= 10);

	// Attack 0 does not divide by zero
	Envelope z;
	z.process(0.001f, true, false, true, 0.f, 0.f, 1.f, 0.f, 0.f, Envelope::ADSR);
	assert(std::isfinite(z.value));

	std::puts("envelope: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `dsp/Envelope.hpp`:

```cpp
#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

/** Monotonic 0..1 → 0..1. shape −1 = linear, 0 = Fundamental-like exponential approach, +1 = steeper. */
inline float curve(float x, float shape) {
	x = std::min(std::max(x, 0.f), 1.f);
	const float k = 4.f * (1.f + std::min(std::max(shape, -1.f), 1.f));
	if (k < 0.01f)
		return x;
	return (1.f - std::exp(-k * x)) / (1.f - std::exp(-k));
}

struct Envelope {
	enum Mode { ADSR, AD, LOOP };
	enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

	Stage stage = IDLE;
	float value = 0.f;
	bool eoc = false;

	float process(float dt, bool gate, bool retrig, bool gateConnected, float a, float d, float s, float r, float shape, Mode mode) {
		eoc = false;
		const bool run = gate || (mode == LOOP && !gateConnected);
		if ((run && !lastRun) || retrig)
			start(ATTACK);
		lastRun = run;

		const float minTime = 1e-3f;
		switch (stage) {
		case ATTACK:
			advance(dt, std::max(a, minTime));
			value = from + (1.f - from) * curve(phase, shape);
			if (phase >= 1.f)
				start(DECAY);
			break;
		case DECAY: {
			const float target = mode == ADSR ? s : 0.f;
			advance(dt, std::max(d, minTime));
			value = from + (target - from) * curve(phase, shape);
			if (phase >= 1.f) {
				if (mode == ADSR)
					stage = SUSTAIN;
				else if (mode == LOOP && run) {
					eoc = true;
					start(ATTACK);
				}
				else {
					eoc = true;
					stage = IDLE;
					value = 0.f;
				}
			}
			break;
		}
		case SUSTAIN:
			value = s;
			break;
		case RELEASE:
			advance(dt, std::max(r, minTime));
			value = from * (1.f - curve(phase, shape));
			if (phase >= 1.f) {
				eoc = true;
				stage = IDLE;
				value = 0.f;
			}
			break;
		default:
			value = 0.f;
			break;
		}
		if (!run && mode == ADSR && (stage == ATTACK || stage == DECAY || stage == SUSTAIN))
			start(RELEASE);
		return value;
	}

private:
	float phase = 0.f;
	float from = 0.f;
	bool lastRun = false;

	void start(Stage st) {
		stage = st;
		phase = 0.f;
		from = value;
	}
	void advance(float dt, float time) {
		phase = std::min(phase + dt / time, 1.f);
	}
};

} // namespace jxmp
```

`Envelope.cpp`: params `ATTACK, DECAY, SUSTAIN, RELEASE` (same ranges and display as Fundamental: `configParam(..., 0, 1, 0.5, "Attack", " ms", LAMBDA_BASE, MIN_TIME * 1000)` with `MIN_TIME 1e-3`, `MAX_TIME 10`, time = `MIN_TIME * pow(LAMBDA_BASE, param)`), `A_CV..R_CV` attenuverters (−1..1), `CURVE` (−1..1, 0), `MODE` (`configSwitch` {"ADSR", "AD", "Loop"}), `PUSH` button; inputs `GATE, RETRIG, A_CV, D_CV, S_CV, R_CV`; outputs `ENV` (0..10 V), `INV` (10 − env), `EOC` (1 ms 10 V pulse via `dsp::PulseGenerator` per channel); lights per stage. Per channel up to 16 (`jxmp::Envelope env[16]`), retrig via `dsp::SchmittTrigger`. Widget 10 HP, colour `#f2c230`: four big knobs in a row (A D S R), four trimpots below (CV amounts), CURVE knob, MODE 3-way switch (`CKSSThree`), PUSH button, stage LEDs.

- [ ] **Step 4: Tests, build, screenshot; with xdotool hold PUSH and check the ENV output via a scope module (Fundamental Scope) in a screenshot.**
- [ ] **Step 5: Commit** `git commit -m "JXMP: JX-ENV (ADSR) with curve, AD/loop modes, INV and EOC"`.

---

### Task 4: JX-OSC (VCO)

**Files:** Create `plugins/JXMP/src/dsp/OscillatorCore.hpp` (copy of `VoltageControlledOscillator` from `plugins/Fundamental/src/VCO.cpp`, lines 1–242, with the attribution header, in `namespace jxmp`), `plugins/JXMP/src/dsp/Osc.hpp`, `plugins/JXMP/src/Osc.cpp`, `plugins/JXMP/tests/test_osc.cpp`; register.

**Interfaces:** Produces `float jxmp::morph(float sin, float tri, float saw, float sqr, float m)` (m 0..3); `int jxmp::unisonOffsets(int n, float cents, float* out)` (fills `n` offsets in cents, returns n, n clamped 1..7); `struct jxmp::SubDivider { float process(float phase, int octaves); }` (±1 square at phase/2 or /4, edge-tracked); model `modelJXMPVCO` slug `VCO`.

- [ ] **Step 1: Test** `test_osc.cpp`:

```cpp
#include "dsp/Osc.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

int main() {
	assert(morph(1, 2, 3, 4, 0.f) == 1 && morph(1, 2, 3, 4, 1.f) == 2 && morph(1, 2, 3, 4, 2.f) == 3 && morph(1, 2, 3, 4, 3.f) == 4);
	assert(std::fabs(morph(0, 10, 0, 0, 0.5f) - 5.f) < 1e-5f);
	assert(morph(1, 2, 3, 4, -1.f) == 1 && morph(1, 2, 3, 4, 9.f) == 4);  // clamped

	float o[8];
	assert(unisonOffsets(1, 30.f, o) == 1 && o[0] == 0.f);
	for (int n = 2; n <= 7; ++n) {
		assert(unisonOffsets(n, 30.f, o) == n);
		float sum = 0, mx = 0;
		for (int i = 0; i < n; ++i) { sum += o[i]; mx = std::max(mx, std::fabs(o[i])); }
		assert(std::fabs(sum) < 1e-4f && std::fabs(mx - 30.f) < 1e-4f);
	}
	assert(unisonOffsets(12, 30.f, o) == 7);

	// Sub divider: one octave down toggles once per main cycle
	SubDivider sub;
	int flips = 0;
	float last = sub.process(0.f, 1);
	for (int cyc = 0; cyc < 8; ++cyc)
		for (int i = 0; i < 100; ++i) {
			const float v = sub.process(i / 100.f, 1);
			if (v != last) ++flips;
			last = v;
		}
	assert(flips == 8);
	std::puts("osc: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `dsp/Osc.hpp`:

```cpp
#pragma once
#include <algorithm>
#include <cmath>

namespace jxmp {

inline float morph(float sin, float tri, float saw, float sqr, float m) {
	m = std::min(std::max(m, 0.f), 3.f);
	const float v[4] = {sin, tri, saw, sqr};
	const int i = std::min((int) m, 2);
	const float f = m - i;
	return v[i] + (v[i + 1] - v[i]) * f;
}

inline int unisonOffsets(int n, float cents, float* out) {
	n = std::min(std::max(n, 1), 7);
	if (n == 1) {
		out[0] = 0.f;
		return 1;
	}
	for (int i = 0; i < n; ++i)
		out[i] = cents * (2.f * i / (n - 1) - 1.f);
	return n;
}

/** Square sub-oscillator from the main phase: flips on every main phase wrap (÷2), every second wrap for ÷4. */
struct SubDivider {
	float lastPhase = 0.f;
	int count = 0;
	float process(float phase, int octaves) {
		if (phase < lastPhase)
			++count;
		lastPhase = phase;
		const int div = octaves >= 2 ? 2 : 1;
		return ((count / div) & 1) ? -1.f : 1.f;
	}
};

} // namespace jxmp
```

`Osc.cpp`: params as Fundamental VCO (`FREQ, FM, PW, PW_CV, LINEAR, SYNC`) plus `OCTAVE` (−2..2 snap), `MORPH` (0..3), `MORPH_CV` (−1..1), `SUB_LEVEL` (0..1), `SUB_OCT` (switch {"−1 oct", "−2 oct"}), `VOICES` (1..7 snap), `DETUNE` (0..50 cents); inputs `PITCH, FM, SYNC, PW, MORPH`; outputs `SIN, TRI, SAW, SQR, SUB, MIX`. Processing per poly channel `c` (channels = max(1, PITCH channels)): voices `n` with offsets `o[]`; run the copied oscillator with `T = simd::float_4` holding up to 4 voices per lane group (2 groups for n > 4), pitch per lane = base pitch + `o[v]/1200`; sum each waveform over voices × `1/sqrt(n)`; `SUB` from `SubDivider` on voice 0's phase; `MIX = morph(...) + subLevel * sub*5 V`, clamped ±12 V. Widget 14 HP, colour `#ee7b1f`: big FREQ knob, OCTAVE snap knob, FM and PW knobs + trimpots, FM-mode and sync switches, MORPH knob + trimpot, SUB level + octave switch, VOICES snap knob, DETUNE knob.

- [ ] **Step 4: Tests, build, screenshot front/back.**
- [ ] **Step 5 (Review Focus 2):** add to `test_osc.cpp` a run of 48 000 samples of the core at 16 channels × 7 voices with max detune and PW 0.99: every output finite and within ±12 V.
- [ ] **Step 6 (Review Focus 1):** patch 16-channel V/OCT (Fundamental Merge or MIDI-CV poly) into JX-OSC with 7 voices; enable Engine → CPU meter; screenshot; record the module's CPU % in the ledger.
- [ ] **Step 7: Commit** `git commit -m "JXMP: JX-OSC (VCO) with sub, unison, morph and octave"`.

---

### Task 5: JX-FLT (VCF)

**Files:** Create `plugins/JXMP/src/dsp/Ladder.hpp` (copy of `LadderFilter` + `clip` from `plugins/Fundamental/src/VCF.cpp` lines 1–66 with attribution, exposing `state[0..3]` and the feedback-corrected input `u`), `plugins/JXMP/src/dsp/LadderTap.hpp`, `plugins/JXMP/src/Flt.cpp`, `plugins/JXMP/tests/test_ladder.cpp`; register.

**Interfaces:** Produces `enum jxmp::FilterMode { LP, BP, HP, NOTCH }`, `float jxmp::ladderTap(FilterMode, bool steep24, float u, const float s[4])`; model `modelJXMPVCF` slug `VCF`.

- [ ] **Step 1: Test** `test_ladder.cpp`:

```cpp
#include "dsp/LadderTap.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace jxmp;

int main() {
	const float s[4] = {3.f, 3.f, 3.f, 3.f};
	for (bool steep : {false, true}) {
		assert(std::fabs(ladderTap(LP, steep, 3.f, s) - 3.f) < 1e-5f);
		assert(std::fabs(ladderTap(NOTCH, steep, 3.f, s) - 3.f) < 1e-5f);
		assert(std::fabs(ladderTap(BP, steep, 3.f, s)) < 1e-5f);
		assert(std::fabs(ladderTap(HP, steep, 3.f, s)) < 1e-5f);
	}
	const float z[4] = {0, 0, 0, 0};
	assert(std::fabs(ladderTap(HP, true, 2.f, z) - 2.f) < 1e-5f);  // HF passes the HP
	std::puts("ladder: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `dsp/LadderTap.hpp` with the spec's table (LP `s1`/`s3`, BP `2s0−2s1`/`4s1−8s2+4s3`, HP `u−2s0+s1`/`u−4s0+6s1−4s2+s3`, NOTCH `u−2s0+2s1`/`u−4s0+6s1−4s2+2s3`). `Flt.cpp`: params as Fundamental VCF (`FREQ, FINE` if present, `RES, DRIVE, FREQ_CV, RES_CV, DRIVE_CV`) plus `MODE` (switch LP/BP/HP/NOTCH), `SLOPE` (12/24), `KEYTRACK` (0..1), `ENV_AMT` (−1..1); inputs `IN, VOCT, ENV, FREQ, RES, DRIVE`; outputs `OUT, LPF, HPF`. Cutoff pitch = FREQ + FREQ_CV·CV + KEYTRACK·VOCT + ENV_AMT·ENV/10·5 octaves; same 2× oversampling as Fundamental; `OUT` = `ladderTap(mode, slope, u, state)` clipped as the original HPF is. Widget 10 HP, colour `#3a8dde`. `configBypass(IN, OUT)`.
- [ ] **Step 4 (Review Focus 2):** add to the test a 48 000-sample run of the copied ladder at max resonance and drive on white noise ±10 V: outputs finite, |out| ≤ 12 V for every mode/slope.
- [ ] **Step 5: Build, screenshot. Commit** `git commit -m "JXMP: JX-FLT (VCF) with modes, slope, keytrack and envelope"`.

---

### Task 6: Voice check, docs

- [ ] **Step 1:** Patch (hand-written JSON like the Patchbay test patch): MIDI-CV (Cardinal Host MIDI) → Patchbay front → back → JX-OSC V/OCT, JX-ENV gate; JX-OSC MIX → JX-FLT IN; JX-ENV ENV → JX-FLT ENV and JX-AMP CV1; JX-FLT OUT → JX-AMP IN1 → Patchbay back → front → Audio. Screenshot front and back. With xdotool: press JX-ENV PUSH and confirm the Audio meter moves in a screenshot.
- [ ] **Step 2 (Review Focus 3):** change several knobs/switches with xdotool, save, reload with capture, compare param values in the decoded JSON.
- [ ] **Step 3:** `plugins/JXMP/README.md`: add the four modules; `research_docs/03-work-items.md`: V-1…V-5 ☑, S-3 ☑.
- [ ] **Step 4: Commit** `git commit -m "Docs: JXMP voice modules"`.
