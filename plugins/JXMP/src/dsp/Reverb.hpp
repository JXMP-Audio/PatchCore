/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-REV: an 8-line feedback delay network (Householder mixing) with input diffusion, damping in the loop and a
 * predelay. PLATE (dense, bright), HALL (large), ROOM (small) set the line lengths and the diffusion. Decay is a
 * real RT60. No Rack dependency, tested in tests/test_reverb.cpp.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace jxmp {
namespace reverb {

enum Algo { PLATE = 0, HALL, ROOM };

struct Delay {
	std::vector<float> buf;
	int pos = 0;

	void resize(int n) {
		buf.assign(std::max(n, 2), 0.f);
		pos = 0;
	}
	/** Sample written `delay` samples ago (1 … size-1) */
	float read(int delay) const {
		const int n = (int) buf.size();
		return buf[(pos - std::min(std::max(delay, 1), n - 1) + n) % n];
	}
	void write(float x) {
		buf[pos] = x;
		pos = (pos + 1) % (int) buf.size();
	}
};

struct Allpass {
	Delay d;
	int length = 1;
	float process(float x, float g) {
		const float delayed = d.read(length);
		const float y = -g * x + delayed;
		d.write(x + g * delayed);
		return y;
	}
};

constexpr int kLines = 8;

struct Fdn {
	float sampleRate = 0.f;
	Delay lines[kLines];
	int lengths[kLines] = {};
	float damp[kLines] = {};
	Allpass diffusers[4];
	Delay pre;
	/** Feedback gains, recomputed only when the settings change */
	float gains[kLines] = {};
	/** Input level per line: sqrt(1 - g²), so a long decay is not louder than a short one */
	float inject[kLines] = {};
	float lastSize = -1.f, lastRt60 = -1.f;
	int lastAlgo = -1;

	/** Line lengths in ms at SIZE 1 for each algorithm; mutually prime-ish so the echoes do not line up */
	static const float* baseMs(Algo a) {
		static const float plate[kLines] = {11.3f, 13.7f, 17.9f, 19.1f, 23.3f, 29.9f, 31.1f, 37.7f};
		static const float hall[kLines] = {31.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.9f, 67.1f};
		static const float room[kLines] = {8.3f, 9.7f, 11.9f, 13.1f, 14.3f, 16.7f, 18.1f, 19.9f};
		return a == PLATE ? plate : a == HALL ? hall : room;
	}

	void setSampleRate(float fs) {
		if (fs == sampleRate)
			return;
		sampleRate = fs;
		const int maxLine = (int) (fs * 0.067f * 2.f) + 4;  // HALL at SIZE 2
		for (Delay& l : lines)
			l.resize(maxLine);
		static const float diffMs[4] = {4.7f, 3.6f, 12.7f, 9.3f};
		for (int i = 0; i < 4; ++i) {
			diffusers[i].d.resize((int) (fs * diffMs[i] / 1000.f) + 4);
			diffusers[i].length = (int) (fs * diffMs[i] / 1000.f);
		}
		pre.resize((int) (fs * 0.25f) + 4);
		std::fill(damp, damp + kLines, 0.f);
		lastSize = -1.f;  // recompute the lengths for the new rate
	}

	/**
	 * One stereo sample in, the wet stereo signal out. size 0.25..2 (line length scale), rt60 seconds,
	 * predelay seconds (0..0.2), tone 0..1 (dark … bright damping).
	 */
	void process(float inL, float inR, Algo algo, float size, float rt60, float predelay, float tone, float* outL, float* outR) {
		const float fs = sampleRate;
		if (size != lastSize || rt60 != lastRt60 || (int) algo != lastAlgo) {
			lastSize = size;
			lastRt60 = rt60;
			lastAlgo = (int) algo;
			const float* ms = baseMs(algo);
			for (int i = 0; i < kLines; ++i) {
				lengths[i] = std::max(8, (int) (fs * ms[i] * size / 1000.f));
				// −60 dB after rt60 seconds, whatever the line length
				gains[i] = std::pow(10.f, -3.f * lengths[i] / fs / std::max(rt60, 0.05f));
				inject[i] = std::sqrt(std::max(0.f, 1.f - gains[i] * gains[i]));
			}
		}

		// Predelay, then diffusion (a plate diffuses most)
		pre.write(0.5f * (inL + inR));
		float x = pre.read(std::max(1, (int) (predelay * fs)));
		const float g = algo == PLATE ? 0.75f : algo == HALL ? 0.65f : 0.55f;
		for (Allpass& a : diffusers)
			x = a.process(x, g);

		// Read the lines, damp, mix (Householder), feed back with the decay gain of each line
		float y[kLines];
		float sum = 0.f;
		const float cutoff = 1500.f + 10500.f * tone;
		const float k = 1.f - std::exp(-2.f * float(M_PI) * cutoff / fs);
		for (int i = 0; i < kLines; ++i) {
			const float r = lines[i].read(lengths[i]);
			damp[i] += k * (r - damp[i]);
			y[i] = damp[i];
			sum += y[i];
		}
		const float h = 2.f / kLines * sum;
		float l = 0.f, rr = 0.f;
		for (int i = 0; i < kLines; ++i) {
			lines[i].write((y[i] - h) * gains[i] + (i % 2 ? x : -x) * inject[i]);
			if (i % 2)
				rr += y[i];
			else
				l += y[i];
		}
		// Soft limit at ±10 V
		*outL = 10.f * std::tanh(l * 0.35f / 10.f);
		*outR = 10.f * std::tanh(rr * 0.35f / 10.f);
	}
};

} // namespace reverb
} // namespace jxmp
