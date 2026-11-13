/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-FLD and JX-RNG building blocks: a triangle wavefolder with symmetry, a ring modulator with dry/wet (both
 * also 4× oversampled), and
 * the logic and stepping of JX-LGC and JX-SW. No Rack dependency, tested in tests/test_shapers.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace jxmp {
namespace shapers {

/**
 * Wavefolder: the input (±5 V nominal) is amplified by 1 … 10 (FOLD 0 … 1), shifted by SYMMETRY (−1 … 1) and
 * folded back whenever it leaves ±5 V, so it stays within ±5 V and gains harmonics.
 */
inline float fold(float x, float amount, float symmetry) {
	float v = x / 5.f * (1.f + 9.f * std::min(std::max(amount, 0.f), 1.f)) + symmetry;
	// Triangle reflection into -1..1
	v = std::fmod(v + 1.f, 4.f);
	if (v < 0.f)
		v += 4.f;
	v = v < 2.f ? v - 1.f : 3.f - v;
	return 5.f * v;
}

/** 4× oversampling: windowed-sinc FIR (96 taps, Blackman, lowpass at 0.45 of the base rate; flat to 15 kHz, −4 dB
 * at 20 kHz) up and down. 96 taps, not 48: a ring product just above Nyquist (28 kHz) kept only 28 dB down. About 24
 * samples of delay in total. Nonlinear stages (folding, multiplying) run at 4× inside, so the harmonics they make
 * above the audio band are filtered out instead of folding back as tones off the pitch. */
struct Oversampler4 {
	static constexpr int kOver = 4, kTaps = 96, kPerPhase = kTaps / kOver;

	struct Taps {
		float h[kTaps];
		Taps() {
			double sum = 0.0;
			for (int i = 0; i < kTaps; ++i) {
				const double n = i - (kTaps - 1) / 2.0, x = n / kOver * 0.9, pi = 3.14159265358979;
				const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
				const double w = 0.42 - 0.5 * std::cos(2 * pi * i / (kTaps - 1)) + 0.08 * std::cos(4 * pi * i / (kTaps - 1));
				h[i] = (float) (sinc * w);
				sum += h[i];
			}
			for (float& v : h)
				v = (float) (v / sum);
		}
	};
	/** Made once (thread-safe), unity gain at DC */
	static const float* taps() {
		static const Taps t;
		return t.h;
	}

	/** One base-rate input → 4 samples at the high rate */
	struct Up {
		float in[kPerPhase] = {};
		int pos = 0;
		void process(float x, float out[kOver]) {
			const float* h = taps();
			in[pos] = std::isfinite(x) ? x : 0.f;
			for (int p = 0; p < kOver; ++p) {
				float u = 0.f;
				for (int j = 0; j < kPerPhase; ++j)
					u += h[p + kOver * j] * in[(pos - j + kPerPhase) % kPerPhase];
				out[p] = u * (float) kOver;  // zero-stuffing lost ×4
			}
			pos = (pos + 1) % kPerPhase;
		}
	};

	/** 4 high-rate samples → one base-rate output (only the sample kept is filtered) */
	struct Down {
		float buf[kTaps] = {};
		int pos = 0;
		float process(const float in[kOver]) {
			const float* h = taps();
			for (int p = 0; p < kOver; ++p) {
				buf[pos] = std::isfinite(in[p]) ? in[p] : 0.f;
				pos = (pos + 1) % kTaps;
			}
			float out = 0.f;
			for (int k = 0; k < kTaps; ++k)
				out += h[k] * buf[(pos - 1 - k + 2 * kTaps) % kTaps];
			return out;
		}
	};
};

/**
 * The wavefolder as JX-FLD runs it: 4× oversampled with first-order antiderivative anti-aliasing of the fold. The
 * plain fold left aliases only 16 dB below the harmonics for a 1.5 kHz note; this keeps them more than 50 dB down.
 * One per polyphony channel.
 */
struct Folder {
	Oversampler4::Up up;
	Oversampler4::Down down;
	double lastV = 0.0;
	bool started = false;

	/** The triangle fold of `v` (−1 … 1) and its antiderivative (periodic, continuous) */
	static double tri(double v) {
		double t = std::fmod(v + 1.0, 4.0);
		if (t < 0.0)
			t += 4.0;
		return t < 2.0 ? t - 1.0 : 3.0 - t;
	}
	static double triIntegral(double v) {
		double t = std::fmod(v + 1.0, 4.0);
		if (t < 0.0)
			t += 4.0;
		return t < 2.0 ? 0.5 * (t - 1.0) * (t - 1.0) - 0.5 : 0.5 - 0.5 * (t - 3.0) * (t - 3.0);
	}

	/** Same arguments as fold() */
	float process(float x, float amount, float symmetry) {
		const double gain = (1.0 + 9.0 * std::min(std::max(amount, 0.f), 1.f)) / 5.0;
		const double sym = std::isfinite(symmetry) ? symmetry : 0.f;
		float hi[Oversampler4::kOver];
		up.process(x, hi);
		for (float& u : hi) {
			const double v = u * gain + sym;
			double y;
			if (!started) {
				y = tri(v);
				started = true;
			}
			else if (std::fabs(v - lastV) > 1e-6)
				y = (triIntegral(v) - triIntegral(lastV)) / (v - lastV);
			else
				y = tri(0.5 * (v + lastV));
			lastV = v;
			u = (float) (5.0 * y);
		}
		return down.process(hi);
	}
};

/** Ring modulator: the product, scaled so two 5 V signals give 5 V; MIX 0 = A dry, 1 = the product */
inline float ring(float a, float b, float mix) {
	mix = std::min(std::max(mix, 0.f), 1.f);
	return a * (1.f - mix) + (a * b / 5.f) * mix;
}

/** The ring modulator as JX-RNG runs it: 4× oversampled. The product holds the sum of the two frequencies, which for
 * high notes lies above the Nyquist frequency and came back as a tone off the pitch as loud as the rest. One per
 * polyphony channel. */
struct RingMod {
	Oversampler4::Up upA, upB;
	Oversampler4::Down down;

	float process(float a, float b, float mix) {
		float ha[Oversampler4::kOver], hb[Oversampler4::kOver];
		upA.process(a, ha);
		upB.process(b, hb);
		for (int p = 0; p < Oversampler4::kOver; ++p)
			ha[p] = ring(ha[p], hb[p], mix);
		return down.process(ha);
	}
};

/** Gate logic of JX-LGC (high above 1 V) */
struct Gates {
	bool a, b;
	bool andGate() const { return a && b; }
	bool orGate() const { return a || b; }
	bool xorGate() const { return a != b; }
};

/** Sequential switch of JX-SW: steps 0 … length-1 on each clock, back to 0 on reset */
struct Stepper {
	int step = 0;
	void clock(int length) {
		length = std::min(std::max(length, 2), 4);
		step = (step + 1) % length;
	}
	void reset() {
		step = 0;
	}
	/** A step saved with the patch, kept inside 0..3 */
	void restore(int s) {
		step = std::min(std::max(s, 0), 3);
	}
	int current(int length) const {
		return step % std::min(std::max(length, 2), 4);
	}
};

} // namespace shapers
} // namespace jxmp
