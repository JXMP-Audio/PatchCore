/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL core, no Rack dependency: sample data, per-slot settings and the voice of one slot (FastTracker 2 style:
 * one voice per slot, root note, loop off / forward / ping-pong, one-shot or gated).
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace jxmp {
namespace sampler {

/** One loaded sample, float at the file's own rate. `r` is empty for a mono sample. */
struct SampleData {
	std::vector<float> l, r;
	bool stereo = false;
	float rate = 48000.f;
	size_t frames() const { return l.size(); }
};

enum Mode { ONE_SHOT = 0, GATE = 1 };
enum Loop { LOOP_OFF = 0, LOOP_FWD = 1, LOOP_PING = 2 };

/** Settings of one slot. Points are positions in the sample (frames); `end` and `loopEnd` are exclusive. */
struct SlotSettings {
	size_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
	int root = 60;      // MIDI note the sample sounds at (60 = C-4)
	float fine = 0.f;   // cents, ±100
	float level = 1.f;  // 0..1
	float pan = 0.f;    // -1..1
	Mode mode = ONE_SHOT;
	Loop loop = LOOP_OFF;
};

/** MIDI note as FastTracker 2 writes it: "C-4" (60), "C#4"; root notes go from C-0 (12) to B-8 (119) */
inline std::string noteName(int midi) {
	static const char* const names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
	midi = std::min(std::max(midi, 12), 119);
	return names[midi % 12] + std::to_string(midi / 12 - 1);
}

/** A slot right after loading a sample: the whole sample, no loop, root C-4, full level, centre */
inline SlotSettings defaults(size_t frames) {
	SlotSettings s;
	s.end = s.loopEnd = frames;
	return s;
}

enum Point { P_START = 0, P_END = 1, P_LOOP_START = 2, P_LOOP_END = 3 };
constexpr size_t kMinLoop = 16;

/** Keeps START < END and START <= LOOP ST < LOOP END <= END inside the sample, the loop at least 16 frames (when the
 * played part is that long). `moved` is the point the user just moved: it stays, the others make room. */
inline void constrain(SlotSettings& s, size_t frames, int moved) {
	if (frames == 0) {
		s.start = s.end = s.loopStart = s.loopEnd = 0;
		return;
	}
	s.start = std::min(s.start, frames - 1);
	s.end = std::min(std::max(s.end, (size_t) 1), frames);
	if (s.start >= s.end) {
		if (moved == P_END)
			s.start = s.end - 1;
		else
			s.end = s.start + 1;
	}
	auto inside = [&](size_t v) { return std::min(std::max(v, s.start), s.end); };
	s.loopStart = inside(s.loopStart);
	s.loopEnd = inside(s.loopEnd);
	const size_t minLoop = std::min(kMinLoop, s.end - s.start);
	if (moved == P_LOOP_START) {
		if (s.loopEnd < s.loopStart + minLoop)
			s.loopEnd = std::min(s.end, s.loopStart + minLoop);
		if (s.loopEnd < s.loopStart + minLoop)
			s.loopStart = s.loopEnd - minLoop;
	}
	else {
		if (s.loopStart + minLoop > s.loopEnd)
			s.loopStart = s.loopEnd >= s.start + minLoop ? s.loopEnd - minLoop : s.start;
		if (s.loopEnd < s.loopStart + minLoop)
			s.loopEnd = s.loopStart + minLoop;
	}
}

/** Equal-power pan, centre = 1 / 1 (a centred slot keeps its level on both sides) */
inline void panGains(float pan, float& gl, float& gr) {
	const float a = (std::min(std::max(pan, -1.f), 1.f) + 1.f) * 0.78539816f;
	gl = std::cos(a) * 1.41421356f;
	gr = std::sin(a) * 1.41421356f;
}

/** Identity to 8 V, then a soft knee that never reaches 10 V */
inline float softLimit10(float v) {
	const float a = std::fabs(v);
	if (a <= 8.f)
		return v;
	return std::copysign(8.f + 2.f * std::tanh((a - 8.f) / 2.f), v);
}

/** The stereo MIX: only slots whose own OUT is not patched. Mono slots are panned; for a stereo slot PAN is a
 * balance (it only turns the far side down). */
struct MixBus {
	float L = 0.f, R = 0.f;
	void clear() { L = R = 0.f; }
	void add(float l, float r, bool stereo, float level, float pan, bool ownOutPatched) {
		if (ownOutPatched)
			return;
		if (stereo) {
			L += l * level * (pan > 0.f ? 1.f - pan : 1.f);
			R += r * level * (pan < 0.f ? 1.f + pan : 1.f);
		}
		else {
			float gl, gr;
			panGains(pan, gl, gr);
			L += l * level * gl;
			R += l * level * gr;
		}
	}
};

/** Playback speed in file frames per engine frame. Without a V/OCT cable the sample plays at its original pitch
 * (the root note only matters when a note comes in); 0 V = C-4 as everywhere in JXMP. */
inline float pitchRatio(float fileRate, float engineRate, bool voctPatched, float voct, int root, float fineCents) {
	// A broken or wild cable (NaN, ±inf, far beyond ±10 V) must not stop the audio: ±10 octaves at most
	voct = std::isfinite(voct) ? std::min(std::max(voct, -10.f), 10.f) : 0.f;
	const float octaves = (voctPatched ? voct - (root - 60) / 12.f : 0.f) + fineCents / 1200.f;
	return fileRate / engineRate * std::pow(2.f, octaves);
}

/** 4-point Hermite read of `x` at `pos` (indices clamped to the data) */
inline float hermite(const std::vector<float>& x, double pos) {
	const long n = (long) x.size();
	const long i = (long) std::floor(pos);
	const float t = (float) (pos - i);
	auto at = [&](long k) { return x[(size_t) std::min(std::max(k, 0L), n - 1)]; };
	const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
	const float c1 = 0.5f * (x1 - xm1);
	const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
	const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
	return ((c3 * t + c2) * t + c1) * t + x0;
}

/** The voice of one slot. A new trigger fades the running sound out over 2 ms while the new one starts at START; in
 * GATE mode the gate end fades out over 10 ms (a loop holds the tone until then). Output in sample units (±1). */
struct Voice {
	static constexpr float kRetriggerFade = 0.002f;
	static constexpr float kRelease = 0.010f;

	struct Head {
		double pos = 0.0;
		int dir = 1;
		float gain = 0.f, fadeStep = 0.f;
		bool on = false, fading = false;
	};
	Head cur, old;
	bool pendingTrigger = false, pendingRelease = false, pendingFade = false;

	void trigger() {
		pendingTrigger = true;
		pendingRelease = false;
	}
	void release() { pendingRelease = true; }
	/** Fades everything out over 2 ms in any mode (the slot gets a new sample) */
	void fadeOut() {
		pendingFade = true;
		pendingTrigger = pendingRelease = false;
	}
	bool active() const { return cur.on || old.on; }
	/** Where the main head plays (frames), for the display; negative when silent */
	double position() const { return cur.on ? cur.pos : -1.0; }

	void process(const SampleData& d, const SlotSettings& set, float ratio, float sampleTime, float& outL, float& outR) {
		outL = outR = 0.f;
		const size_t frames = d.frames();
		if (frames == 0) {
			cur.on = old.on = false;
			pendingTrigger = pendingRelease = pendingFade = false;
			return;
		}
		// The settings may belong to another (longer) sample for a moment: keep every point inside this one
		const double end = (double) std::min(std::max(set.end, (size_t) 1), frames);
		const double start = std::min((double) set.start, end - 1.0);
		const double ls = std::min(std::max((double) set.loopStart, start), end);
		const double le = std::min(std::max((double) set.loopEnd, ls), end);
		const bool looping = set.mode == GATE && set.loop != LOOP_OFF && le - ls >= 2.0;
		const float sr = 1.f / sampleTime;

		if (pendingTrigger) {
			if (cur.on) {
				old = cur;
				old.fading = true;
				old.fadeStep = 1.f / (kRetriggerFade * sr);
			}
			cur = Head();
			cur.pos = start;
			cur.gain = 1.f;
			cur.on = true;
			pendingTrigger = false;
		}
		if (pendingRelease) {
			if (set.mode == GATE && cur.on && !cur.fading) {
				cur.fading = true;
				cur.fadeStep = 1.f / (kRelease * sr);
			}
			pendingRelease = false;
		}
		if (pendingFade) {
			for (Head* h : {&cur, &old}) {
				const float step = 1.f / (kRetriggerFade * sr);
				if (h->on && (!h->fading || h->fadeStep < step)) {
					h->fading = true;
					h->fadeStep = step;
				}
			}
			pendingFade = false;
		}
		for (Head* h : {&cur, &old}) {
			if (!h->on)
				continue;
			h->pos = std::min(std::max(h->pos, 0.0), (double) (frames - 1));
			const float l = hermite(d.l, h->pos) * h->gain;
			const float r = d.stereo && d.r.size() == frames ? hermite(d.r, h->pos) * h->gain : l;
			outL += l;
			outR += r;
			h->pos += h->dir * ratio;
			// In the loop once past LOOP ST; going backwards only happens inside a ping-pong loop
			if (looping && (h->pos >= ls || h->dir < 0)) {
				if (set.loop == LOOP_FWD) {
					// Also after a switch from PING while going backwards: forward again, inside the loop
					h->dir = 1;
					if (h->pos >= le || h->pos < ls)
						h->pos = ls + std::fmod(std::fmod(h->pos - ls, le - ls) + (le - ls), le - ls);
				}
				else {
					if (h->dir > 0 && h->pos >= le) {
						h->pos = 2.0 * le - h->pos;
						h->dir = -1;
					}
					if (h->dir < 0 && h->pos < ls) {
						h->pos = 2.0 * ls - h->pos;
						h->dir = 1;
					}
					h->pos = std::min(std::max(h->pos, ls), le);
				}
			}
			else if ((h->dir > 0 && h->pos >= end) || (h->dir < 0 && h->pos < start)) {
				h->on = false;
			}
			if (h->fading) {
				h->gain -= h->fadeStep;
				if (h->gain <= 0.f)
					h->on = false;
			}
		}
	}
};

} // namespace sampler
} // namespace jxmp
