/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
