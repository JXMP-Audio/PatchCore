/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SMPL envelopes in the style of FastTracker 2 instruments, no Rack dependency: 2-12 points (time in seconds from
 * the gate, value 0..1), a sustain point that holds while the gate is high, a loop between two points. Volume uses the
 * value as a gain; pan reads 0.5 as the PAN knob, 1 as left and 0 as right.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace sampler {

constexpr float kEnvMaxTime = 20.f;  // seconds, the latest a point can be

struct Envelope {
	enum { kMax = 12 };
	bool on = false;
	int n = 2;
	float t[kMax] = {0.f, 1.f};
	float v[kMax] = {1.f, 1.f};
	int sus = -1, ls = -1, le = -1;  // point indices, -1 = none
};

/** The envelope's value at time t: straight lines between the points, the last value after the last point */
inline float envValue(const Envelope& e, float t) {
	if (e.n <= 0)
		return 1.f;
	if (t <= e.t[0])
		return e.v[0];
	for (int i = 1; i < e.n; ++i) {
		if (t <= e.t[i]) {
			const float span = e.t[i] - e.t[i - 1];
			return span > 0.f ? e.v[i - 1] + (e.v[i] - e.v[i - 1]) * (t - e.t[i - 1]) / span : e.v[i];
		}
	}
	return e.v[e.n - 1];
}

/** Where a playing envelope is */
struct EnvRun {
	float t = 0.f;

	/** Moves on by dt. `held`: the sustain point holds (gate high in GATE mode). `loopOn`: the loop applies. */
	void step(const Envelope& e, float dt, bool held, bool loopOn) {
		if (held && e.sus >= 0 && e.sus < e.n) {
			const float ts = e.t[e.sus];
			if (t <= ts && t + dt >= ts) {
				t = ts;
				return;
			}
		}
		t += dt;
		if (loopOn && e.ls >= 0 && e.le > e.ls && e.le < e.n) {
			const float a = e.t[e.ls], b = e.t[e.le];
			if (b > a && t >= b)
				t = a + std::fmod(t - a, b - a);
		}
		t = std::min(t, kEnvMaxTime + 1.f);
	}
	bool pastEnd(const Envelope& e) const { return e.n <= 0 || t >= e.t[e.n - 1]; }
};

/** Makes any envelope (from a patch file) usable: 2..12 points, the first at 0, times rising and inside 0..20 s,
 * values 0..1, sustain and loop on existing points with the loop start before its end */
inline void envSanitize(Envelope& e) {
	e.n = std::min(std::max(e.n, 2), (int) Envelope::kMax);
	for (int k = 0; k < e.n; ++k) {
		const float t = std::isfinite(e.t[k]) ? e.t[k] : 0.f;
		const float lo = k ? e.t[k - 1] : 0.f;
		e.t[k] = k ? std::min(std::max(t, lo), kEnvMaxTime) : 0.f;
		e.v[k] = std::isfinite(e.v[k]) ? std::min(std::max(e.v[k], 0.f), 1.f) : 0.f;
	}
	if (e.sus < 0 || e.sus >= e.n)
		e.sus = -1;
	if (e.ls < 0 || e.le < 0 || e.ls >= e.n || e.le >= e.n || e.ls > e.le)
		e.ls = e.le = -1;
}

/** Adds a point at (t, v) in time order; returns its index, or -1 when there are 12 already. The sustain and loop
 * stay on the same points. */
inline int envInsert(Envelope& e, float t, float v) {
	if (e.n >= Envelope::kMax)
		return -1;
	t = std::min(std::max(t, 0.001f), kEnvMaxTime);
	int i = e.n;
	for (int k = 1; k < e.n; ++k) {
		if (e.t[k] > t) {
			i = k;
			break;
		}
	}
	for (int k = e.n; k > i; --k) {
		e.t[k] = e.t[k - 1];
		e.v[k] = e.v[k - 1];
	}
	e.t[i] = t;
	e.v[i] = std::min(std::max(v, 0.f), 1.f);
	++e.n;
	for (int* p : {&e.sus, &e.ls, &e.le})
		if (*p >= i)
			++*p;
	return i;
}

/** Removes point i (not the first, and never below 2 points). A sustain on it goes; a loop that loses an end goes. */
inline bool envRemove(Envelope& e, int i) {
	if (i <= 0 || i >= e.n || e.n <= 2)
		return false;
	for (int k = i; k + 1 < e.n; ++k) {
		e.t[k] = e.t[k + 1];
		e.v[k] = e.v[k + 1];
	}
	--e.n;
	if (e.sus == i)
		e.sus = -1;
	else if (e.sus > i)
		--e.sus;
	if (e.ls == i || e.le == i)
		e.ls = e.le = -1;
	else {
		if (e.ls > i)
			--e.ls;
		if (e.le > i)
			--e.le;
	}
	return true;
}

/** Moves point i to (t, v): the first point stays at 0, the others between their neighbours */
inline void envMove(Envelope& e, int i, float t, float v) {
	if (i < 0 || i >= e.n)
		return;
	e.v[i] = std::min(std::max(v, 0.f), 1.f);
	if (i == 0) {
		e.t[0] = 0.f;
		return;
	}
	const float lo = e.t[i - 1] + 0.001f;
	const float hi = i + 1 < e.n ? e.t[i + 1] - 0.001f : kEnvMaxTime;
	e.t[i] = std::min(std::max(t, lo), std::max(lo, hi));
}

/** The pan a slot ends up at: the PAN knob (-1 left .. 1 right) moved by the pan envelope (0.5 = no change, 1 =
 * left, 0 = right) as far as there is room on that side (FT2's formula) */
inline float envPan(float pan, float env) {
	const float p = pan - (env - 0.5f) * 2.f * (1.f - std::fabs(pan));
	return std::min(std::max(p, -1.f), 1.f);
}

/** RESET: a quick attack, a dip, held at point 4 while the gate is high, then gone */
inline Envelope defaultVolEnv() {
	Envelope e;
	e.n = 5;
	const float t[5] = {0.f, 0.012f, 0.25f, 0.9f, 1.5f}, v[5] = {0.f, 1.f, 0.62f, 0.55f, 0.f};
	std::copy(t, t + 5, e.t);
	std::copy(v, v + 5, e.v);
	e.sus = 3;
	return e;
}

/** RESET: right, left, right in a loop (auto-pan), back to the middle at the end */
inline Envelope defaultPanEnv() {
	Envelope e;
	e.n = 5;
	const float t[5] = {0.f, 0.2f, 0.45f, 0.7f, 1.5f}, v[5] = {0.5f, 0.15f, 0.85f, 0.15f, 0.5f};
	std::copy(t, t + 5, e.t);
	std::copy(v, v + 5, e.v);
	e.ls = 1;
	e.le = 3;
	return e;
}

} // namespace sampler
} // namespace jxmp
