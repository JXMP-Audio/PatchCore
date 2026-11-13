/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL menu, no Rack dependency: the folders and how the four buttons move through them. MENU opens it, goes one
 * folder back, and closes it at the top; ▲ ▼ move the highlight (wrapping); OK opens a folder or runs an item, which
 * closes the menu. An item that asks (CLEAR SAMPLE) wants a second OK; MENU then says no.
 */

#pragma once

#include <string>

namespace jxmp {
namespace sampler {

enum MenuAction { A_NONE, A_CROP, A_NORMALIZE, A_REVERSE, A_XFADE, A_SNAP, A_SEAM, A_ZOOM_LOOP, A_WHOLE, A_LOAD, A_DETECT, A_CLEAR };

struct MenuItem {
	const char* name;
	MenuAction action;
	float value;          // CROSSFADE: its length in ms
	const MenuItem* sub;  // a folder: its items
	int subCount;
	bool ask;             // asks before it runs
};

struct MenuResult {
	MenuAction action = A_NONE;
	float value = 0.f;
};

namespace menu {
static const MenuItem kEdit[] = {
    {"CROP TO S-E", A_CROP, 0.f, nullptr, 0, false},
    {"NORMALIZE", A_NORMALIZE, 0.f, nullptr, 0, false},
    {"REVERSE S-E", A_REVERSE, 0.f, nullptr, 0, false},
};
static const MenuItem kXfade[] = {
    {"5 MS", A_XFADE, 5.f, nullptr, 0, false},
    {"10 MS", A_XFADE, 10.f, nullptr, 0, false},
    {"20 MS", A_XFADE, 20.f, nullptr, 0, false},
    {"50 MS", A_XFADE, 50.f, nullptr, 0, false},
};
static const MenuItem kLoop[] = {
    {"CROSSFADE", A_NONE, 0.f, kXfade, 4, false},
    {"SNAP TO ZERO", A_SNAP, 0.f, nullptr, 0, false},
    {"SHOW SEAM", A_SEAM, 0.f, nullptr, 0, false},
};
static const MenuItem kView[] = {
    {"ZOOM TO LOOP", A_ZOOM_LOOP, 0.f, nullptr, 0, false},
    {"WHOLE SAMPLE", A_WHOLE, 0.f, nullptr, 0, false},
};
static const MenuItem kSample[] = {
    {"LOAD SAMPLE", A_LOAD, 0.f, nullptr, 0, false},
    {"DETECT ROOT", A_DETECT, 0.f, nullptr, 0, false},
    {"CLEAR SAMPLE", A_CLEAR, 0.f, nullptr, 0, true},
};
static const MenuItem kRoot[] = {
    {"EDIT", A_NONE, 0.f, kEdit, 3, false},
    {"LOOP", A_NONE, 0.f, kLoop, 3, false},
    {"VIEW", A_NONE, 0.f, kView, 2, false},
    {"SAMPLE", A_NONE, 0.f, kSample, 3, false},
};
} // namespace menu

struct Menu {
	static constexpr int kDepth = 4;
	bool open = false, confirming = false;
	int depth = 0;
	const MenuItem* items[kDepth] = {menu::kRoot};
	int counts[kDepth] = {4};
	int sel[kDepth] = {};
	const char* names[kDepth] = {};
	float lastXfade = 20.f;

	const MenuItem* current() const { return items[depth]; }
	int count() const { return counts[depth]; }
	int selected() const { return sel[depth]; }

	/** "LOOP > CROSSFADE" inside folders, empty at the top */
	std::string path() const {
		std::string p;
		for (int d = 1; d <= depth; ++d) {
			if (d > 1)
				p += " > ";
			p += names[d];
		}
		return p;
	}

	void pressMenu() {
		if (!open) {
			open = true;
			depth = 0;
			sel[0] = 0;
		}
		else if (confirming)
			confirming = false;
		else if (depth > 0)
			--depth;
		else
			open = false;
	}
	void up() {
		if (open && !confirming)
			sel[depth] = (sel[depth] + counts[depth] - 1) % counts[depth];
	}
	void down() {
		if (open && !confirming)
			sel[depth] = (sel[depth] + 1) % counts[depth];
	}
	MenuResult ok() {
		MenuResult r;
		if (!open)
			return r;
		const MenuItem& it = items[depth][sel[depth]];
		if (it.sub && depth + 1 < kDepth) {
			++depth;
			items[depth] = it.sub;
			counts[depth] = it.subCount;
			names[depth] = it.name;
			sel[depth] = 0;
			for (int k = 0; k < it.subCount; ++k)
				if (it.sub[k].action == A_XFADE && it.sub[k].value == lastXfade)
					sel[depth] = k;
			return r;
		}
		if (it.ask && !confirming) {
			confirming = true;
			return r;
		}
		confirming = false;
		open = false;
		r.action = it.action;
		r.value = it.value;
		if (it.action == A_XFADE)
			lastXfade = it.value;
		return r;
	}
};

} // namespace sampler
} // namespace jxmp
