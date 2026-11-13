/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL DETECT ROOT, no Rack dependency: the pitch of a sample (YIN, de Cheveigné & Kawahara 2002) measured in
 * several windows of START..END after the attack, turned into a root note and a FINE correction. Samples without a
 * clear pitch (drums, noise, chords) are reported as such instead of being guessed.
 */

#pragma once

#include "SampleEdit.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace jxmp {
namespace sampler {

struct PitchResult {
	bool found = false;
	int root = 60;     // the nearest note (MIDI, 60 = C-4)
	float fine = 0.f;  // cents for FINE: the correction that makes the sample play in tune (sharp → negative)
	float hz = 0.f;
};

/** The pitch of one window (YIN): frequency in Hz, or 0 when the window has no clear period */
inline float yinWindow(const std::vector<float>& x, size_t at, size_t w, size_t tauMin, size_t tauMax, float rate) {
	std::vector<double> d(tauMax + 2, 0.0);
	for (size_t tau = 1; tau <= tauMax + 1; ++tau) {
		double s = 0.0;
		for (size_t j = 0; j < w; ++j) {
			const double v = (double) x[at + j] - x[at + j + tau];
			s += v * v;
		}
		d[tau] = s;
	}
	// Cumulative mean normalised difference
	std::vector<double> c(tauMax + 2, 1.0);
	double sum = 0.0;
	for (size_t tau = 1; tau <= tauMax + 1; ++tau) {
		sum += d[tau];
		c[tau] = sum > 0.0 ? d[tau] * tau / sum : 1.0;
	}
	const double threshold = 0.15;
	size_t tau = tauMin;
	while (tau <= tauMax && c[tau] >= threshold)
		++tau;
	if (tau > tauMax)
		return 0.f;
	while (tau + 1 <= tauMax && c[tau + 1] < c[tau])
		++tau;
	// Parabola through the minimum and its neighbours
	double t = (double) tau;
	if (tau > 1 && tau + 1 <= tauMax + 1) {
		const double a = c[tau - 1], b = c[tau], e = c[tau + 1];
		const double den = a - 2.0 * b + e;
		if (std::fabs(den) > 1e-12)
			t += 0.5 * (a - e) / den;
	}
	return t > 0.0 ? (float) (rate / t) : 0.f;
}

/** The root note of START..END of `d`: windows after the attack (from 40 Hz to 2 kHz), the median of the windows that
 * have a clear pitch, and only when most of them agree */
inline PitchResult detectRoot(const SampleData& d, size_t start, size_t end) {
	PitchResult r;
	const size_t n = d.frames();
	end = std::min(end, n);
	if (start >= end || d.rate <= 0.f)
		return r;
	const float rate = d.rate;
	const size_t tauMin = std::max<size_t>(2, (size_t) (rate / 2000.f));
	const size_t tauMax = (size_t) std::ceil(rate / 40.f);
	const size_t w = tauMax, need = w + tauMax + 2;
	// Skip the attack: 30 ms or 5 % of the part, whichever is longer
	const size_t skip = std::max((size_t) (0.03f * rate), (end - start) / 20);
	size_t a = start + skip;
	if (a + need > end)
		a = start;  // a short part: all of it
	if (a + need > end)
		return r;
	std::vector<float> x(end - a);
	for (size_t i = 0; i < x.size(); ++i)
		x[i] = monoAt(d, a + i);
	// Up to 12 windows over the part; quiet ones (the tail, silence) do not count
	const int windows = 12;
	const size_t span = x.size() - need;
	std::vector<size_t> at;
	std::vector<float> rms;
	float loudest = 0.f;
	for (int k = 0; k < windows; ++k) {
		const size_t p = windows > 1 ? span * k / (windows - 1) : 0;
		double s = 0.0;
		for (size_t j = 0; j < need; ++j)
			s += (double) x[p + j] * x[p + j];
		const float v = (float) std::sqrt(s / need);
		at.push_back(p);
		rms.push_back(v);
		loudest = std::max(loudest, v);
	}
	if (loudest < 1e-3f)
		return r;
	std::vector<float> notes;
	int counted = 0;
	for (int k = 0; k < windows; ++k) {
		if (rms[k] < 0.1f * loudest)
			continue;
		++counted;
		const float hz = yinWindow(x, at[k], w, tauMin, tauMax, rate);
		if (hz > 0.f)
			notes.push_back(69.f + 12.f * std::log2(hz / 440.f));
	}
	if (notes.size() < 2 || (int) notes.size() * 2 < counted)
		return r;
	std::vector<float> sorted = notes;
	std::sort(sorted.begin(), sorted.end());
	const float median = sorted[sorted.size() / 2];
	int agree = 0;
	for (float m : notes)
		if (std::fabs(m - median) < 0.5f)
			++agree;
	if (agree * 10 < (int) notes.size() * 6)
		return r;
	// The mean of the windows that agree, for a finer FINE
	double sum = 0.0;
	for (float m : notes)
		if (std::fabs(m - median) < 0.5f)
			sum += m;
	const float midi = (float) (sum / agree);
	r.root = std::min(std::max((int) std::lround(midi), 12), 119);
	r.fine = std::min(std::max(-(midi - r.root) * 100.f, -100.f), 100.f);
	r.hz = 440.f * std::pow(2.f, (midi - 69.f) / 12.f);
	r.found = true;
	return r;
}

} // namespace sampler
} // namespace jxmp
