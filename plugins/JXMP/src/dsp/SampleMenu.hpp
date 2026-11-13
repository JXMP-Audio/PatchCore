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

enum MenuAction {
	A_NONE, A_CROP, A_NORMALIZE, A_REVERSE, A_XFADE, A_SNAP, A_SEAM, A_ZOOM_LOOP, A_WHOLE, A_LOAD, A_DETECT, A_CLEAR,
	A_SHOW_WAVE,
	// Envelopes: the value says which one (0 = volume, 1 = pan)
	A_ENV_SHOW, A_ENV_ONOFF, A_ENV_SUS, A_ENV_LOOP_START, A_ENV_LOOP_END, A_ENV_SUS_OFF, A_ENV_LOOP_OFF, A_ENV_RESET,
	A_FADE,  // the value: seconds, 0 = off
	A_RECORD, A_BROWSE
};

struct MenuItem {
	const char* name;
	MenuAction action;
	float value;          // CROSSFADE: its length in ms; FADEOUT: seconds; envelope items: 0 volume, 1 pan
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
    {"WAVE", A_SHOW_WAVE, 0.f, nullptr, 0, false},
    {"ZOOM TO LOOP", A_ZOOM_LOOP, 0.f, nullptr, 0, false},
    {"WHOLE SAMPLE", A_WHOLE, 0.f, nullptr, 0, false},
};
static const MenuItem kEnvVol[] = {
    {"SHOW", A_ENV_SHOW, 0.f, nullptr, 0, false},
    {"ON / OFF", A_ENV_ONOFF, 0.f, nullptr, 0, false},
    {"SUSTAIN AT POINT", A_ENV_SUS, 0.f, nullptr, 0, false},
    {"LOOP START AT POINT", A_ENV_LOOP_START, 0.f, nullptr, 0, false},
    {"LOOP END AT POINT", A_ENV_LOOP_END, 0.f, nullptr, 0, false},
    {"SUSTAIN OFF", A_ENV_SUS_OFF, 0.f, nullptr, 0, false},
    {"LOOP OFF", A_ENV_LOOP_OFF, 0.f, nullptr, 0, false},
    {"RESET", A_ENV_RESET, 0.f, nullptr, 0, false},
};
static const MenuItem kEnvPan[] = {
    {"SHOW", A_ENV_SHOW, 1.f, nullptr, 0, false},
    {"ON / OFF", A_ENV_ONOFF, 1.f, nullptr, 0, false},
    {"SUSTAIN AT POINT", A_ENV_SUS, 1.f, nullptr, 0, false},
    {"LOOP START AT POINT", A_ENV_LOOP_START, 1.f, nullptr, 0, false},
    {"LOOP END AT POINT", A_ENV_LOOP_END, 1.f, nullptr, 0, false},
    {"SUSTAIN OFF", A_ENV_SUS_OFF, 1.f, nullptr, 0, false},
    {"LOOP OFF", A_ENV_LOOP_OFF, 1.f, nullptr, 0, false},
    {"RESET", A_ENV_RESET, 1.f, nullptr, 0, false},
};
static const MenuItem kFade[] = {
    {"OFF", A_FADE, 0.f, nullptr, 0, false},
    {"0.1 S", A_FADE, 0.1f, nullptr, 0, false},
    {"0.25 S", A_FADE, 0.25f, nullptr, 0, false},
    {"0.5 S", A_FADE, 0.5f, nullptr, 0, false},
    {"1 S", A_FADE, 1.f, nullptr, 0, false},
    {"2 S", A_FADE, 2.f, nullptr, 0, false},
    {"5 S", A_FADE, 5.f, nullptr, 0, false},
};
static const MenuItem kEnv[] = {
    {"VOLUME", A_NONE, 0.f, kEnvVol, 8, false},
    {"PAN", A_NONE, 0.f, kEnvPan, 8, false},
    {"FADEOUT", A_NONE, 0.f, kFade, 7, false},
};
static const MenuItem kSample[] = {
    {"RECORD", A_RECORD, 0.f, nullptr, 0, false},
    {"BROWSE", A_BROWSE, 0.f, nullptr, 0, false},
    {"LOAD SAMPLE", A_LOAD, 0.f, nullptr, 0, false},
    {"DETECT ROOT", A_DETECT, 0.f, nullptr, 0, false},
    {"CLEAR SAMPLE", A_CLEAR, 0.f, nullptr, 0, true},
};
static const MenuItem kRoot[] = {
    {"EDIT", A_NONE, 0.f, kEdit, 3, false},
    {"LOOP", A_NONE, 0.f, kLoop, 3, false},
    {"VIEW", A_NONE, 0.f, kView, 3, false},
    {"ENV", A_NONE, 0.f, kEnv, 3, false},
    {"SAMPLE", A_NONE, 0.f, kSample, 5, false},
};
} // namespace menu

struct Menu {
	static constexpr int kDepth = 4;
	bool open = false, confirming = false;
	int depth = 0;
	const MenuItem* items[kDepth] = {menu::kRoot};
	int counts[kDepth] = {5};
	int sel[kDepth] = {};
	const char* names[kDepth] = {};
	float lastXfade = 20.f;
	float fadeNow = 0.f;  // the selected slot's FADEOUT (set by the module), where FADEOUT opens

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
				if ((it.sub[k].action == A_XFADE && it.sub[k].value == lastXfade) ||
				    (it.sub[k].action == A_FADE && it.sub[k].value == fadeNow))
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
