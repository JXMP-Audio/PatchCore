/*
 * PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Space on the standalone transport: one press plays / pauses, a second press within 0.35 s stops and goes back to
 * the start (like the stop button). No Rack dependency (tests/test_space_taps.cpp).
 */

#pragma once

namespace patchcore {

struct SpaceTaps {
	enum Action { TOGGLE, STOP };
	static constexpr double kDouble = 0.35;
	double last = -1e9;

	/** A Space press at `now` seconds: what the transport does */
	Action press(double now) {
		if (now - last < kDouble) {
			last = -1e9;  // a third press starts afresh
			return STOP;
		}
		last = now;
		return TOGGLE;
	}
};

} // namespace patchcore
