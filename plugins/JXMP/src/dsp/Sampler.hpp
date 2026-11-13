/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL core, no Rack dependency: sample data, per-slot settings and the voice of one slot (FastTracker 2 style:
 * one voice per slot, root note, loop off / forward / ping-pong, one-shot or gated).
 */

#pragma once

#include "SampleEnvelope.hpp"
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
	Envelope volEnv = defaultVolEnv(), panEnv = defaultPanEnv();  // off until switched on
	float fadeout = 0.f;  // seconds from the gate end to silence; 0 = off (GATE mode: the 10 ms release)
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

/** Where the interpolation reads around a loop: inside a playing loop the frames past LOOP END are the loop's own
 * (forward: from LOOP ST again; ping-pong: mirrored back), so the seam never hears what lies outside the loop */
struct LoopFold {
	Loop loop = LOOP_OFF;
	long ls = 0, le = 0;
	long fold(long k) const {
		const long len = le - ls;
		if (loop == LOOP_OFF || len < 2)
			return k;
		if (loop == LOOP_FWD) {
			if (k >= le)
				return ls + (k - le) % len;
			if (k < ls)
				return le - 1 - (ls - 1 - k) % len;
			return k;
		}
		// Ping-pong: mirror about the ends (the head turns there)
		for (int guard = 0; guard < 4 && (k >= le || k < ls); ++guard)
			k = k >= le ? 2 * le - 1 - k : 2 * ls - 1 - k;
		return std::min(std::max(k, ls), le - 1);
	}
};

/** 4-point Hermite read of `x` at `pos` (indices folded into a playing loop, then clamped to the data) */
inline float hermite(const std::vector<float>& x, double pos, const LoopFold& lf = LoopFold()) {
	const long n = (long) x.size();
	const long i = (long) std::floor(pos);
	const float t = (float) (pos - i);
	auto at = [&](long k) { return x[(size_t) std::min(std::max(lf.fold(k), 0L), n - 1)]; };
	const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
	const float c1 = 0.5f * (x1 - xm1);
	const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
	const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
	return ((c3 * t + c2) * t + c1) * t + x0;
}

/** The voice of one slot. A new trigger fades the running sound out over 2 ms while the new one starts at START; in
 * GATE mode the gate end fades out over 10 ms (a loop holds the tone until then). Output in sample units (±1).
 * Up to kTails older sounds fade out at once (fast retriggers); when all are busy the quietest one makes room. */
struct Voice {
	static constexpr float kRetriggerFade = 0.002f;
	static constexpr float kRelease = 0.010f;
	static constexpr int kTails = 4;

	struct Head {
		double pos = 0.0;
		int dir = 1;
		float gain = 0.f, fadeStep = 0.f;
		bool on = false, fading = false;
		bool released = false;  // the gate ended (GATE mode)
		float fade = 1.f;       // FADEOUT after the gate end
		EnvRun vol, pan;
	};
	Head cur, tails[kTails];
	float panValue = 0.5f;  // the main head's pan envelope (0.5 = the PAN knob), for the mix
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
	bool active() const {
		if (cur.on)
			return true;
		for (const Head& t : tails)
			if (t.on)
				return true;
		return false;
	}
	/** Where the main head plays (frames), for the display; negative when silent */
	double position() const { return cur.on ? cur.pos : -1.0; }
	/** Where the main head's volume / pan envelope is (seconds); negative when silent */
	float volTime() const { return cur.on ? cur.vol.t : -1.f; }
	float panTime() const { return cur.on ? cur.pan.t : -1.f; }

	void process(const SampleData& d, const SlotSettings& set, float ratio, float sampleTime, float& outL, float& outR) {
		outL = outR = 0.f;
		const size_t frames = d.frames();
		if (frames == 0) {
			cur.on = false;
			for (Head& t : tails)
				t.on = false;
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
				// A free tail, else the quietest (its cut is the smallest click)
				Head* slot = &tails[0];
				for (Head& t : tails) {
					if (!t.on) {
						slot = &t;
						break;
					}
					if (t.gain < slot->gain)
						slot = &t;
				}
				const float step = 1.f / (kRetriggerFade * sr);
				*slot = cur;
				if (!slot->fading || slot->fadeStep < step) {
					slot->fading = true;
					// Fades out over 2 ms from where it is
					slot->fadeStep = std::max(slot->fadeStep, slot->gain * step);
				}
			}
			cur = Head();
			cur.pos = start;
			cur.gain = 1.f;
			cur.on = true;
			pendingTrigger = false;
		}
		if (pendingRelease) {
			if (set.mode == GATE && cur.on && !cur.released) {
				cur.released = true;
				// FADEOUT takes over the release; with a volume envelope the envelope runs to its end first
				if (set.fadeout <= 0.f && !set.volEnv.on && !cur.fading) {
					cur.fading = true;
					cur.fadeStep = 1.f / (kRelease * sr);
				}
			}
			pendingRelease = false;
		}
		if (pendingFade) {
			for (Head* h = &cur; h != tails + kTails; h = h == &cur ? tails : h + 1) {
				const float step = 1.f / (kRetriggerFade * sr);
				if (h->on && (!h->fading || h->fadeStep < step)) {
					h->fading = true;
					h->fadeStep = step;
				}
			}
			pendingFade = false;
		}
		for (Head* h = &cur; h != tails + kTails; h = h == &cur ? tails : h + 1) {
			if (!h->on)
				continue;
			// Inside this sample (the settings may be a longer one's for a moment). Up to `frames`, not the last frame:
			// pulled back there, a voice slower than the file (ratio < 1) would never reach END and would hold forever.
			// hermite() keeps its reads inside on its own.
			h->pos = std::min(std::max(h->pos, 0.0), (double) frames);
			// Inside a playing loop the read folds around its ends
			LoopFold lf;
			if (looping && (h->pos >= ls || h->dir < 0)) {
				lf.loop = set.loop;
				lf.ls = (long) ls;
				lf.le = (long) le;
			}
			// Envelopes: sustain only while the gate is high in GATE mode; after the gate the loop only runs on with a
			// FADEOUT (otherwise a looping envelope would never end)
			const bool held = set.mode == GATE && !h->released;
			const bool loopOn = !h->released || set.fadeout > 0.f;
			float g = h->gain * h->fade;
			if (set.volEnv.on) {
				g *= envValue(set.volEnv, h->vol.t);
				h->vol.step(set.volEnv, sampleTime, held, loopOn);
			}
			if (h == &cur)
				panValue = set.panEnv.on ? envValue(set.panEnv, h->pan.t) : 0.5f;
			if (set.panEnv.on)
				h->pan.step(set.panEnv, sampleTime, held, loopOn);
			const float l = hermite(d.l, h->pos, lf) * g;
			const float r = d.stereo && d.r.size() == frames ? hermite(d.r, h->pos, lf) * g : l;
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
			if (h->released && set.fadeout > 0.f) {
				h->fade -= sampleTime / set.fadeout;
				if (h->fade <= 0.f)
					h->on = false;
			}
			// A volume envelope that ended: silent → done; still sounding after the gate, no fadeout → the release
			if (set.volEnv.on && h->vol.pastEnd(set.volEnv)) {
				if (set.volEnv.v[set.volEnv.n - 1] <= 1e-4f)
					h->on = false;
				else if (h->released && set.fadeout <= 0.f && !h->fading) {
					h->fading = true;
					h->fadeStep = 1.f / (kRelease * sr);
				}
			}
		}
	}
};

} // namespace sampler
} // namespace jxmp
