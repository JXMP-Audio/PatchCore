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

/** Blends the last n frames before LOOP END into the n frames that lead into LOOP ST (equal power), so going round the
 * loop sounds like playing into LOOP ST: no click at the seam. n = `ms` long at most, and no more than the room before
 * LOOP ST (from START) and the loop itself. False (nothing changed) when that leaves fewer than 8 frames. */
inline bool crossfadeLoop(SampleData& d, const SlotSettings& s, float ms) {
	const size_t frames = d.frames();
	const size_t ls = std::min(s.loopStart, frames), le = std::min(std::max(s.loopEnd, ls), frames);
	const size_t start = std::min(s.start, ls);
	size_t n = (size_t) std::lround(ms / 1000.f * d.rate);
	n = std::min(n, std::min(ls - start, le - ls));
	if (n < 8)
		return false;
	auto blend = [&](std::vector<float>& x) {
		for (size_t k = 0; k < n; ++k) {
			const float t = (float) (k + 1) / (float) n * 1.5707963f;
			x[le - n + k] = x[le - n + k] * std::cos(t) + x[ls - n + k] * std::sin(t);
		}
	};
	blend(d.l);
	if (d.r.size() == frames)
		blend(d.r);
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

/** Whether going from LOOP END back to LOOP ST jumps or kinks the wave (the SHOW SEAM verdict): the frame at LOOP ST
 * is compared with where the end was heading, and the slopes on both sides with how much the slope changes anyway */
inline bool seamClicks(const SampleData& d, size_t ls, size_t le) {
	const size_t n = d.frames();
	if (le > n || ls + 2 > n || le < ls + 3 || le < 3)
		return false;
	const float e1 = monoAt(d, le - 1), e2 = monoAt(d, le - 2), e3 = monoAt(d, le - 3);
	const float s0 = monoAt(d, ls), s1 = monoAt(d, ls + 1);
	const float slopeEnd = e1 - e2, natural = std::fabs(slopeEnd - (e2 - e3));
	const float jump = std::fabs(s0 - (e1 + slopeEnd));
	const float kink = std::fabs((s1 - s0) - slopeEnd);
	return jump > 0.02f + 2.f * natural || kink > 0.01f + 3.f * natural;
}

} // namespace sampler
} // namespace jxmp
