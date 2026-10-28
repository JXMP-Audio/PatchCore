# Sided modules + Patchbay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Basic modules keep sockets and cables on the front, JXMP modules patch on the back, and a Patchbay 4/8/16 module bridges the two with one smart jack per channel and side.

**Architecture:** A header-only interface `patchcore::SidedModuleWidget` (in `include/`, so plugins can use it) tells the UI which side each port is on. `ModuleWidget_syncPatchCoreView`, the blanking caps, the back plate labels and cable visibility all switch from "global front/back" to "per port side". A new built-in plugin `plugins/JXMP/` holds the Patchbay; its smart jack is two stacked `PortWidget`s, of which only the active one has a hit box.

**Tech Stack:** C++17, VCV Rack 2 API (Cardinal fork), NanoVG, GNU Make. Pure logic is tested with a standalone `g++` test binary.

**Spec:** [04-sided-modules-patchbay-design.md](04-sided-modules-patchbay-design.md)

## Global Constraints

- Do not edit `src/Rack/` (submodule). Changes go to `src/override/`, `src/patchcore/`, `include/`, `plugins/JXMP/`.
- After editing a file in `src/override/`, regenerate its diff: `cd src/override && diff -u ../Rack/src/app/<File>.cpp <File>.cpp > diffs/<File>.cpp.diff` (diff exits 1, that is fine).
- `src/CardinalPlugin.cpp` uses CRLF line endings. Keep them if you touch it.
- No third-party trademarks in names, panels or text (no VCV, no Reason). Brand is "JXMP Audio", plugin slug `JXMP`.
- Classic theme (`patchcore::enabled() == false`) must look and behave exactly as today.
- Basic modules = every module whose widget is not a `SidedModuleWidget`. All their ports are `SIDE_FRONT`.
- Patchbay sizes: 4 ch / 4 HP, 8 ch / 4 HP, 16 ch / 8 HP. Signals pass unchanged, up to 16 poly channels.
- Commit messages end with the `Co-Authored-By` / `Claude-Session` lines from the session.

## Review Focus

1. **Module browser previews** (`mw->module == NULL`): side comes from the widget, so JXMP previews must still hide back ports on the front. Covered in Task 3 Step 6.
2. **Picking up an existing cable from a Patchbay jack** (plain drag, Ctrl-drag clone, Shift-click delete) must behave like any Rack jack. Covered in Task 5 Step 7.
3. **Undo/redo** of a cable through a Patchbay restores the cable and the direction follows. Covered in Task 5 Step 7.
4. **Flip during a drag** (Tab while dragging a cable): the dragged cable stays visible only on its side, nothing crashes. Covered in Task 3 Step 6.
5. **Patch from a hand-edited file with a front↔back cable**: stays connected and is drawn on the front. Covered by `cableSide()` in Task 3 and checked in Task 3 Step 6.

---

### Task 1: Baseline build

**Files:** none changed.

- [ ] **Step 1: Build the standalone (JACK) variant**

Run (from `PatchCore/`): `make -j12 jack 2>&1 | tail -20`
Expected: ends without `Error`, binary at `bin/CardinalJACK` (first build takes a long time).
If dependencies are missing, install them as listed in `docs/BUILDING.md` (Fedora section) and rerun.

- [ ] **Step 2: Run it and take baseline screenshots**

Run: `PATCHCORE_DEV_PATCH=<some example patch from patches/> ./bin/CardinalJACK` once normally and once with `PATCHCORE_DEV_VIEW=back`. Save screenshots to the scratchpad (not the repo) for later comparison.
Expected: front shows modules without sockets (today's behaviour), back shows sockets and cables.

---

### Task 2: Patchbay direction logic (pure, tested)

**Files:**
- Create: `plugins/JXMP/src/PatchbayLogic.hpp`
- Create: `plugins/JXMP/tests/test_patchbay_logic.cpp`
- Create: `plugins/JXMP/tests/run.sh`

**Interfaces:**
- Produces: `jxmp::Dir`, `jxmp::Role`, `jxmp::Need`, `jxmp::ChannelCables`, `jxmp::Roles`, `jxmp::direction()`, `jxmp::need()`, `jxmp::roles()`, `jxmp::ownedDragRole()` — used by Task 5.

- [ ] **Step 1: Write the failing test**

`plugins/JXMP/tests/test_patchbay_logic.cpp`:

```cpp
#include "PatchbayLogic.hpp"
#include <cassert>
#include <cstdio>

using namespace jxmp;

int main() {
	// Direction comes from complete cables
	assert(direction({}) == Dir::Free);
	assert(direction({true, false, false, false}) == Dir::Forward);  // front in
	assert(direction({false, false, false, true}) == Dir::Forward);  // back out
	assert(direction({false, false, true, false}) == Dir::Reverse);  // back in
	assert(direction({false, true, false, false}) == Dir::Reverse);  // front out
	// Hand-edited patch with both inputs: forward wins in the UI
	assert(direction({true, false, true, false}) == Dir::Forward);

	// What a dragged cable still needs
	assert(need(false, false) == Need::Nothing);
	assert(need(true, false) == Need::Input);
	assert(need(false, true) == Need::Output);
	assert(need(true, true) == Need::Nothing);

	// Roles
	assert(roles(Dir::Forward, Need::Nothing).front == Role::In);
	assert(roles(Dir::Forward, Need::Output).back == Role::Out);
	assert(roles(Dir::Reverse, Need::Input).front == Role::Out);
	assert(roles(Dir::Reverse, Need::Input).back == Role::In);
	assert(roles(Dir::Free, Need::Input).front == Role::In);
	assert(roles(Dir::Free, Need::Input).back == Role::In);
	assert(roles(Dir::Free, Need::Output).front == Role::Out);
	assert(roles(Dir::Free, Need::Nothing).front == Role::Out);

	// A drag started on our free jack flips its end when hovering a foreign port of the same type
	assert(ownedDragRole(Role::Out, true, false) == Role::In);
	assert(ownedDragRole(Role::In, false, true) == Role::Out);
	assert(ownedDragRole(Role::Out, false, true) == Role::Out);
	assert(ownedDragRole(Role::In, true, false) == Role::In);
	assert(ownedDragRole(Role::Out, false, false) == Role::Out);

	std::puts("patchbay logic: all tests passed");
	return 0;
}
```

`plugins/JXMP/tests/run.sh`:

```sh
#!/usr/bin/env bash
# Builds and runs the JXMP pure-logic tests (no Rack needed).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/jxmp-tests"
mkdir -p "$out"
for t in "$here"/test_*.cpp; do
	name="$(basename "$t" .cpp)"
	g++ -std=c++17 -Wall -Wextra -Werror -I"$here/../src" "$t" -o "$out/$name"
	"$out/$name"
done
```

- [ ] **Step 2: Run test to verify it fails**

Run: `chmod +x plugins/JXMP/tests/run.sh && plugins/JXMP/tests/run.sh`
Expected: FAIL, `PatchbayLogic.hpp: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`plugins/JXMP/src/PatchbayLogic.hpp`:

```cpp
/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/** Direction rules of a Patchbay channel. No Rack dependency, so it can be tested alone. */
namespace jxmp {

enum class Dir { Free, Forward, Reverse };
enum class Role { In, Out };
/** What the cable being dragged still needs at its free end. */
enum class Need { Nothing, Input, Output };

/** Which of a channel's four ports have a complete cable. */
struct ChannelCables {
	bool frontIn = false;
	bool frontOut = false;
	bool backIn = false;
	bool backOut = false;
};

struct Roles {
	Role front;
	Role back;
};

/** Forward = front to back. Any cable locks the direction. */
inline Dir direction(const ChannelCables& c) {
	if (c.frontIn || c.backOut)
		return Dir::Forward;
	if (c.backIn || c.frontOut)
		return Dir::Reverse;
	return Dir::Free;
}

inline Need need(bool hasOutputEnd, bool hasInputEnd) {
	if (hasOutputEnd && !hasInputEnd)
		return Need::Input;
	if (hasInputEnd && !hasOutputEnd)
		return Need::Output;
	return Need::Nothing;
}

/** Which port is active under each smart jack. A free channel offers what the dragged cable needs, else an output. */
inline Roles roles(Dir dir, Need dragging) {
	switch (dir) {
	case Dir::Forward:
		return {Role::In, Role::Out};
	case Dir::Reverse:
		return {Role::Out, Role::In};
	default: {
		const Role r = dragging == Need::Input ? Role::In : Role::Out;
		return {r, r};
	}
	}
}

/** Role of our end of a cable dragged out of a free jack: it turns into an input while a foreign output
is hovered, and back into an output while a foreign input is hovered. */
inline Role ownedDragRole(Role current, bool hoveringForeignOutput, bool hoveringForeignInput) {
	if (current == Role::Out && hoveringForeignOutput)
		return Role::In;
	if (current == Role::In && hoveringForeignInput)
		return Role::Out;
	return current;
}

} // namespace jxmp
```

- [ ] **Step 4: Run test to verify it passes**

Run: `plugins/JXMP/tests/run.sh`
Expected: `patchbay logic: all tests passed`

- [ ] **Step 5: Commit**

```bash
git add plugins/JXMP/src/PatchbayLogic.hpp plugins/JXMP/tests
git commit -m "Add Patchbay direction logic with tests"
```

---

### Task 3: Per-port sides in the PatchCore view

**Files:**
- Create: `include/patchcore/SidedModuleWidget.hpp`
- Modify: `src/patchcore/PatchCore.hpp` (declarations, remove `frontCables` and `hideFrontPorts`)
- Modify: `src/patchcore/PatchCore.cpp:78-80,114-115,129` (remove `hideFrontPorts`, `frontCables` load/save; add `portSide`, `hasBackPorts`, `cableSide`)
- Modify: `src/override/ModuleWidget.cpp:280-305` (sync), `:307-320` (caps), `:417` (draw)
- Modify: `src/patchcore/BackPanel.cpp:75-82` (labels only for back ports)
- Modify: `src/patchcore/RackFrame.cpp:117` (per-cable visibility)
- Modify: `src/override/MenuBar.cpp:684-688` (remove the "Sockets and cables on the front" item)
- Regenerate: `src/override/diffs/ModuleWidget.cpp.diff`, `src/override/diffs/MenuBar.cpp.diff`

**Interfaces:**
- Produces: `rack::patchcore::PortSide { SIDE_FRONT = 0, SIDE_BACK }`, `rack::patchcore::SidedModuleWidget` with `virtual PortSide portSide(engine::Port::Type type, int portId) const`; `PortSide patchcore::portSide(app::PortWidget*)`, `bool patchcore::hasBackPorts(app::ModuleWidget*)`, `PortSide patchcore::cableSide(app::CableWidget*)`.
- Note: the spec puts the interface on the module; it is on the **ModuleWidget** instead, so browser previews (no module instance) know their sides too. Update the spec's "Port sides" section accordingly in Step 7.

- [ ] **Step 1: Add the interface header**

`include/patchcore/SidedModuleWidget.hpp`:

```cpp
/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <engine/Port.hpp>

namespace rack {
namespace patchcore {

enum PortSide {
	SIDE_FRONT = 0,
	SIDE_BACK,
};

/** Implemented by the widgets of JXMP modules, next to app::ModuleWidget.
Widgets without it belong to basic modules: every port on the front. */
struct SidedModuleWidget {
	virtual ~SidedModuleWidget() {}
	/** Side of an input or output. Called every frame from the UI thread: keep it cheap and constant. */
	virtual PortSide portSide(engine::Port::Type type, int portId) const {
		return SIDE_BACK;
	}
};

} // namespace patchcore
} // namespace rack
```

- [ ] **Step 2: Declare the helpers, drop the old setting**

In `src/patchcore/PatchCore.hpp`: add `#include <patchcore/SidedModuleWidget.hpp>` after `#include <window/Window.hpp>`; add `struct CableWidget;` and `struct PortWidget;` to the `namespace app` forward declarations; delete the `frontCables` member with its comment and the `hideFrontPorts()` declaration with its comment. Below `isBackView()` add:

```cpp
/** Side of a socket: what its module widget says, SIDE_FRONT for basic modules. */
PortSide portSide(app::PortWidget* port);

/** True when the module widget is a SidedModuleWidget with at least one back port. */
bool hasBackPorts(app::ModuleWidget* mw);

/** Side a cable is drawn on: the side of its ports. A cable whose ends disagree (hand-edited patch) is on the front. */
PortSide cableSide(app::CableWidget* cw);
```

- [ ] **Step 3: Implement them**

In `src/patchcore/PatchCore.cpp`: add `#include <app/CableWidget.hpp>`, `#include <app/ModuleWidget.hpp>`, `#include <app/PortWidget.hpp>` if missing; delete `hideFrontPorts()` (lines 78-80), the `frontCables` read (114-115) and write (129). Add after `isBackView()`:

```cpp
PortSide portSide(app::PortWidget* const port) {
	app::ModuleWidget* const mw = port->getAncestorOfType<app::ModuleWidget>();
	if (const SidedModuleWidget* const sided = dynamic_cast<const SidedModuleWidget*>(mw))
		return sided->portSide(port->type, port->portId);
	return SIDE_FRONT;
}


bool hasBackPorts(app::ModuleWidget* const mw) {
	if (dynamic_cast<SidedModuleWidget*>(mw) == nullptr)
		return false;
	for (app::PortWidget* const port : mw->getPorts()) {
		if (portSide(port) == SIDE_BACK)
			return true;
	}
	return false;
}


PortSide cableSide(app::CableWidget* const cw) {
	if (cw->outputPort != nullptr && cw->inputPort != nullptr)
		return portSide(cw->outputPort) == portSide(cw->inputPort) ? portSide(cw->outputPort) : SIDE_FRONT;
	if (cw->outputPort != nullptr)
		return portSide(cw->outputPort);
	if (cw->inputPort != nullptr)
		return portSide(cw->inputPort);
	return SIDE_FRONT;
}
```

- [ ] **Step 4: Per-port view sync, caps and draw**

In `src/override/ModuleWidget.cpp`, replace the body of `ModuleWidget_syncPatchCoreView` from `const int view = ...` to the end of the `else if (view == 2)` block with:

```cpp
	const int view = !patchcore::enabled() ? 0 : patchcore::isBackView() ? 2 : 1;
	if (view == internal->patchcoreView && mw->children.size() == internal->patchcoreChildCount)
		return;

	// Undo the previous view. Only widgets that are still children get shown again.
	ModuleWidget_showHiddenRecursive(mw, internal->patchcoreHidden);
	internal->patchcoreHidden.clear();

	if (view != 0) {
		// Back: keep only back sockets (and containers holding them), everything else belongs to the front
		if (view == 2)
			ModuleWidget_hideNonPorts(mw, internal->patchcoreHidden);

		// Each socket shows on its own side only
		const patchcore::PortSide shown = view == 2 ? patchcore::SIDE_BACK : patchcore::SIDE_FRONT;
		doIfTypeRecursive<PortWidget>(mw, [&](PortWidget* port) {
			if (port->isVisible() && patchcore::portSide(port) != shown) {
				port->hide();
				internal->patchcoreHidden.push_back(port);
			}
		});
	}
```

In `ModuleWidget_drawBlankingCaps`, inside the `for (PortWidget* const port : mw->getPorts())` loop, add as first statement:

```cpp
			if (patchcore::portSide(port) != patchcore::SIDE_BACK)
				continue;
```

In `ModuleWidget::draw`, replace `if (patchcore::hideFrontPorts()) {` with:

```cpp
	if (patchcore::enabled() && patchcore::hasBackPorts(this)) {
```

- [ ] **Step 5: Back labels, cable visibility, menu**

`src/patchcore/BackPanel.cpp`, in `collectPorts`, change the port branch to:

```cpp
		if (app::PortWidget* const port = dynamic_cast<app::PortWidget*>(child)) {
			if (portSide(port) == SIDE_BACK)
				ports.push_back({port, math::Rect(port->getRelativeOffset(math::Vec(), mw), port->box.size)});
		}
```

`src/patchcore/RackFrame.cpp`, replace `rack->getCableContainer()->setVisible(!hideFrontPorts());` and its comment line above the loop with (keep the module loop):

```cpp
		// Each cable lives on the side of its sockets
		for (widget::Widget* const w : rack->getCableContainer()->children) {
			if (app::CableWidget* const cw = dynamic_cast<app::CableWidget*>(w))
				cw->setVisible(!enabled() || (int) cableSide(cw) == (int) viewSide);
		}
```

(add `#include <app/CableWidget.hpp>` if missing). Update the comment above the module loop to `// Front/back: each module shows the sockets of the current side`.

`src/override/MenuBar.cpp`: delete the whole `createCheckMenuItem("Sockets and cables on the front", ...)` block (lines 684-688). Regenerate the diffs of `ModuleWidget.cpp` and `MenuBar.cpp` (see Global Constraints).

Check nothing else uses the removed names:
Run: `grep -rn "hideFrontPorts\|frontCables" src include --include=*.cpp --include=*.hpp | grep -v diffs/`
Expected: no output.

- [ ] **Step 6: Build and check by hand**

Run: `make -j12 jack 2>&1 | grep -E "error|Error" ; ./bin/CardinalJACK`
Expected, PatchCore look:
- A patch of basic modules (e.g. Fundamental VCO → VCF → Audio): sockets and cables on the front, like Classic. After `Tab`: blank plates, no cables.
- Classic theme (View menu): identical to Task 1 screenshots.
- `Tab` pressed while dragging a cable: no crash; the cable is visible only on the front.
- Module browser opens and previews render.
(JXMP-specific checks come in Task 5.)

- [ ] **Step 7: Update the spec and commit**

In `research_docs/04-sided-modules-patchbay-design.md`, section "Port sides": rename the interface to `SidedModuleWidget`, implemented by the module **widget** (`struct MyWidget : app::ModuleWidget, patchcore::SidedModuleWidget`), header `include/patchcore/SidedModuleWidget.hpp`, with the reason (browser previews have no module). In "Cables", replace the "second line of defence" sentence with: "Ports on the other side are hidden, so a cross-side cable cannot be made in the UI."

```bash
git add include/patchcore src/patchcore src/override research_docs/04-sided-modules-patchbay-design.md
git commit -m "PatchCore: sockets and cables per port side; basic modules stay on the front"
```

---

### Task 4: JXMP plugin skeleton with Patchbay 4 (engine + plain jacks)

**Files:**
- Create: `plugins/JXMP/plugin.json`
- Create: `plugins/JXMP/src/plugin.hpp`
- Create: `plugins/JXMP/src/Patchbay.cpp`
- Create: `plugins/JXMP/res/Panel4HP.svg`, `plugins/JXMP/res/Panel8HP.svg`
- Modify: `plugins/Makefile` (after the Cardinal block, ~line 280; build rule after the `$(BUILD_DIR)/Cardinal/%.cpp.o` rule, ~line 2136)
- Modify: `plugins/plugins.cpp` (include ~line 14, instance ~line 957, init function after `initStatic__Cardinal`, call in `initStaticPlugins`)

**Interfaces:**
- Consumes: `patchcore::SidedModuleWidget`, `patchcore::SIDE_FRONT/SIDE_BACK` (Task 3).
- Produces: `Patchbay<N>` module with enums `FRONT_INPUT/BACK_INPUT` (inputs), `FRONT_OUTPUT/BACK_OUTPUT` (outputs), `FORWARD_LIGHT/REVERSE_LIGHT` (lights), each `ENUMS(..., N)`; `PatchbayWidget<N>`; `SmartPort` (Task 5 adds behaviour); models `modelJXMPPatchbay4/8/16`.

- [ ] **Step 1: Plugin manifest and header**

`plugins/JXMP/plugin.json`:

```json
{
  "slug": "JXMP",
  "name": "JXMP",
  "version": "2.0.0",
  "license": "GPL-3.0-or-later",
  "brand": "JXMP Audio",
  "author": "JXMP Audio",
  "authorEmail": "",
  "authorUrl": "https://github.com/JXMP-Audio",
  "pluginUrl": "https://github.com/JXMP-Audio/PatchCore",
  "manualUrl": "https://github.com/JXMP-Audio/PatchCore/blob/main/research_docs/04-sided-modules-patchbay-design.md",
  "sourceUrl": "https://github.com/JXMP-Audio/PatchCore",
  "changelogUrl": "",
  "modules": [
    {"slug": "Patchbay4", "name": "Patchbay 4", "description": "Passes 4 channels between the front and the back of the rack", "tags": ["Utility"]},
    {"slug": "Patchbay8", "name": "Patchbay 8", "description": "Passes 8 channels between the front and the back of the rack", "tags": ["Utility"]},
    {"slug": "Patchbay16", "name": "Patchbay 16", "description": "Passes 16 channels between the front and the back of the rack", "tags": ["Utility"]}
  ]
}
```

`plugins/JXMP/src/plugin.hpp`:

```cpp
/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "rack.hpp"
#include <patchcore/SidedModuleWidget.hpp>

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelJXMPPatchbay4;
extern Model* modelJXMPPatchbay8;
extern Model* modelJXMPPatchbay16;
```

- [ ] **Step 2: Panels**

`plugins/JXMP/res/Panel4HP.svg` (20.32 × 128.5 mm). Text is drawn by the widget, nanosvg does not render SVG text:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="20.32mm" height="128.5mm" viewBox="0 0 20.32 128.5">
  <rect width="20.32" height="128.5" fill="#24262b"/>
  <rect x="0.4" y="0.4" width="19.52" height="127.7" fill="none" stroke="#3a3d44" stroke-width="0.4"/>
  <rect x="0" y="0" width="20.32" height="16" fill="#1b1c20"/>
  <rect x="0" y="118" width="20.32" height="10.5" fill="#1b1c20"/>
</svg>
```

`plugins/JXMP/res/Panel8HP.svg`: identical with `width="40.64mm"`, `viewBox="0 0 40.64 128.5"`, outer rect `width="40.64"`, inner stroke rect `width="39.84"`, both bars `width="40.64"`, plus a column divider `<line x1="20.32" y1="18" x2="20.32" y2="116" stroke="#3a3d44" stroke-width="0.3"/>`.

- [ ] **Step 3: Module, widget and models**

`plugins/JXMP/src/Patchbay.cpp`:

```cpp
/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plugin.hpp"
#include "PatchbayLogic.hpp"

template <int N>
struct Patchbay : engine::Module {
	enum ParamIds { NUM_PARAMS };
	enum InputIds { ENUMS(FRONT_INPUT, N), ENUMS(BACK_INPUT, N), NUM_INPUTS };
	enum OutputIds { ENUMS(FRONT_OUTPUT, N), ENUMS(BACK_OUTPUT, N), NUM_OUTPUTS };
	enum LightIds { ENUMS(FORWARD_LIGHT, N), ENUMS(REVERSE_LIGHT, N), NUM_LIGHTS };

	Patchbay() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int k = 0; k < N; ++k) {
			configInput(FRONT_INPUT + k, string::f("Ch %d", k + 1));
			configInput(BACK_INPUT + k, string::f("Ch %d", k + 1));
			configOutput(FRONT_OUTPUT + k, string::f("Ch %d", k + 1));
			configOutput(BACK_OUTPUT + k, string::f("Ch %d", k + 1));
		}
	}

	static void pass(engine::Input& in, engine::Output& out) {
		const int channels = in.getChannels();
		out.setChannels(channels);
		if (channels > 0)
			out.writeVoltages(in.getVoltages());
	}

	/** Dim when patched, brighter with signal. */
	static float level(engine::Input& in) {
		if (!in.isConnected())
			return 0.f;
		return math::clamp(std::fabs(in.getVoltageSum()) / 5.f, 0.25f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		for (int k = 0; k < N; ++k) {
			pass(inputs[FRONT_INPUT + k], outputs[BACK_OUTPUT + k]);
			pass(inputs[BACK_INPUT + k], outputs[FRONT_OUTPUT + k]);
			lights[FORWARD_LIGHT + k].setBrightnessSmooth(level(inputs[FRONT_INPUT + k]), args.sampleTime);
			lights[REVERSE_LIGHT + k].setBrightnessSmooth(level(inputs[BACK_INPUT + k]), args.sampleTime);
		}
	}
};


/** One half of a smart jack. Inactive halves have no hit box and draw nothing. */
struct SmartPort : PJ301MPort {
	math::Vec center;
	math::Vec fullSize;
	bool active = true;

	void setActive(const bool a) {
		if (a == active)
			return;
		active = a;
		box.size = a ? fullSize : math::Vec();
		box.pos = a ? center.minus(fullSize.div(2)) : center;
	}

	void draw(const DrawArgs& args) override {
		if (active)
			PJ301MPort::draw(args);
	}
};


template <int N>
struct PatchbayLabels : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, nvgRGB(0xd8, 0xdb, 0xe0));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFontSize(args.vg, 9.f);
		nvgText(args.vg, box.size.x / 2, mm2px(8.f), "PATCHBAY", nullptr);
		nvgFontSize(args.vg, 8.f);
		nvgText(args.vg, box.size.x / 2, mm2px(123.f), "JXMP", nullptr);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFontSize(args.vg, 7.f);
		for (int k = 0; k < N; ++k) {
			const math::Vec p = jackPos(k);
			nvgText(args.vg, p.x - mm2px(7.8f), p.y, string::f("%d", k + 1).c_str(), nullptr);
		}
	}

	static math::Vec jackPos(const int k) {
		const int rows = N == 16 ? 8 : N;
		const float pitch = N == 4 ? 22.f : 12.f;
		const float top = N == 4 ? 30.f : 24.f;
		const float x = (N == 16 && k >= 8) ? 29.32f : 9.f;
		return mm2px(math::Vec(x, top + (k % rows) * pitch));
	}
};


template <int N>
struct PatchbayWidget : app::ModuleWidget, patchcore::SidedModuleWidget {
	typedef Patchbay<N> M;
	SmartPort* frontIn[N];
	SmartPort* frontOut[N];
	SmartPort* backIn[N];
	SmartPort* backOut[N];

	PatchbayWidget(M* const module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, N == 16 ? "res/Panel8HP.svg" : "res/Panel4HP.svg")));

		PatchbayLabels<N>* const labels = new PatchbayLabels<N>;
		labels->box.size = box.size;
		addChild(labels);

		for (int k = 0; k < N; ++k) {
			const math::Vec p = PatchbayLabels<N>::jackPos(k);
			frontIn[k] = addSmartInput(p, M::FRONT_INPUT + k);
			frontOut[k] = addSmartOutput(p, M::FRONT_OUTPUT + k);
			backIn[k] = addSmartInput(p, M::BACK_INPUT + k);
			backOut[k] = addSmartOutput(p, M::BACK_OUTPUT + k);
			addChild(createLightCentered<TinyLight<GreenLight>>(p.plus(mm2px(math::Vec(7.f, -2.2f))), module, M::FORWARD_LIGHT + k));
			addChild(createLightCentered<TinyLight<YellowLight>>(p.plus(mm2px(math::Vec(7.f, 2.2f))), module, M::REVERSE_LIGHT + k));
		}
	}

	SmartPort* addSmartInput(const math::Vec pos, const int id) {
		SmartPort* const port = createInputCentered<SmartPort>(pos, module, id);
		port->center = pos;
		port->fullSize = port->box.size;
		addInput(port);
		return port;
	}

	SmartPort* addSmartOutput(const math::Vec pos, const int id) {
		SmartPort* const port = createOutputCentered<SmartPort>(pos, module, id);
		port->center = pos;
		port->fullSize = port->box.size;
		addOutput(port);
		return port;
	}

	patchcore::PortSide portSide(const engine::Port::Type, const int portId) const override {
		// Front ids come first in both port enums
		return portId < N ? patchcore::SIDE_FRONT : patchcore::SIDE_BACK;
	}
};


Model* modelJXMPPatchbay4 = createModel<Patchbay<4>, PatchbayWidget<4>>("Patchbay4");
Model* modelJXMPPatchbay8 = createModel<Patchbay<8>, PatchbayWidget<8>>("Patchbay8");
Model* modelJXMPPatchbay16 = createModel<Patchbay<16>, PatchbayWidget<16>>("Patchbay16");
```

In this task all four SmartPorts of a channel are active and stacked (only the topmost, the back output, gets clicks). Task 5 makes one active per side.

- [ ] **Step 4: Register the plugin**

`plugins/Makefile`, after the last `PLUGIN_FILES += Cardinal/...` line of the Cardinal block:

```make
# --------------------------------------------------------------
# JXMP (built-in)

PLUGIN_FILES += $(wildcard JXMP/src/*.cpp)
```

and after the `$(BUILD_DIR)/Cardinal/%.cpp.o` rule:

```make
$(BUILD_DIR)/JXMP/%.cpp.o: JXMP/%.cpp
	-@mkdir -p "$(shell dirname $(BUILD_DIR)/$<)"
	@echo "Compiling $<"
	$(SILENT)$(CXX) $< $(BUILD_CXX_FLAGS) -c -o $@ \
		-DpluginInstance=pluginInstance__JXMP
```

`plugins/plugins.cpp`: after `#include "Cardinal/src/plugin.hpp"` add

```cpp

// JXMP (built-in)
#include "JXMP/src/plugin.hpp"
```

after `Plugin* pluginInstance__Cardinal;` add `Plugin* pluginInstance__JXMP;`, after the `initStatic__Cardinal()` function add

```cpp
static void initStatic__JXMP()
{
    Plugin* const p = new Plugin;
    pluginInstance__JXMP = p;

    const StaticPluginLoader spl(p, "JXMP");
    if (spl.ok())
    {
        p->addModel(modelJXMPPatchbay4);
        p->addModel(modelJXMPPatchbay8);
        p->addModel(modelJXMPPatchbay16);
    }
}
```

and in `initStaticPlugins()` call `initStatic__JXMP();` right after `initStatic__Cardinal();`.

- [ ] **Step 5: Build and check by hand**

Run: `make -j12 jack 2>&1 | grep -E "error|Error"; ./bin/CardinalJACK`
Expected:
- The browser lists brand "JXMP Audio" with Patchbay 4, 8 and 16.
- Patchbay front: numbers, title and LEDs; jacks visible. After `Tab`: a plate with jacks at the same positions, labels `CH 1`… .
- Front jack → VCO output works for at least one channel (signal reaches the back), even though roles are not managed yet.

- [ ] **Step 6: Commit**

```bash
git add plugins/JXMP plugins/Makefile plugins/plugins.cpp
git commit -m "Add the JXMP plugin with Patchbay 4/8/16"
```

---

### Task 5: Smart jack (spike first)

**Files:**
- Modify: `plugins/JXMP/src/Patchbay.cpp` (`PatchbayWidget::step`)

**Interfaces:**
- Consumes: `jxmp::direction/need/roles/ownedDragRole` (Task 2), `SmartPort::setActive` (Task 4), `RackWidget::getIncompleteCable()`, `RackWidget::getCablesOnPort()`.

- [ ] **Step 1: Add role management to the widget**

Add to `PatchbayWidget`:

```cpp
	static bool hasCompleteCable(app::PortWidget* const port) {
		for (app::CableWidget* const cw : APP->scene->rack->getCablesOnPort(port)) {
			if (cw->isComplete())
				return true;
		}
		return false;
	}

	bool ownsPort(app::PortWidget* const port, const int k) const {
		return port == frontIn[k] || port == frontOut[k] || port == backIn[k] || port == backOut[k];
	}

	void step() override {
		app::CableWidget* const cw = APP->scene->rack->getIncompleteCable();
		const jxmp::Need dragging = cw ? jxmp::need(cw->outputPort != nullptr, cw->inputPort != nullptr) : jxmp::Need::Nothing;

		for (int k = 0; k < N; ++k) {
			jxmp::ChannelCables c;
			c.frontIn = hasCompleteCable(frontIn[k]);
			c.frontOut = hasCompleteCable(frontOut[k]);
			c.backIn = hasCompleteCable(backIn[k]);
			c.backOut = hasCompleteCable(backOut[k]);
			const jxmp::Dir dir = jxmp::direction(c);

			// A cable dragged out of this free channel: its end follows what the user hovers
			if (dir == jxmp::Dir::Free && cw != nullptr && (ownsPort(cw->outputPort, k) || ownsPort(cw->inputPort, k))) {
				const bool front = cw->outputPort == frontOut[k] || cw->inputPort == frontIn[k];
				const jxmp::Role current = cw->outputPort != nullptr ? jxmp::Role::Out : jxmp::Role::In;
				const bool foreignOut = cw->hoveredOutputPort != nullptr && !ownsPort(cw->hoveredOutputPort, k);
				const bool foreignIn = cw->hoveredInputPort != nullptr && !ownsPort(cw->hoveredInputPort, k);
				const jxmp::Role role = jxmp::ownedDragRole(current, foreignOut, foreignIn);
				if (role != current) {
					cw->outputPort = role == jxmp::Role::Out ? (front ? frontOut[k] : backOut[k]) : nullptr;
					cw->inputPort = role == jxmp::Role::In ? (front ? frontIn[k] : backIn[k]) : nullptr;
					cw->updateCable();
				}
				setRole(front ? frontIn[k] : backIn[k], front ? frontOut[k] : backOut[k], role);
				continue;
			}

			const jxmp::Roles r = jxmp::roles(dir, dragging);
			setRole(frontIn[k], frontOut[k], r.front);
			setRole(backIn[k], backOut[k], r.back);
		}

		ModuleWidget::step();
	}

	static void setRole(SmartPort* const in, SmartPort* const out, const jxmp::Role role) {
		in->setActive(role == jxmp::Role::In);
		out->setActive(role == jxmp::Role::Out);
	}
```

Delete the sentence "In this task all four SmartPorts…" note if you copied it into a comment.

- [ ] **Step 2: Build**

Run: `make -j12 jack 2>&1 | grep -E "error|Error"`
Expected: no output.

- [ ] **Step 3: Spike — forward path**

Run `./bin/CardinalJACK`. Add a VCO (Fundamental), Patchbay 4, and an Audio module. Drag from VCO SIN output to Patchbay front jack 1.
Expected: cable connects; green LED 1 lights. `Tab`: back jack 1 is an output; drag from it to… (there is no JXMP sink yet) a second Patchbay's back jack 1, then from its front jack 1 to Audio input: you hear the VCO.

- [ ] **Step 4: Spike — reverse path and drag-from-free-jack**

Drag *from* a free Patchbay front jack 2 to the Audio module input.
Expected: the cable connects (front 2 is an output, reverse direction, yellow LED once the back gets a source). Drag *from* a free front jack 3 to a VCO output: the end turns into an input while hovering the output and connects on drop.

- [ ] **Step 5: Spike verdict**

If Steps 3–4 work, continue. If dragging from a free jack or dropping onto a smart jack fails in a way the code above can't fix in about an hour, stop and report to the user with what failed; the spec's fallback is a per-channel direction switch (a separate task to plan then).

- [ ] **Step 6: Lock rules**

Expected (by hand): with front 1 patched from a VCO, hovering front 1 with another output cable dims it and refuses the drop; the back 1 jack accepts several output cables. Removing all cables from channel 1 makes it free again (dragging from front 1 offers an output).

- [ ] **Step 7: Rack interactions (Review Focus 2, 3)**

Expected (by hand), on a Patchbay jack with a cable:
- plain drag picks up the cable and it can be dropped elsewhere;
- Ctrl-drag on a back output with cables creates a new cable; Ctrl-drag on a front input clones the cable;
- Shift-click deletes the top cable;
- Ctrl+Z after connecting a cable through a Patchbay removes it, Ctrl+Shift+Z brings it back, and the LEDs follow;
- save the patch, reload: cables and LEDs come back.

- [ ] **Step 8: Polyphony**

Patch a polyphonic source (e.g. Fundamental Merge with 4 channels) through a Patchbay into a polyphonic input (e.g. Fundamental Split). Expected: 4 channels arrive.

- [ ] **Step 9: Commit**

```bash
git add plugins/JXMP/src/Patchbay.cpp
git commit -m "Patchbay: smart jacks pick their role from the cables"
```

---

### Task 6: Docs and collection

**Files:**
- Modify: `research_docs/03-work-items.md` (mark B-1…B-4 as done or replaced, add a "Sided modules" section with S-1 interface, S-2 Patchbay)
- Modify: `research_docs/README.md` (add this plan)
- Modify: `docs/CARDINAL-MODULES.md` is upstream's; do not edit. Instead add `plugins/JXMP/README.md`:

```markdown
# JXMP modules

Built-in modules by JXMP Audio for PatchCore. Their sockets are on the back of the rack
(press `Tab`), except where a module puts them on the front.

| Module | What it does |
|---|---|
| Patchbay 4 / 8 / 16 | Passes channels between the front and the back. Plug something into front jack *n* and it comes out of back jack *n*, or the other way round. The first cable decides the direction; the LEDs show it (green → back, yellow → front). |

Tests for pure logic: `plugins/JXMP/tests/run.sh`.
```

- [ ] **Step 1: Write the docs above and run the tests once more**

Run: `plugins/JXMP/tests/run.sh`
Expected: `patchbay logic: all tests passed`

- [ ] **Step 2: Commit, push, bump the collection**

```bash
git add research_docs plugins/JXMP/README.md
git commit -m "Docs: sided modules and the Patchbay"
git push origin main
cd .. && ./update-plugins.sh && git push
```
