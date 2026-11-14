/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST menus, no Rack dependency. The main menu: PLUGIN (LOAD, RESCAN, ADD FOLDER, REMOVE PLUGIN) · I/O (TABLE) ·
 * MIDI (ALL, 1-16) · CV SLOTS (CLEAR ALL SLOTS); the slot menu (OK on a slot): LEARN, CHOOSE PARAMETER, RANGE MIN,
 * RANGE MAX, REMOVE. Buttons as JX-SMPL: MENU opens / goes back / closes, ▲ ▼ move (wrapping), OK opens a folder or
 * runs an item (which closes the menu); REMOVE PLUGIN and CLEAR ALL SLOTS want a second OK, MENU says no.
 */

#pragma once

#include <string>

namespace jxmp {
namespace nest {

enum MenuAction {
	A_NONE, A_LOAD, A_RESCAN, A_ADD_FOLDER, A_REMOVE, A_IO_TABLE, A_MIDI_CHANNEL, A_CLEAR_SLOTS,
	A_LEARN, A_CHOOSE, A_RANGE_MIN, A_RANGE_MAX, A_SLOT_REMOVE
};

struct MenuItem {
	const char* name;
	MenuAction action;
	float value;  // MIDI: the channel, 0 = ALL
	const MenuItem* sub;
	int subCount;
	bool ask;
};

struct MenuResult {
	MenuAction action = A_NONE;
	float value = 0.f;
};

namespace menu {
static const MenuItem kPlugin[] = {
    {"LOAD", A_LOAD, 0.f, nullptr, 0, false},
    {"RESCAN", A_RESCAN, 0.f, nullptr, 0, false},
    {"ADD FOLDER", A_ADD_FOLDER, 0.f, nullptr, 0, false},
    {"REMOVE PLUGIN", A_REMOVE, 0.f, nullptr, 0, true},
};
static const MenuItem kIO[] = {
    {"TABLE", A_IO_TABLE, 0.f, nullptr, 0, false},
};
static const MenuItem kMidi[] = {
    {"ALL", A_MIDI_CHANNEL, 0.f, nullptr, 0, false},   {"1", A_MIDI_CHANNEL, 1.f, nullptr, 0, false},
    {"2", A_MIDI_CHANNEL, 2.f, nullptr, 0, false},     {"3", A_MIDI_CHANNEL, 3.f, nullptr, 0, false},
    {"4", A_MIDI_CHANNEL, 4.f, nullptr, 0, false},     {"5", A_MIDI_CHANNEL, 5.f, nullptr, 0, false},
    {"6", A_MIDI_CHANNEL, 6.f, nullptr, 0, false},     {"7", A_MIDI_CHANNEL, 7.f, nullptr, 0, false},
    {"8", A_MIDI_CHANNEL, 8.f, nullptr, 0, false},     {"9", A_MIDI_CHANNEL, 9.f, nullptr, 0, false},
    {"10", A_MIDI_CHANNEL, 10.f, nullptr, 0, false},   {"11", A_MIDI_CHANNEL, 11.f, nullptr, 0, false},
    {"12", A_MIDI_CHANNEL, 12.f, nullptr, 0, false},   {"13", A_MIDI_CHANNEL, 13.f, nullptr, 0, false},
    {"14", A_MIDI_CHANNEL, 14.f, nullptr, 0, false},   {"15", A_MIDI_CHANNEL, 15.f, nullptr, 0, false},
    {"16", A_MIDI_CHANNEL, 16.f, nullptr, 0, false},
};
static const MenuItem kSlots[] = {
    {"CLEAR ALL SLOTS", A_CLEAR_SLOTS, 0.f, nullptr, 0, true},
};
static const MenuItem kRoot[] = {
    {"PLUGIN", A_NONE, 0.f, kPlugin, 4, false},
    {"I/O", A_NONE, 0.f, kIO, 1, false},
    {"MIDI", A_NONE, 0.f, kMidi, 17, false},
    {"CV SLOTS", A_NONE, 0.f, kSlots, 1, false},
};
static const MenuItem kSlot[] = {
    {"LEARN", A_LEARN, 0.f, nullptr, 0, false},
    {"CHOOSE PARAMETER", A_CHOOSE, 0.f, nullptr, 0, false},
    {"RANGE MIN", A_RANGE_MIN, 0.f, nullptr, 0, false},
    {"RANGE MAX", A_RANGE_MAX, 0.f, nullptr, 0, false},
    {"REMOVE", A_SLOT_REMOVE, 0.f, nullptr, 0, false},
};
} // namespace menu

struct Menu {
	enum Kind { MAIN, SLOT };
	static constexpr int kDepth = 3;
	bool open = false, confirming = false;
	int depth = 0;
	const MenuItem* items[kDepth] = {menu::kRoot};
	int counts[kDepth] = {4};
	int sel[kDepth] = {};
	const char* names[kDepth] = {};
	int midiNow = 0;  // the module's channel, where MIDI opens

	explicit Menu(Kind k = MAIN) {
		if (k == SLOT) {
			items[0] = menu::kSlot;
			counts[0] = 5;
		}
	}

	const MenuItem* current() const { return items[depth]; }
	int count() const { return counts[depth]; }
	int selected() const { return sel[depth]; }
	const MenuItem& item() const { return items[depth][sel[depth]]; }

	/** "PLUGIN" inside a folder, empty at the top */
	std::string path() const {
		std::string p;
		for (int d = 1; d <= depth; ++d)
			p += (d > 1 ? " > " : "") + std::string(names[d]);
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
	void close() {
		open = confirming = false;
		depth = 0;
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
		const MenuItem& it = item();
		if (it.sub && depth + 1 < kDepth) {
			++depth;
			items[depth] = it.sub;
			counts[depth] = it.subCount;
			names[depth] = it.name;
			sel[depth] = 0;
			for (int k = 0; k < it.subCount; ++k)
				if (it.sub[k].action == A_MIDI_CHANNEL && (int) it.sub[k].value == midiNow)
					sel[depth] = k;
			return r;
		}
		if (it.ask && !confirming) {
			confirming = true;
			return r;
		}
		r.action = it.action;
		r.value = it.value;
		close();
		return r;
	}
};

} // namespace nest
} // namespace jxmp
