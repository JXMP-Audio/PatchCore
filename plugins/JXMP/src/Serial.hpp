/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
