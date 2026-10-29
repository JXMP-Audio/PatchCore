# Realistic cables Implementation Plan

> **Cancelled (2026-10-29):** see [07](07-realistic-cables-design.md). Not to be executed.


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In the PatchCore look, cables hang and swing like ropes, end in drawn 3.5 mm (front) or 6.3 mm (back) plugs with a shadow and a plug-in animation, come in four types with a classic or stackable plug, and stack into towers on outputs.

**Architecture:** Pure, Rack-free headers in `src/patchcore/` (`CableRope.hpp`, `CableStyle.hpp`, `CableStack.hpp`) hold the simulation and the rules and are tested with `g++`. `src/override/CableWidget.cpp` stores type/stackable in its `Internal`, runs one rope per cable, and draws cables and plugs procedurally when `patchcore::enabled()`. A new `src/override/PortWidget.cpp` override adds tower dragging and the one-plug-per-jack rule. Classic keeps every old code path.

**Tech Stack:** C++17, VCV Rack 2 API (Cardinal fork), NanoVG, GNU Make, `g++` test runner.

**Spec:** [07-realistic-cables-design.md](07-realistic-cables-design.md)

## Global Constraints

- PatchCore look only (`patchcore::enabled()`); with Classic every changed function takes its old path, unchanged.
- Do not edit `src/Rack/`. Overrides go to `src/override/` with a regenerated diff in `src/override/diffs/` (`cd src/override && diff -u ../Rack/src/app/<F>.cpp <F>.cpp > diffs/<F>.cpp.diff`).
- `CableWidget`'s public header is Rack's; new per-cable state lives in `CableWidget::Internal`, reached through functions declared in `src/patchcore/PatchCore.hpp`.
- Plug on the front = 3.5 mm mini jack, on the back = 6.3 mm studio jack, chosen by `portSide()` of the plug's port.
- Patch JSON keys: `"patchcoreType"` (`standard|led|braided|coiled`), `"patchcoreStackable"` (bool). Missing keys = defaults; old patches load unchanged.
- Stacking only on outputs. A second cable on an output auto-converts the plug below to stackable, in the same undo step.
- Performance: sleeping ropes cost nothing; at most 4 sub-steps of 1/120 s per frame.
- Binary is `bin/Cardinal` (`make -j12 jack`). Screenshots: `PATCHCORE_CAPTURE` (see the Patchbay plan ledger's capture script).
- Commit messages end with the session's `Co-Authored-By` / `Claude-Session` lines.

## Review Focus

1. **Zoom changes** (rope lengths are in rack px, not screen px): a cable must not stretch or explode when zooming. Covered by keeping the rope in rack coordinates — Task 3 Step 4 checks at zoom 0.5 and 2.
2. **Module moved / deleted while a rope is awake**: ends jump; the rope must follow without NaN and the widget must not touch a deleted port. Task 1 test `big_jump_stays_finite`, Task 3 Step 4.
3. **Undo of an auto-converted stack**: undo removes the top cable *and* restores the classic plug below. Task 7 Step 6.
4. **Old patch with 3 cables on one output** loads as a tower of 3, bottom two stackable. Task 7 Step 6.
5. **Classic theme** draws exactly the old cables and plugs. Task 3 Step 4 and Task 4 Step 5 compare against Classic screenshots.

---

### Task 1: Rope simulation (pure)

**Files:**
- Create: `src/patchcore/CableRope.hpp`
- Create: `src/patchcore/tests/test_cable_rope.cpp`, `src/patchcore/tests/run.sh`

**Interfaces:**
- Produces: `patchcore::rope::Vec2`, `RopeParams`, `Rope::reset/step/asleep/points` (used by Task 3, 5).

- [ ] **Step 1: Tests**

`src/patchcore/tests/run.sh`: same script as `plugins/JXMP/tests/run.sh` with `-I"$here/.."`.

`src/patchcore/tests/test_cable_rope.cpp`:

```cpp
#include "CableRope.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace patchcore::rope;

static Rope settled(Vec2 a, Vec2 b, const RopeParams& p) {
	Rope r;
	r.reset(a, b, p);
	for (int i = 0; i < 2000 && !r.asleep(); ++i)
		r.step(a, {0, 1}, b, {0, 1}, 1.f / 60.f, p);
	return r;
}

static float length(const Rope& r) {
	float l = 0;
	for (size_t i = 1; i < r.points().size(); ++i)
		l += (r.points()[i] - r.points()[i - 1]).norm();
	return l;
}

int main() {
	RopeParams p;
	const Vec2 a{0, 0}, b{300, 0};

	// sags_below_line: the middle hangs below the ends (y grows downwards)
	Rope r = settled(a, b, p);
	assert(r.asleep());
	assert(r.points()[p.points / 2].y > 20.f);

	// keeps_length: within 2 % of the target length
	const float target = std::max(p.minLength, 300.f * p.slack);
	assert(std::fabs(length(r) - target) / target < 0.02f);

	// ends_pinned
	assert((r.points().front() - a).norm() < 1e-3f);
	assert((r.points().back() - b).norm() < 1e-3f);

	// moving_end_wakes
	r.step(a, {0, 1}, {320, 0}, {0, 1}, 1.f / 60.f, p);
	assert(!r.asleep());

	// big_jump_stays_finite: huge dt and a far jump
	r.step({-5000, 3000}, {0, 1}, {9000, -2000}, {0, 1}, 10.f, p);
	for (const Vec2& q : r.points())
		assert(std::isfinite(q.x) && std::isfinite(q.y));

	// coiled_sags_less
	RopeParams coiled = p;
	coiled.slack = 1.0f;
	coiled.minLength = 40.f;
	Rope c = settled(a, b, coiled);
	assert(c.points()[p.points / 2].y < settled(a, b, p).points()[p.points / 2].y);

	std::puts("cable rope: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL** (`CableRope.hpp: No such file`): `chmod +x src/patchcore/tests/run.sh && src/patchcore/tests/run.sh`

- [ ] **Step 3: Implementation**

`src/patchcore/CableRope.hpp`:

```cpp
/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/** Verlet rope for cables. No Rack dependency, so it can be tested alone. Coordinates in rack px, y down. */
namespace patchcore {
namespace rope {

struct Vec2 {
	float x = 0, y = 0;
	Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
	Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
	Vec2 operator*(float s) const { return {x * s, y * s}; }
	float norm() const { return std::sqrt(x * x + y * y); }
};

struct RopeParams {
	int points = 20;
	float slack = 1.12f;
	float minLength = 60.f;
	float gravity = 900.f;
	float damping = 0.985f;
	int iterations = 8;
	float stiffness = 0.15f;
	/** How far the second point is pulled out of the plug, px */
	float plugLead = 14.f;
};

struct Rope {
	void reset(Vec2 a, Vec2 b, const RopeParams& p) {
		const int n = std::max(p.points, 3);
		pos.assign(n, Vec2());
		prev.assign(n, Vec2());
		for (int i = 0; i < n; ++i) {
			const float t = float(i) / (n - 1);
			// Start on a parabola close to the rest shape, so a fresh cable doesn't drop from straight
			const float sag = 0.25f * targetLength(a, b, p) * 4.f * t * (1.f - t) * 0.5f;
			pos[i] = a + (b - a) * t + Vec2{0, sag};
			prev[i] = pos[i];
		}
		lastA = a;
		lastB = b;
		quiet = 0.f;
		sleeping = false;
		acc = 0.f;
	}

	void step(Vec2 a, Vec2 dirA, Vec2 b, Vec2 dirB, float dt, const RopeParams& p) {
		if ((int) pos.size() != std::max(p.points, 3))
			reset(a, b, p);
		const bool endsMoved = (a - lastA).norm() > 0.01f || (b - lastB).norm() > 0.01f;
		if (endsMoved)
			sleeping = false, quiet = 0.f;
		lastA = a;
		lastB = b;
		if (sleeping)
			return;

		const float h = 1.f / 120.f;
		acc = std::min(acc + std::max(dt, 0.f), 4 * h);
		float maxMove = 0.f;
		while (acc >= h) {
			acc -= h;
			maxMove = std::max(maxMove, substep(a, dirA, b, dirB, h, p));
		}

		quiet = maxMove < 0.05f ? quiet + dt : 0.f;
		if (quiet > 0.5f)
			sleeping = true;
	}

	bool asleep() const { return sleeping; }
	const std::vector<Vec2>& points() const { return pos; }

	static float targetLength(Vec2 a, Vec2 b, const RopeParams& p) {
		return std::max(p.minLength, (b - a).norm() * p.slack);
	}

private:
	std::vector<Vec2> pos, prev;
	Vec2 lastA, lastB;
	float quiet = 0.f, acc = 0.f;
	bool sleeping = false;

	float substep(Vec2 a, Vec2 dirA, Vec2 b, Vec2 dirB, float h, const RopeParams& p) {
		const int n = (int) pos.size();
		float maxMove = 0.f;
		for (int i = 1; i < n - 1; ++i) {
			const Vec2 v = (pos[i] - prev[i]) * p.damping;
			prev[i] = pos[i];
			pos[i] = pos[i] + v + Vec2{0, p.gravity * h * h};
		}
		const float seg = targetLength(a, b, p) / (n - 1);
		for (int it = 0; it < p.iterations; ++it) {
			pos.front() = a;
			pos.back() = b;
			// The cable leaves each plug straight along the plug direction
			pos[1] = pos[1] + ((a + dirA * std::min(p.plugLead, seg)) - pos[1]) * 0.5f;
			pos[n - 2] = pos[n - 2] + ((b + dirB * std::min(p.plugLead, seg)) - pos[n - 2]) * 0.5f;
			for (int i = 0; i < n - 1; ++i) {
				const Vec2 d = pos[i + 1] - pos[i];
				const float len = std::max(d.norm(), 1e-6f);
				const Vec2 corr = d * ((len - seg) / len * 0.5f);
				if (i > 0)
					pos[i] = pos[i] + corr;
				if (i + 1 < n - 1)
					pos[i + 1] = pos[i + 1] - corr;
			}
			// Bending resistance: pull each point towards the middle of its neighbours
			for (int i = 1; i < n - 1; ++i)
				pos[i] = pos[i] + (((pos[i - 1] + pos[i + 1]) * 0.5f) - pos[i]) * (p.stiffness * 0.5f);
		}
		pos.front() = a;
		pos.back() = b;
		for (int i = 0; i < n; ++i) {
			if (!std::isfinite(pos[i].x) || !std::isfinite(pos[i].y)) {
				reset(a, b, p);
				return 1.f;
			}
			maxMove = std::max(maxMove, (pos[i] - prev[i]).norm());
		}
		return maxMove;
	}
};

} // namespace rope
} // namespace patchcore
```

- [ ] **Step 4: Run, expect** `cable rope: all tests passed`. If `keeps_length` fails because stiffness shortens the rope, raise `iterations` to 12 in the test's params only if the default still passes visually in Task 3; record a ruling.

- [ ] **Step 5: Commit** `git add src/patchcore/CableRope.hpp src/patchcore/tests && git commit -m "PatchCore: rope simulation for cables, with tests"`

---

### Task 2: Cable style and stack rules (pure) + per-cable state

**Files:**
- Create: `src/patchcore/CableStyle.hpp`, `src/patchcore/CableStack.hpp`
- Create: `src/patchcore/tests/test_cable_style.cpp`
- Modify: `src/patchcore/PatchCore.hpp` (declarations), `src/patchcore/PatchCore.cpp` (settings `cableType`, `cableColor`), `src/override/CableWidget.cpp` (`Internal` fields, JSON, accessors)

**Interfaces:**
- Produces: `enum patchcore::CableType { CABLE_STANDARD, CABLE_LED, CABLE_BRAIDED, CABLE_COILED }`; `const char* cableTypeName(CableType)`, `CableType cableTypeFromName(const char*)`, `rope::RopeParams ropeParams(CableType)` (CableStyle.hpp); `std::vector<bool> stackAfterAdd(const std::vector<bool>& stackableBottomToTop, bool newPlugStackable)` (CableStack.hpp: returns the stackable flags of the tower after a new plug goes on top: every plug below the new one becomes stackable); in PatchCore.hpp: `CableType cableType(app::CableWidget*)`, `void setCableType(app::CableWidget*, CableType)`, `bool cableStackable(app::CableWidget*)`, `void setCableStackable(app::CableWidget*, bool)`; `Settings::cableType` (int, default `CABLE_STANDARD`), `Settings::cableColor` (std::string, `""` = cycle).

- [ ] **Step 1: Tests** `src/patchcore/tests/test_cable_style.cpp`:

```cpp
#include "CableStyle.hpp"
#include "CableStack.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace patchcore;

int main() {
	for (int t = CABLE_STANDARD; t <= CABLE_COILED; ++t)
		assert(cableTypeFromName(cableTypeName((CableType) t)) == t);
	assert(cableTypeFromName("nonsense") == CABLE_STANDARD);
	assert(cableTypeFromName(nullptr) == CABLE_STANDARD);
	assert(std::strcmp(cableTypeName(CABLE_LED), "led") == 0);

	assert(ropeParams(CABLE_BRAIDED).stiffness > ropeParams(CABLE_STANDARD).stiffness);
	assert(ropeParams(CABLE_COILED).slack < ropeParams(CABLE_STANDARD).slack);

	// First plug on an empty jack keeps its own flag
	assert((stackAfterAdd({}, false) == std::vector<bool>{false}));
	// Second plug: the classic one below turns stackable, the new one keeps its flag
	assert((stackAfterAdd({false}, false) == std::vector<bool>{true, false}));
	assert((stackAfterAdd({true, false}, true) == std::vector<bool>{true, true, true}));

	std::puts("cable style: all tests passed");
	return 0;
}
```

- [ ] **Step 2: Run, expect FAIL** (missing headers).

- [ ] **Step 3: Implementation**

`src/patchcore/CableStyle.hpp`:

```cpp
#pragma once

#include <cstring>
#include "CableRope.hpp"

namespace patchcore {

enum CableType {
	CABLE_STANDARD = 0,
	CABLE_LED,
	CABLE_BRAIDED,
	CABLE_COILED,
};

inline const char* cableTypeName(CableType t) {
	switch (t) {
	case CABLE_LED: return "led";
	case CABLE_BRAIDED: return "braided";
	case CABLE_COILED: return "coiled";
	default: return "standard";
	}
}

inline CableType cableTypeFromName(const char* s) {
	if (s != nullptr) {
		for (int t = CABLE_STANDARD; t <= CABLE_COILED; ++t)
			if (std::strcmp(s, cableTypeName((CableType) t)) == 0)
				return (CableType) t;
	}
	return CABLE_STANDARD;
}

inline rope::RopeParams ropeParams(CableType t) {
	rope::RopeParams p;
	if (t == CABLE_BRAIDED)
		p.stiffness = 0.35f;
	if (t == CABLE_COILED) {
		p.slack = 1.0f;
		p.minLength = 40.f;
		p.damping = 0.97f;
	}
	return p;
}

} // namespace patchcore
```

`src/patchcore/CableStack.hpp`:

```cpp
#pragma once

#include <vector>

namespace patchcore {

/** Stackable flags of a tower (bottom to top) after a new plug goes on top. Every plug below the new one must be
stackable, so a classic plug that gets a plug on top turns stackable. */
inline std::vector<bool> stackAfterAdd(const std::vector<bool>& stackableBottomToTop, bool newPlugStackable) {
	std::vector<bool> out(stackableBottomToTop.size(), true);
	out.push_back(newPlugStackable);
	return out;
}

} // namespace patchcore
```

Add to `src/patchcore/PatchCore.hpp` (after the `cableSide` declaration) `#include "CableStyle.hpp"` at the top and:

```cpp
/** Per-cable look, stored in CableWidget::Internal and in the patch. */
CableType cableType(app::CableWidget* cw);
void setCableType(app::CableWidget* cw, CableType type);
bool cableStackable(app::CableWidget* cw);
void setCableStackable(app::CableWidget* cw, bool stackable);
```

and to `Settings`:

```cpp
	/** Type of new cables */
	int cableType = CABLE_STANDARD;
	/** Colour of new cables as hex, empty = cycle through Rack's cable colours */
	std::string cableColor;
```

Load/save both in `PatchCore.cpp` next to `transportVisible` (`json_integer`, `json_string`).

In `src/override/CableWidget.cpp`, extend `Internal` with `patchcore::CableType type = patchcore::CABLE_STANDARD; bool stackable = false;`. In the constructor, after `internal = new Internal;`: `internal->type = (patchcore::CableType) patchcore::settings.cableType;`. In `mergeJson` add (PatchCore look or not, keys are harmless):

```cpp
	json_object_set_new(rootJ, "patchcoreType", json_string(patchcore::cableTypeName(internal->type)));
	if (internal->stackable)
		json_object_set_new(rootJ, "patchcoreStackable", json_true());
```

In `fromJson` add:

```cpp
	if (json_t* const typeJ = json_object_get(rootJ, "patchcoreType"))
		internal->type = patchcore::cableTypeFromName(json_string_value(typeJ));
	internal->stackable = json_is_true(json_object_get(rootJ, "patchcoreStackable"));
```

At the end of the file, before the namespaces close, define the accessors in `namespace rack::patchcore` (they need `Internal`):

```cpp
} // namespace app

namespace patchcore {
CableType cableType(app::CableWidget* cw) { return cw->internal->type; }
void setCableType(app::CableWidget* cw, CableType type) { cw->internal->type = type; }
bool cableStackable(app::CableWidget* cw) { return cw->internal->stackable; }
void setCableStackable(app::CableWidget* cw, bool stackable) { cw->internal->stackable = stackable; }
} // namespace patchcore

namespace app {
```

(`CableWidget::internal` is a private member of Rack's header; if the compiler refuses access, declare the accessors as static member-like free functions inside `namespace app` in this file and forward from `patchcore::` — record the ruling.)

New cable colour: in `src/override/CableWidget.cpp` constructor nothing; in `PortWidget.cpp` (Task 7) and wherever Rack calls `getNextCableColor()` for a new cable, PatchCore uses `settings.cableColor` when set. For this task, only implement a helper `NVGcolor patchcore::newCableColor()` in `PatchCore.cpp` returning `color::fromHexString(settings.cableColor)` or `APP->scene->rack->getNextCableColor()`.

- [ ] **Step 4: Run tests, expect** `cable style: all tests passed`; build `make -j12 jack`, expect no errors. Load the Patchbay test patch, save it, decode it (`zstd -dc … | tar -x`), expect `"patchcoreType": "standard"` on every cable.

- [ ] **Step 5: Commit** `git commit -m "PatchCore: cable type and stackable flag, saved in the patch"` (+ regenerated `CableWidget.cpp.diff`).

---

### Task 3: Rope-driven cables (Standard look)

**Files:** Modify `src/override/CableWidget.cpp` (`Internal`, `step`, `drawLayer`), regenerate its diff.

**Interfaces:** Consumes `rope::Rope`, `ropeParams()`, `cableType()`.

- [ ] **Step 1:** Add `patchcore::rope::Rope rope; bool ropeValid = false;` to `Internal`. In `CableWidget::step()`, when `patchcore::enabled()`: compute `outputPos`/`inputPos` as now; plug directions are `{0, 1}` (cables hang down out of the plug) unless the cable is incomplete (then the free end's direction is `{0, 1}` too); call `internal->rope.reset(...)` on the first frame (`!ropeValid`) and `internal->rope.step(out, {0,1}, in, {0,1}, APP->window->getLastFrameDuration(), patchcore::ropeParams(type))` every frame. Use the rope's point at 20 % / 80 % for the plug angles (`setAngle`). Keep the old spring code for Classic only.

- [ ] **Step 2:** Add a static helper in `CableWidget.cpp`:

```cpp
/** Smooth NanoVG path through the rope points (Catmull-Rom as cubic Béziers), offset by `d`. */
static void ropePath(NVGcontext* vg, const std::vector<patchcore::rope::Vec2>& p, math::Vec d) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, p[0].x + d.x, p[0].y + d.y);
	for (size_t i = 0; i + 1 < p.size(); ++i) {
		const auto& p0 = p[i > 0 ? i - 1 : i];
		const auto& p1 = p[i];
		const auto& p2 = p[i + 1];
		const auto& p3 = p[i + 2 < p.size() ? i + 2 : i + 1];
		nvgBezierTo(vg,
			p1.x + (p2.x - p0.x) / 6.f + d.x, p1.y + (p2.y - p0.y) / 6.f + d.y,
			p2.x - (p3.x - p1.x) / 6.f + d.x, p2.y - (p3.y - p1.y) / 6.f + d.y,
			p2.x + d.x, p2.y + d.y);
	}
}
```

In `drawLayer`, in the `patchcoreLook` branch, replace the three `nvgMoveTo/nvgQuadTo` paths with `ropePath(args.vg, internal->rope.points(), offset)` (shadow: offset `(2, 6)` then a second pass `(4, 11)` at alpha 0.12; tube: offset 0; highlight: offset `lift`). Do not shorten the ends by 13 px any more (the plug body covers them, Task 4).

- [ ] **Step 3:** Build, run, screenshot front/back of the Patchbay test patch at zoom 1.0. Expected: cables hang in smooth curves, no kinks at the plugs.

- [ ] **Step 4 (Review Focus 1, 2, 5):** Screenshots at `PATCHCORE_DEV_ZOOM=0.5` and `2.0`: same shapes, scaled. With the user's xdotool consent (already given for this project's tests): drag a module with cables sideways and release — no NaN/vanished cables; delete a module with cables — no crash. Switch to Classic (View menu) and screenshot: identical to the Patchbay plan's Classic look.

- [ ] **Step 5: Commit** `git commit -m "PatchCore: cables follow a rope simulation"`.

---

### Task 4: Drawn plugs, shadow, plug-in animation, 6.3 mm sockets

**Files:**
- Create: `src/patchcore/PlugDraw.cpp` (+ declarations in `PatchCore.hpp`), add it to `src/Makefile` next to the other `patchcore/` files
- Modify: `src/override/CableWidget.cpp` (`PlugWidget::draw`, connect animation)
- Create: `plugins/JXMP/src/StudioSocket.hpp`; Modify `plugins/JXMP/src/Patchbay.cpp` (back `SmartPort`s derive from the studio socket look)

**Interfaces:** Produces `void patchcore::draw::miniJack(NVGcontext*, float scale, NVGcolor boot, float flash)`, `void patchcore::draw::studioJack(NVGcontext*, float scale, NVGcolor boot, float flash)` — drawn at the origin pointing along +y (the cable side), scale 1 = 3.5 mm socket size at zoom 1.

- [ ] **Step 1:** `PlugDraw.cpp`: `miniJack`: shadow ellipse (offset (2,5), alpha 0.3) → knurled barrel (rounded rect 9×12 px, vertical linear gradient grey 0xd8→0x7a, 5 dark 0.6 px stripes) → collar (rect 7×3, 0x9a) → boot (tapered path 7→4.5 px wide, 12 px long, cable colour with a lighter left edge gradient) → specular dot. `studioJack`: same structure at 1.45× width: chrome barrel with two bright bands (gradients 0xf2/0x8c), a black ring, longer boot (16 px). `flash` 0..1 adds a white glow circle (alpha 0.5·flash) around the barrel.

- [ ] **Step 2:** In `PlugWidget`, add `bool studio = false; double connectTime = -1;` and override `draw()`: if `!patchcore::enabled()` → old `Widget::draw(args)`; else rotate by `angle - π/2` and call `studioJack`/`miniJack` with `scale = 1 + 0.15 * (1 - t)` for `t = clamp((now - connectTime) / 0.12)` and `flash = 1 - t`. In `CableWidget::step()` set `studio = portSide(port) == SIDE_BACK` for each plug, and set `connectTime = system::getTime()` on both plugs on the first step where `isComplete()` turns true (store the previous completeness in `Internal`). While a plug is being dragged (incomplete end), use scale 1.08.

- [ ] **Step 3:** `plugins/JXMP/src/StudioSocket.hpp`: `struct StudioSocketLook` with `static void draw(NVGcontext*, math::Vec size)` (hex nut 1.35× PJ301M size, dark hole, chrome ring). In `Patchbay.cpp`, the back `SmartPort`s set `studio = true`; `SmartPort::draw` draws `StudioSocketLook` instead of the SVG when `studio`. Back jack pitch: Patchbay 8/16 rows at 12 mm fit a 1.35× nut (11.3 mm); keep the layout.

- [ ] **Step 4:** Build; screenshots front (mini plugs on VCO/Patchbay front) and back (studio plugs, bigger sockets). Expected: plugs cover the cable ends, rotate with the cable, shadow under them.

- [ ] **Step 5 (Review Focus 5):** Classic screenshot unchanged. **Commit** `git commit -m "PatchCore: drawn 3.5 mm and 6.3 mm plugs with plug-in animation"`.

---

### Task 5: Cable types LED, Braided, Coiled

**Files:** Modify `src/override/CableWidget.cpp` (`drawLayer`), `src/patchcore/PlugDraw.cpp` (LED boot).

- [ ] **Step 1: LED** — level `v = clamp(|output voltage sum| / 10)` (already read in `drawLayer` via `cable->outputModule->outputs[...]`; smooth it in `Internal` with a 50 ms one-pole). Draw a core line along the rope (`thickness * 0.25`, colour `color::plus(color, white*0.6)`, alpha `0.25 + 0.75 v`) and pass `glow = v` to the plug functions: the boot gets an additive halo (`nvgGlobalCompositeOperation(NVG_LIGHTER)`, radius 10 px, alpha 0.6 v).
- [ ] **Step 2: Braided** — after the tube, two stroked passes along the same path with `nvgStrokeWidth(thickness - 2)`, dash emulation: walk the rope polyline and draw short diagonal segments every 4 px alternating left/right (`±thickness*0.35` normal offset), alpha 0.35 dark / 0.25 light.
- [ ] **Step 3: Coiled** — walk the rope polyline at arc-length steps of 1.5 px; at distance `s`, offset along the normal by `r·sin(2π s / 7)` (`r = thickness * 0.55`); draw the back half (cos < 0) darker first, then the front half. No tube body under it.
- [ ] **Step 4:** Add a dev env `PATCHCORE_DEV_CABLETYPE=<name>` in `devStep()` that sets every cable's type once (for screenshots). Build; screenshot each type front and back. Expected: four clearly different looks; LED cables brighten with the VCO signal.
- [ ] **Step 5: Commit** `git commit -m "PatchCore: LED, braided and coiled cables"`.

---

### Task 6: Menus

**Files:** Modify `src/override/MenuBar.cpp` (View → PatchCore → Cables), `src/override/CableWidget.cpp` (`PlugWidget::onButton` right-click menu), regenerate diffs.

- [ ] **Step 1:** View menu, under the PatchCore label: submenu "Cables" with radio items for the 4 types (`settings.cableType`), a "Colour: cycle" check item, then one item per `settings::cableColors` swatch (a `ColorSwatchItem` drawing a 12 px filled circle before the label) setting `settings.cableColor` to its hex; `saveSettings()` on change.
- [ ] **Step 2:** `PlugWidget::onButton`: when `patchcore::enabled()` and right button press: `e.consume(this)` and open a `ui::Menu` (`createMenu()`) with the colour swatches, the 4 types and a "Stackable plug" check item, for the owning `CableWidget` (`getAncestorOfType<CableWidget>()`). Colour changes push `history::CableColorChange` like Rack; type/stackable changes push a small `history::Action` subclass `CableStyleChange { int64_t cableId; CableType oldType, newType; bool oldStack, newStack; }` that looks the cable up by id in `undo/redo`.
- [ ] **Step 3:** `PlugWidget` must report a hit box for events: give it `box.size` = the drawn plug bounds (≈ 14×28 px around its position) only in the PatchCore look, so left-drags still fall through to the port below (forward `onDragStart` etc. by not consuming left button events).
- [ ] **Step 4:** Build; with xdotool (user consent given): right-click a plug, choose "Braided", save, decode the patch: `"patchcoreType": "braided"` on that cable; Ctrl+Z, save, decode: `"standard"` again. Set View default type LED, drag a new cable, decode: `"led"`.
- [ ] **Step 5: Commit** `git commit -m "PatchCore: cable colour and type menus"`.

---

### Task 7: Stackable plugs and towers

**Files:**
- Create: `src/override/PortWidget.cpp` (copy of `src/Rack/src/app/PortWidget.cpp` with the changes below), its diff; add `RACK_FILES += override/PortWidget.cpp` and `IGNORED_FILES += Rack/src/app/PortWidget.cpp` in `src/Makefile`
- Modify: `src/override/CableWidget.cpp` (tower drawing)

**Interfaces:** Consumes `stackAfterAdd()`, `cableStackable/setCableStackable`.

- [ ] **Step 1: One plug per jack, auto-convert.** In `PortWidget::onDragEnd` (override), when `patchcore::enabled()` and the released cable is complete and its `outputPort` already has other complete cables: compute `stackAfterAdd(flags of existing cables bottom→top, cableStackable(new))`, apply the flags with `setCableStackable`, and push one `history::ComplexAction` containing the `CableAdd` and a `CableStyleChange` per changed cable (so one Ctrl+Z undoes both).
- [ ] **Step 2: Tower order and drawing.** Tower order = `APP->scene->rack->getCablesOnPort(outputPort)` order (oldest first). In `CableWidget::step()`, set the output plug's `stackIndex` = index in that list; `PlugWidget::draw` offsets by `stackIndex * (-1.5, -2.5)` px and scales by `1 + 0.06 * stackIndex`, and draws a stackable plug with a small socket ring on its top. The cable leaves from the offset plug position (rope end = offset position).
- [ ] **Step 3: Pull the tower.** In `PortWidget::onDragStart` (override), PatchCore look, plain drag on an **output** with cables: instead of taking only the top cable, take the cable whose drawn plug is under the mouse (hit-test plug positions from Step 2, default the bottom one) and remember it plus every cable above it in a static `std::vector<CableWidget*> towerRest`. The grabbed one becomes the incomplete cable as in Rack. In `onDragEnd`: if the grabbed cable is complete again, move every `towerRest` cable's output to the same port (`cw->outputPort = grabbed->outputPort; cw->updateCable();`), push `CableRemove`+`CableAdd` pairs into the same `ComplexAction`; if it was dropped nowhere (deleted), delete the rest too (`rack->removeCable`, with history).
- [ ] **Step 4: Old patches.** After a patch loads (`devStep`-like hook in `RackFrame::step()` keyed on the cable count changing), for every output with >1 cables set all but the top `stackable = true` — only if no cable on it already has the key (so user choices are kept).
- [ ] **Step 5:** Build; xdotool tests (decode the saved patch after each):
  - Ctrl-drag a second cable out of a VCO output: the bottom cable now has `"patchcoreStackable": true`.
  - Plain drag the bottom plug to another output: both cables now leave from the new output.
  - Drag the top plug only (aim at its offset position): only the top cable moves.
- [ ] **Step 6 (Review Focus 3, 4):** Ctrl+Z after the auto-convert: 1 cable left and its plug is classic again (no key). Load a hand-written patch with 3 cables on one output: two keys `true`, top none. Screenshot of the tower.
- [ ] **Step 7: Commit** `git commit -m "PatchCore: stackable plugs and towers"`.

---

### Task 8: Docs and measurement

- [ ] **Step 1:** Measure: open `patches/examples/falkTX_-_Mini-Arp-Seq.vcv` (or the biggest example) with `PATCHCORE_CAPTURE` and Rack's frame-time (Engine → CPU meter / `settings::frameRateLimit` log); compare frame time before (main) and after. Record numbers in the ledger.
- [ ] **Step 2:** Update `research_docs/03-work-items.md` (C-1…C-5 ☑), add a "Cables" section to `plugins/JXMP/README.md`'s parent doc or `research_docs/README.md` index (this plan).
- [ ] **Step 3: Commit** `git commit -m "Docs: realistic cables"`.
