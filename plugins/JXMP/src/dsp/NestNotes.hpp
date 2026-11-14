/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NEST notes from the jacks, no Rack dependency. GATE jacks start a note at the rise with the V/OCT note of that
 * moment (0 V = C-4 = 60, unpatched = 60) and the VELOCITY (0..10 V -> 1..127, unpatched 100), and end that same
 * note at the fall; V/OCT moving while the gate is high does not retrigger. Each GATE -> NOTE jack plays its own
 * note (drums). Gates: Schmitt trigger 0.1 V / 1 V. Our notes go out on `channel` (0 = ALL -> channel 1).
 */

#pragma once

#include "NestIO.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace jxmp {
namespace nest {

struct NoteEvent {
	uint8_t status = 0, data1 = 0, data2 = 0;
	int frame = 0;
};

/** Whether a host MIDI message passes the channel choice (0 = ALL, 1..16); system messages always pass */
inline bool channelPasses(uint8_t status, int channel) {
	return channel <= 0 || status >= 0xF0 || (status & 0x0F) == channel - 1;
}
/** The message moved to the chosen channel (ALL keeps it) */
inline uint8_t toChannel(uint8_t status, int channel) {
	if (channel <= 0 || status >= 0xF0)
		return status;
	return (uint8_t) ((status & 0xF0) | (channel - 1));
}

struct NoteMaker {
	int channel = 0;
	bool high[kJacks] = {};
	int playing[kJacks] = {};

	void reset() {
		for (int k = 0; k < kJacks; ++k)
			high[k] = false;
	}

	/** Ends every held note (on the channel it started on), then all notes off (CC 123) on that channel, on all 16
	 * for ALL; the gates start again from low. For role / channel changes and leaving BYPASS: no note hangs. */
	void releaseAll(int frame, std::vector<NoteEvent>& out) {
		const uint8_t ch = (uint8_t) (channel > 0 ? channel - 1 : 0);
		for (int k = 0; k < kJacks; ++k)
			if (high[k]) {
				NoteEvent e;
				e.status = (uint8_t) (0x80 | ch);
				e.data1 = (uint8_t) playing[k];
				e.data2 = 0;
				e.frame = frame;
				out.push_back(e);
			}
		for (int c = channel > 0 ? channel - 1 : 0; c < (channel > 0 ? channel : 16); ++c) {
			NoteEvent e;
			e.status = (uint8_t) (0xB0 | c);
			e.data1 = 123;
			e.data2 = 0;
			e.frame = frame;
			out.push_back(e);
		}
		reset();
	}

	void process(const IOTable& t, const float inV[kJacks], const bool patched[kJacks], int frame,
	             std::vector<NoteEvent>& out) {
		int note = 60, vel = 100;
		for (int k = 0; k < kJacks; ++k) {
			if (!patched[k])
				continue;
			if (t.in[k].kind == R_VOCT)
				note = (int) std::lround(60.f + 12.f * inV[k]);
			else if (t.in[k].kind == R_VEL)
				vel = 1 + (int) std::lround(126.f * std::min(std::max(inV[k], 0.f), 10.f) / 10.f);
		}
		note = std::min(std::max(note, 0), 127);
		const uint8_t ch = (uint8_t) (channel > 0 ? channel - 1 : 0);
		for (int k = 0; k < kJacks; ++k) {
			const RoleKind kind = t.in[k].kind;
			if (kind != R_GATE && kind != R_GATE_NOTE)
				continue;
			const float v = patched[k] ? inV[k] : 0.f;
			const bool was = high[k];
			const bool now = was ? v > 0.1f : v >= 1.f;
			if (now == was)
				continue;
			high[k] = now;
			NoteEvent e;
			e.frame = frame;
			if (now) {
				playing[k] = kind == R_GATE_NOTE ? std::min(std::max(t.in[k].note, 0), 127) : note;
				e.status = (uint8_t) (0x90 | ch);
				e.data2 = (uint8_t) vel;
			} else {
				e.status = (uint8_t) (0x80 | ch);
			}
			e.data1 = (uint8_t) playing[k];
			out.push_back(e);
		}
	}
};

} // namespace nest
} // namespace jxmp
