/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST CV slots, no Rack dependency. A slot is bound to one plugin parameter (normalised 0..1). Its base value is
 * what the plugin's own knob says; a patched CV adds depth * cv / 10 V of the RANGE, clamped to the RANGE.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace jxmp {
namespace nest {

struct CvSlot {
	int param = -1;  // -1 = no parameter
	std::string paramName;
	float base = 0.f, rangeMin = 0.f, rangeMax = 1.f;
	float last = -1.f;  // the value sent last (-1 = nothing yet)
};

/** depth -1..1, cv in volts */
inline float slotTarget(const CvSlot& s, float depth, float cv, bool patched) {
	const float lo = std::min(s.rangeMin, s.rangeMax), hi = std::max(s.rangeMin, s.rangeMax);
	if (!patched)
		return s.base;
	const float v = s.base + depth * cv / 10.f * (hi - lo);
	return std::min(std::max(v, lo), hi);
}

/** Whether the target differs from what was sent, so an unchanged CV sends nothing */
inline bool cvMoved(const CvSlot& s, float target) {
	return s.param >= 0 && (s.last < 0.f || std::fabs(target - s.last) > 1e-4f);
}

} // namespace nest
} // namespace jxmp
