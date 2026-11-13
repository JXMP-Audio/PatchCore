/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-REV: an 8-line feedback delay network (Householder mixing) with input diffusion, damping in the loop and a
 * predelay. PLATE (dense, bright), HALL (large), ROOM (small) set the line lengths and the diffusion. Decay is a
 * real RT60. Every line length moves slowly by up to 1.2 ms (each at its own rate): fixed lengths ring like
 * metal, because the few resonances of the network keep their pitch. SIZE and PREDELAY glide and are read between
 * samples, so turning them does not click. No Rack dependency, tested in tests/test_reverb.cpp.
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
	/** Sample written `delay` samples ago, between samples (1 … size-2) */
	float readFrac(float delay) const {
		const int n = (int) buf.size();
		delay = std::min(std::max(delay, 1.f), (float) (n - 2));
		const int i = (int) delay;
		const float f = delay - (float) i;
		const float a = buf[(pos - i + n) % n], b = buf[(pos - i - 1 + n) % n];
		return a + (b - a) * f;
	}
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
	float lengths[kLines] = {};
	/** Phase of the slow length modulation of each line */
	float modPhase[kLines] = {};
	/** SIZE and PREDELAY after the glide (negative: not set yet) */
	float sizeNow = -1.f, predelayNow = -1.f;
	/** 20 Hz highpass at the input (state per side): the network has a large gain at the lowest frequencies, so DC or
	 * subsonic rumble would come out many times louder, with opposite signs on the two sides */
	float hpInL = 0.f, hpOutL = 0.f, hpInR = 0.f, hpOutR = 0.f;
	float damp[kLines] = {};
	/** Predelay and diffusion per input side: left feeds the even lines, right the odd ones */
	Allpass diffusers[4], diffusersR[4];
	Delay pre, preR;
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
		const int maxLine = (int) (fs * (0.0672f * 2.f + 2.f * kModSeconds)) + 8;  // HALL (67.1 ms) at SIZE 2 + modulation
		for (Delay& l : lines)
			l.resize(maxLine);
		static const float diffMs[4] = {4.7f, 3.6f, 12.7f, 9.3f};
		for (int i = 0; i < 4; ++i) {
			for (Allpass* a : {&diffusers[i], &diffusersR[i]}) {
				a->d.resize((int) (fs * diffMs[i] / 1000.f) + 4);
				a->length = (int) (fs * diffMs[i] / 1000.f);
			}
		}
		pre.resize((int) (fs * 0.25f) + 4);
		preR.resize((int) (fs * 0.25f) + 4);
		std::fill(damp, damp + kLines, 0.f);
		lastSize = -1.f;  // recompute the lengths for the new rate
		sizeNow = predelayNow = -1.f;
	}

	/**
	 * One stereo sample in, the wet stereo signal out. size 0.25..2 (line length scale), rt60 seconds,
	 * predelay seconds (0..0.2), tone 0..1 (dark … bright damping).
	 */
	/** Silence in every line (after a NaN got in) */
	void clear() {
		for (Delay& l : lines)
			std::fill(l.buf.begin(), l.buf.end(), 0.f);
		for (Allpass& a : diffusers)
			std::fill(a.d.buf.begin(), a.d.buf.end(), 0.f);
		for (Allpass& a : diffusersR)
			std::fill(a.d.buf.begin(), a.d.buf.end(), 0.f);
		std::fill(pre.buf.begin(), pre.buf.end(), 0.f);
		std::fill(preR.buf.begin(), preR.buf.end(), 0.f);
		std::fill(damp, damp + kLines, 0.f);
		hpInL = hpOutL = hpInR = hpOutR = 0.f;
	}

	/** Depth of the line length modulation (the length moves between +0 and +2 × this) */
	static constexpr float kModSeconds = 0.0006f;
	/** Wet level: four lines per side summed; brings a signal out about as loud as it went in */
	static float outputGain(Algo a) {
		return a == PLATE ? 0.105f : a == HALL ? 0.16f : 0.23f;
	}

	void process(float inL, float inR, Algo algo, float size, float rt60, float predelay, float tone, float* outL, float* outR) {
		const float fs = sampleRate;
		// SIZE and PREDELAY glide (~30 ms), so a turned knob does not jump the read positions
		const float glide = std::min(1.f, 1.f / (0.03f * fs));
		if (std::isfinite(size))
			sizeNow = sizeNow < 0.f ? size : sizeNow + (size - sizeNow) * glide;
		if (std::isfinite(predelay))
			predelayNow = predelayNow < 0.f ? predelay : predelayNow + (predelay - predelayNow) * glide;
		size = sizeNow < 0.f ? 1.f : sizeNow;
		predelay = std::min(std::max(predelayNow, 0.f), 0.2f);
		// A NaN (from another module or a CV) would circulate in the lines for good
		if (!std::isfinite(rt60))
			rt60 = 1.f;
		// Recomputed when a setting moved noticeably (the glide moves SIZE a little on every sample)
		if (std::fabs(size - lastSize) > 1e-4f || rt60 != lastRt60 || (int) algo != lastAlgo) {
			lastSize = size;
			lastRt60 = rt60;
			lastAlgo = (int) algo;
			const float* ms = baseMs(algo);
			for (int i = 0; i < kLines; ++i) {
				lengths[i] = std::max(8.f, fs * ms[i] * size / 1000.f);
				// −60 dB after rt60 seconds, whatever the line length
				gains[i] = std::pow(10.f, -3.f * lengths[i] / fs / std::max(rt60, 0.05f));
				inject[i] = std::sqrt(std::max(0.f, 1.f - gains[i] * gains[i]));
			}
		}

		// Predelay, then diffusion (a plate diffuses most)
		inL = std::isfinite(inL) ? inL : 0.f;
		inR = std::isfinite(inR) ? inR : 0.f;
		const float hp = 1.f - 2.f * float(M_PI) * 20.f / fs;
		hpOutL = 0.5f * (1.f + hp) * (inL - hpInL) + hp * hpOutL;
		hpInL = inL;
		hpOutR = 0.5f * (1.f + hp) * (inR - hpInR) + hp * hpOutR;
		hpInR = inR;
		pre.write(hpOutL);
		preR.write(hpOutR);
		const float preSamples = std::max(1.f, predelay * fs);
		float x = pre.readFrac(preSamples), xr = preR.readFrac(preSamples);
		// Diffusion: stronger allpasses rang metallic themselves (0.75 / 0.65 / 0.55 before 2026-11-13)
		const float g = algo == PLATE ? 0.6f : algo == HALL ? 0.5f : 0.45f;
		for (Allpass& a : diffusers)
			x = a.process(x, g);
		for (Allpass& a : diffusersR)
			xr = a.process(xr, g);

		// Read the lines, damp, mix (Householder), feed back with the decay gain of each line
		float y[kLines];
		float sum = 0.f;
		const float cutoff = 1500.f + 10500.f * tone;
		const float k = 1.f - std::exp(-2.f * float(M_PI) * cutoff / fs);
		for (int i = 0; i < kLines; ++i) {
			modPhase[i] += (0.31f + 0.13f * i) / fs;
			modPhase[i] -= std::floor(modPhase[i]);
			const float wobble = kModSeconds * fs * (1.f + std::sin(2.f * float(M_PI) * modPhase[i]));
			const float r = lines[i].readFrac(lengths[i] + wobble);
			damp[i] += k * (r - damp[i]);
			y[i] = damp[i];
			sum += y[i];
		}
		const float h = 2.f / kLines * sum;
		float l = 0.f, rr = 0.f;
		for (int i = 0; i < kLines; ++i) {
			lines[i].write((y[i] - h) * gains[i] + (i % 2 ? xr : -x) * inject[i]);
			if (i % 2)
				rr += y[i];
			else
				l += y[i];
		}
		// Soft limit at ±10 V
		const float gain = outputGain(algo);
		*outL = 10.f * std::tanh(l * gain / 10.f);
		*outR = 10.f * std::tanh(rr * gain / 10.f);
		if (!std::isfinite(*outL) || !std::isfinite(*outR)) {
			clear();
			*outL = *outR = 0.f;
		}
	}
};

} // namespace reverb
} // namespace jxmp
