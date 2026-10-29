/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <engine/Port.hpp>
#include <nanovg.h>

#include <string>

namespace rack {
namespace patchcore {

enum PortSide {
	SIDE_FRONT = 0,
	SIDE_BACK,
};

/** True when the PatchCore look is active, so sides exist. Defined in src/patchcore/PatchCore.cpp. */
bool enabled();

/** Implemented by the widgets of JXMP modules, next to app::ModuleWidget.
Widgets without it belong to basic modules: every port on the front. */
struct SidedModuleWidget {
	virtual ~SidedModuleWidget() {}
	/** Side of an input or output. Called every frame from the UI thread: keep it cheap and constant. */
	virtual PortSide portSide(engine::Port::Type type, int portId) const {
		return SIDE_BACK;
	}
	/** Draws the whole back plate (and jack labels) itself, in module coordinates, instead of the generated one.
	Return false to keep the generated plate. The sockets are drawn on top afterwards. */
	virtual bool drawStyledBack(NVGcontext* vg) {
		(void) vg;
		return false;
	}
	/** Text for the serial plate on the generated back. Empty: the generic sticker. */
	virtual std::string serialLabel() const {
		return "";
	}
};

} // namespace patchcore
} // namespace rack
