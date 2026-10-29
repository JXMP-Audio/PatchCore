/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-NSE: seven colours of noise with a level control.
 * Based on VCV Fundamental (GPL-3.0-or-later), src/Noise.cpp, Copyright (C) VCV and contributors:
 * generators, filters and calibration are unchanged.
 */

#include "JXMPWidget.hpp"
#include "dsp/Noise.hpp"

namespace {

struct InverseAWeightingFFTFilter {
	static constexpr int BUFFER_LEN = 1024;

	alignas(16) float inputBuffer[BUFFER_LEN] = {};
	alignas(16) float outputBuffer[BUFFER_LEN] = {};
	int frame = 0;
	dsp::RealFFT fft;

	InverseAWeightingFFTFilter() : fft(BUFFER_LEN) {}

	float process(float deltaTime, float x) {
		inputBuffer[frame] = x;
		if (++frame >= BUFFER_LEN) {
			frame = 0;
			alignas(16) float freqBuffer[BUFFER_LEN * 2];
			fft.rfft(inputBuffer, freqBuffer);

			for (int i = 0; i < BUFFER_LEN; i++) {
				float f = 1 / deltaTime / 2 / BUFFER_LEN * i;
				float amp = 0.f;
				if (80.f <= f && f <= 20000.f) {
					float f2 = f * f;
					// Inverse A-weighted curve
					amp = ((424.36f + f2) * std::sqrt((11599.3f + f2) * (544496.f + f2)) * (148693636.f + f2)) / (148693636.f * f2 * f2);
				}
				freqBuffer[2 * i + 0] *= amp / BUFFER_LEN;
				freqBuffer[2 * i + 1] *= amp / BUFFER_LEN;
			}

			fft.irfft(freqBuffer, outputBuffer);
		}
		return outputBuffer[frame];
	}
};


struct RackUniform {
	float operator()() {
		return random::uniform();
	}
};


struct Nse : engine::Module {
	enum ParamIds { LEVEL_PARAM, NUM_PARAMS };
	enum InputIds { LEVEL_INPUT, NUM_INPUTS };
	enum OutputIds { WHITE_OUTPUT, PINK_OUTPUT, RED_OUTPUT, VIOLET_OUTPUT, BLUE_OUTPUT, GRAY_OUTPUT, BLACK_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	jxmp::PinkNoise<8> pinkNoise;
	RackUniform uniform;
	dsp::IIRFilter<2, 2> redFilter;
	float lastWhite = 0.f;
	float lastPink = 0.f;
	InverseAWeightingFFTFilter grayFilter;

	Nse() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(LEVEL_PARAM, 0.f, 1.f, 1.f, "Level", "%", 0.f, 100.f);
		configInput(LEVEL_INPUT, "Level CV")->description = "Level CV: 0 V silent, 10 V full (times the LEVEL knob).";
		configOutput(WHITE_OUTPUT, "White")->description = "White noise: equal energy at every frequency, bright hiss.";
		configOutput(PINK_OUTPUT, "Pink")->description = "Pink noise (−3 dB/octave): equal energy per octave, a natural rushing sound.";
		configOutput(RED_OUTPUT, "Red")->description = "Red noise (−6 dB/octave): deep rumble, also good as a slow random CV.";
		configOutput(VIOLET_OUTPUT, "Violet")->description = "Violet noise (+6 dB/octave): very thin and bright.";
		configOutput(BLUE_OUTPUT, "Blue")->description = "Blue noise (+3 dB/octave): bright, thinner than white.";
		configOutput(GRAY_OUTPUT, "Gray")->description = "Gray noise: shaped so every frequency sounds equally loud to the ear.";
		configOutput(BLACK_OUTPUT, "Black")->description = "Black noise: random values spread evenly over ±5 V, useful as random CV.";
		// Butterworth lowpass, cutoff 20 Hz @ 44.1 kHz (as in Fundamental)
		const float b[] = {0.00425611, 0.00425611};
		const float a[] = {-0.99148778};
		redFilter.setCoefficients(b, a);
	}

	void process(const ProcessArgs& args) override {
		// Calibrated to the RMS of a 5 V sine, then scaled by the level (CV 0..10 V multiplies it)
		float level = params[LEVEL_PARAM].getValue();
		if (inputs[LEVEL_INPUT].isConnected())
			level *= math::clamp(inputs[LEVEL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const float gain = 5.f / std::sqrt(2.f) * level;

		const float white = random::normal();
		outputs[WHITE_OUTPUT].setVoltage(white * gain);
		if (outputs[RED_OUTPUT].isConnected())
			outputs[RED_OUTPUT].setVoltage(redFilter.process(white) / 0.0645f * gain);
		if (outputs[VIOLET_OUTPUT].isConnected())
			outputs[VIOLET_OUTPUT].setVoltage((white - lastWhite) / 1.41f * gain);
		lastWhite = white;
		if (outputs[GRAY_OUTPUT].isConnected())
			outputs[GRAY_OUTPUT].setVoltage(grayFilter.process(args.sampleTime, white) / 1.67f * gain);

		if (outputs[PINK_OUTPUT].isConnected() || outputs[BLUE_OUTPUT].isConnected()) {
			const float pink = pinkNoise.process(uniform) / 0.816f;
			outputs[PINK_OUTPUT].setVoltage(pink * gain);
			outputs[BLUE_OUTPUT].setVoltage((pink - lastPink) / 0.705f * gain);
			lastPink = pink;
		}
		if (outputs[BLACK_OUTPUT].isConnected())
			outputs[BLACK_OUTPUT].setVoltage((random::uniform() * 10.f - 5.f) * level);
	}
};


struct NseWidget : JXMPModuleWidget {
	NseWidget(Nse* const module) {
		setup(module, "JX-NSE", nvgRGB(0xc9, 0xcb, 0xd0), 6);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		section(3.f, 20.5f, 24.48f, 33.f, "NOISE");
		label(math::Vec(15.24f, 25.5f), "LEVEL", 7.5f);
		knob(math::Vec(15.24f, 41.5f), Nse::LEVEL_PARAM, 15.f, st::CAP_MAIN, 0.f, 1.f,
		     st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); }));

		// Colour legend: what the seven back outputs are
		section(3.f, 58.f, 24.48f, 59.f, "COLOURS");
		const char* names[7] = {"WHITE", "PINK", "RED", "VIOLET", "BLUE", "GRAY", "BLACK"};
		const char* slopes[7] = {"0 dB", "-3 dB", "-6 dB", "+6 dB", "+3 dB", "A-W", "UNI"};
		for (int i = 0; i < 7; ++i) {
			label(math::Vec(11.f, 64.f + i * 7.5f), names[i], 6.5f);
			label(math::Vec(22.f, 64.f + i * 7.5f), slopes[i], 6.f);
		}

		// Back: sockets
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(9.f, 32.f)), module, Nse::LEVEL_INPUT));
		for (int i = 0; i < Nse::NUM_OUTPUTS; ++i) {
			const math::Vec pos = i < 4 ? math::Vec(9.f, 50.f + i * 16.f) : math::Vec(22.f, 50.f + (i - 4) * 16.f);
			addOutput(createOutputCentered<PJ301MPort>(mm2px(pos), module, Nse::WHITE_OUTPUT + i));
		}
	}
};

} // namespace


Model* modelJXMPNoise = createModel<Nse, NseWidget>("Noise");
