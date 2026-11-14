/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST jack roles, no Rack or Carla dependency: what each of the 12 inputs and 12 outputs on the back does, the
 * choices the I/O table offers (built from the ports the loaded plugin reports), the short labels under the jacks.
 * An audio role keeps the port's index and name, so it survives a plugin change; a port the plugin lacks is invalid
 * (the display shows "×" and the jack is silent).
 */

#pragma once

#include "Sampler.hpp"  // noteName

#include <cctype>
#include <string>
#include <vector>

namespace jxmp {
namespace nest {

constexpr int kJacks = 12;
constexpr int kSlots = 8;
constexpr int kMaxPorts = 32;
constexpr int kDefaultDrumNote = 36;

enum RoleKind { R_NONE, R_AUDIO, R_CV, R_GATE, R_VOCT, R_VEL, R_GATE_NOTE };

struct Role {
	RoleKind kind = R_NONE;
	int index = 0;     // R_AUDIO: plugin port; R_CV: slot 0..7
	std::string name;  // R_AUDIO: the port's name as the plugin reported it
	int note = kDefaultDrumNote;
	bool operator==(const Role& o) const {
		return kind == o.kind && index == o.index && name == o.name && note == o.note;
	}
};

struct PortInfo {
	std::vector<std::string> ins, outs;
	bool midiIn = false;
};

struct IOTable {
	Role in[kJacks], out[kJacks];
	/** A new module: IN 1-2 -> the plugin's inputs 1-2, OUT 1-2 <- its outputs 1-2, the rest empty */
	void reset() {
		for (int k = 0; k < kJacks; ++k)
			in[k] = out[k] = Role();
		for (int k = 0; k < 2; ++k) {
			in[k].kind = out[k].kind = R_AUDIO;
			in[k].index = out[k].index = k;
			in[k].name = out[k].name = k ? "R" : "L";
		}
	}
	IOTable() { reset(); }
};

inline bool takesNotes(RoleKind k) { return k == R_GATE || k == R_VOCT || k == R_VEL || k == R_GATE_NOTE; }

inline bool roleValid(const Role& r, bool isInput, const PortInfo& p) {
	if (r.kind == R_AUDIO)
		return r.index >= 0 && r.index < (int) (isInput ? p.ins : p.outs).size() && r.index < kMaxPorts;
	if (r.kind == R_CV)
		return isInput && r.index >= 0 && r.index < kSlots;
	if (takesNotes(r.kind))
		return isInput && p.midiIn;
	return true;
}

inline std::string upper(std::string s) {
	for (char& c : s)
		c = (char) std::toupper((unsigned char) c);
	return s;
}

inline std::string roleText(const Role& r, bool /*isInput*/) {
	switch (r.kind) {
		case R_AUDIO: return r.name;
		case R_CV: return "CV SLOT " + std::to_string(r.index + 1);
		case R_GATE: return "GATE";
		case R_VOCT: return "V/OCT";
		case R_VEL: return "VELOCITY";
		case R_GATE_NOTE: return "GATE \xC2\xBB NOTE " + sampler::noteName(r.note);
		default: return "—";
	}
}

/** The label under a jack on the back, at most 6 characters */
inline std::string jackLabel(const Role& r) {
	if (r.kind == R_NONE)
		return "";
	if (r.kind == R_CV)
		return "CV " + std::to_string(r.index + 1);
	if (r.kind == R_VEL)
		return "VEL";
	if (r.kind == R_GATE_NOTE)
		return "G " + sampler::noteName(r.note);
	if (r.kind != R_AUDIO)
		return roleText(r, true);
	std::string s = upper(r.name);
	if (s.compare(0, 5, "MAIN ") == 0 && s.size() > 5)
		s = s.substr(5);
	const size_t sc = s.find("SIDECHAIN");
	if (sc != std::string::npos)
		s.replace(sc, 9, "SC");
	return s.substr(0, 6);
}

struct Choice {
	bool head = false;
	std::string text;
	Role role;
};

inline Choice heading(const char* t) {
	Choice c;
	c.head = true;
	c.text = t;
	return c;
}

/** What the I/O table offers for one jack, in groups (a heading row, then its items); the first item is "—" */
inline std::vector<Choice> choices(bool isInput, const PortInfo& p) {
	std::vector<Choice> c;
	Choice none;
	none.text = "—";
	c.push_back(none);
	const std::vector<std::string>& ports = isInput ? p.ins : p.outs;
	if (!ports.empty()) {
		c.push_back(heading(isInput ? "PLUGIN AUDIO IN" : "PLUGIN AUDIO OUT"));
		for (int k = 0; k < (int) ports.size() && k < kMaxPorts; ++k) {
			Choice e;
			e.text = upper(ports[k]);
			e.role.kind = R_AUDIO;
			e.role.index = k;
			e.role.name = ports[k];
			c.push_back(e);
		}
	}
	if (!isInput)
		return c;
	c.push_back(heading("CV SLOT"));
	for (int k = 0; k < kSlots; ++k) {
		Choice e;
		e.role.kind = R_CV;
		e.role.index = k;
		e.text = roleText(e.role, true);
		c.push_back(e);
	}
	if (p.midiIn) {
		c.push_back(heading("NOTES"));
		for (RoleKind k : {R_GATE, R_VOCT, R_VEL, R_GATE_NOTE}) {
			Choice e;
			e.role.kind = k;
			e.text = roleText(e.role, true);
			c.push_back(e);
		}
	}
	return c;
}

/** After a plugin load: audio roles take the name of the new plugin's port at their index (missing ones keep theirs) */
inline void renamePorts(IOTable& t, const PortInfo& p) {
	for (int k = 0; k < kJacks; ++k) {
		if (t.in[k].kind == R_AUDIO && t.in[k].index < (int) p.ins.size())
			t.in[k].name = p.ins[t.in[k].index];
		if (t.out[k].kind == R_AUDIO && t.out[k].index < (int) p.outs.size())
			t.out[k].name = p.outs[t.out[k].index];
	}
}

} // namespace nest
} // namespace jxmp
