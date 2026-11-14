/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL editor, no Rack dependency: the edits of the MENU (crop, normalize, reverse, loop crossfade, snap to zero
 * crossings) and the seam check of the SHOW SEAM view. Edits work on a copy: the module swaps the sample afterwards.
 */

#pragma once

#include "Sampler.hpp"

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace sampler {

/** START..END of `d` as a new sample; the points in `s` move with it (the loop stays where it was in the sound) */
inline SampleData cropped(const SampleData& d, SlotSettings& s) {
	const size_t n = d.frames();
	const size_t a = std::min(s.start, n), b = std::min(std::max(s.end, a), n);
	SampleData c;
	c.rate = d.rate;
	c.stereo = d.stereo && d.r.size() == n;
	c.l.assign(d.l.begin() + a, d.l.begin() + b);
	if (c.stereo)
		c.r.assign(d.r.begin() + a, d.r.begin() + b);
	auto in = [&](size_t v) { return std::min(std::max(v, a), b) - a; };
	s.loopStart = in(s.loopStart);
	s.loopEnd = in(s.loopEnd);
	s.start = 0;
	s.end = b - a;
	constrain(s, c.frames(), P_START);
	return c;
}

/** Scales the sample so its peak (either side) is at 0.999; returns the gain, 0 for silence (left as it is) */
inline float normalize(SampleData& d) {
	float peak = 0.f;
	for (float v : d.l)
		peak = std::max(peak, std::fabs(v));
	for (float v : d.r)
		peak = std::max(peak, std::fabs(v));
	if (!(peak > 0.f))
		return 0.f;
	const float g = 0.999f / peak;
	for (float& v : d.l)
		v *= g;
	for (float& v : d.r)
		v *= g;
	return g;
}

/** Plays frames start..end-1 backwards */
inline void reverse(SampleData& d, size_t start, size_t end) {
	end = std::min(end, d.frames());
	if (start >= end)
		return;
	std::reverse(d.l.begin() + start, d.l.begin() + end);
	if (d.r.size() == d.l.size())
		std::reverse(d.r.begin() + start, d.r.begin() + end);
}

/** Loop crossfade: the last n frames before LOOP END blend into the loop's own first n frames, and LOOP ST moves
 * past them (by n), so going round the loop continues exactly where the blend ended: no click at the seam. Only sound
 * inside the loop is used (taking what plays before LOOP ST brought an attack into every round). The gains follow how
 * alike the two parts are (alike: a straight blend, no swell; unlike: constant power). n = `ms` long at most and half
 * the loop; false (nothing changed) when that leaves fewer than 8 frames. */
inline bool crossfadeLoop(SampleData& d, SlotSettings& s, float ms) {
	const size_t frames = d.frames();
	const size_t ls = std::min(s.loopStart, frames), le = std::min(std::max(s.loopEnd, ls), frames);
	size_t n = (size_t) std::lround(ms / 1000.f * d.rate);
	n = std::min(n, (le - ls) / 2);
	if (n < 8)
		return false;
	const bool stereo = d.r.size() == frames;
	// How alike the end of the loop and its start are (0..1)
	double ab = 0.0, aa = 0.0, bb = 0.0;
	for (size_t k = 0; k < n; ++k) {
		ab += (double) d.l[le - n + k] * d.l[ls + k];
		aa += (double) d.l[le - n + k] * d.l[le - n + k];
		bb += (double) d.l[ls + k] * d.l[ls + k];
		if (stereo) {
			ab += (double) d.r[le - n + k] * d.r[ls + k];
			aa += (double) d.r[le - n + k] * d.r[le - n + k];
			bb += (double) d.r[ls + k] * d.r[ls + k];
		}
	}
	const float r = aa > 0.0 && bb > 0.0 ? (float) std::min(std::max(ab / std::sqrt(aa * bb), 0.0), 1.0) : 0.f;
	auto blend = [&](std::vector<float>& x) {
		for (size_t k = 0; k < n; ++k) {
			const float t = (float) (k + 1) / (float) n, a = 1.f - t, b = t;
			const float g = 1.f / std::sqrt(a * a + b * b + 2.f * r * a * b);
			x[le - n + k] = g * (a * x[le - n + k] + b * x[ls + k]);
		}
	};
	blend(d.l);
	if (stereo)
		blend(d.r);
	s.loopStart = ls + n;
	return true;
}

/** The sound at frame i (both sides averaged) */
inline float monoAt(const SampleData& d, size_t i) {
	return d.r.size() == d.l.size() ? 0.5f * (d.l[i] + d.r[i]) : d.l[i];
}

/** The nearest rising zero crossing to `f` within `window` frames (the frame where the sound goes from below zero to
 * zero or above); `f` itself when there is none */
inline size_t nearestZero(const SampleData& d, size_t f, size_t window) {
	const size_t n = d.frames();
	size_t best = f, bestD = window + 1;
	const size_t a = f > window ? f - window : 1, b = std::min(n, f + window + 1);
	for (size_t k = std::max<size_t>(a, 1); k < b; ++k)
		if (monoAt(d, k - 1) < 0.f && monoAt(d, k) >= 0.f) {
			const size_t dist = k > f ? k - f : f - k;
			if (dist < bestD) {
				bestD = dist;
				best = k;
			}
		}
	return best;
}

/** START, END and both loop points onto the nearest rising zero crossing within 20 ms, then the usual order */
inline void snapToZero(const SampleData& d, SlotSettings& s) {
	const size_t w = (size_t) (0.02f * d.rate);
	s.start = nearestZero(d, s.start, w);
	s.end = nearestZero(d, s.end, w);
	s.loopStart = nearestZero(d, s.loopStart, w);
	s.loopEnd = nearestZero(d, s.loopEnd, w);
	constrain(s, d.frames(), P_START);
}

/** Whether going from LOOP END back to LOOP ST jumps or kinks the wave (the SHOW SEAM verdict), on either side of a
 * stereo sample: the frame at LOOP ST is compared with where the end was heading, and the slopes on both sides with
 * how much the slope changes anyway */
inline bool seamClicks(const SampleData& d, size_t ls, size_t le) {
	const size_t n = d.frames();
	if (le > n || ls + 2 > n || le < ls + 3 || le < 3)
		return false;
	auto side = [&](const std::vector<float>& x) {
		const float e1 = x[le - 1], e2 = x[le - 2], e3 = x[le - 3];
		const float s0 = x[ls], s1 = x[ls + 1];
		const float slopeEnd = e1 - e2, natural = std::fabs(slopeEnd - (e2 - e3));
		const float jump = std::fabs(s0 - (e1 + slopeEnd));
		const float kink = std::fabs((s1 - s0) - slopeEnd);
		return jump > 0.02f + 2.f * natural || kink > 0.01f + 3.f * natural;
	};
	return side(d.l) || (d.r.size() == n && side(d.r));
}

} // namespace sampler
} // namespace jxmp
