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
