/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace jxmp {

enum FilterMode { LP = 0, BP, HP, NOTCH };

/** Multimode response from the 4 ladder stages. `u` = input minus resonance feedback, `s` = stage outputs.
`steep24` picks the 4-pole (24 dB) mix, else the 2-pole (12 dB) one. Templated so it runs on SIMD lanes too. */
template <typename T>
inline T ladderTap(FilterMode mode, bool steep24, T u, const T s[4]) {
	switch (mode) {
	case BP:
		return steep24 ? T(4) * s[1] - T(8) * s[2] + T(4) * s[3] : T(2) * s[0] - T(2) * s[1];
	case HP:
		return steep24 ? u - T(4) * s[0] + T(6) * s[1] - T(4) * s[2] + s[3] : u - T(2) * s[0] + s[1];
	case NOTCH:
		return steep24 ? u - T(4) * s[0] + T(6) * s[1] - T(4) * s[2] + T(2) * s[3] : u - T(2) * s[0] + T(2) * s[1];
	default:
		return steep24 ? s[3] : s[1];
	}
}

} // namespace jxmp
