/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-LFO: low-frequency oscillator. Behaviour (rate law, clock, reset, offset, invert) follows
 * VCV Fundamental's LFO (GPL-3.0-or-later).
 */

#include "JXMPWidget.hpp"
#include "dsp/Lfo.hpp"

namespace {

struct Lfo : engine::Module {
	enum ParamIds { FREQ_PARAM, FM_PARAM, PW_PARAM, PWM_PARAM, OFFSET_PARAM, INVERT_PARAM, NUM_PARAMS };
	enum InputIds { FM_INPUT, CLOCK_INPUT, RESET_INPUT, PW_INPUT, NUM_INPUTS };
	enum OutputIds { SIN_OUTPUT, TRI_OUTPUT, SAW_OUTPUT, SQR_OUTPUT, NUM_OUTPUTS };
	enum LightIds { PHASE_LIGHT, NUM_LIGHTS };

	float phases[16] = {};
	dsp::SchmittTrigger resetTriggers[16];
	dsp::SchmittTrigger clockTrigger;
	dsp::Timer clockTimer;
	float clockFreq = 2.f;

	Lfo() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(FREQ_PARAM, -8.f, 10.f, 1.f, "Rate", " Hz", 2.f, 1.f);
		configParam(FM_PARAM, -1.f, 1.f, 0.f, "Rate modulation", "%", 0.f, 100.f);
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configParam(PWM_PARAM, -1.f, 1.f, 0.f, "Pulse width modulation", "%", 0.f, 100.f);
		configSwitch(OFFSET_PARAM, 0.f, 1.f, 0.f, "Offset", {"Bipolar", "Unipolar"});
		configSwitch(INVERT_PARAM, 0.f, 1.f, 0.f, "Invert", {"Normal", "Inverted"});
		configInput(FM_INPUT, "Rate CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(PW_INPUT, "PWM");
		configOutput(SIN_OUTPUT, "Sin");
		configOutput(TRI_OUTPUT, "Tri");
		configOutput(SAW_OUTPUT, "Saw");
		configOutput(SQR_OUTPUT, "Sqr");
	}

	void process(const ProcessArgs& args) override {
		const bool unipolar = params[OFFSET_PARAM].getValue() > 0.5f;
		const bool invert = params[INVERT_PARAM].getValue() > 0.5f;

		// External clock sets the base rate, as in Fundamental
		if (inputs[CLOCK_INPUT].isConnected()) {
			clockTimer.process(args.sampleTime);
			if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) {
				const float f = 1.f / clockTimer.getTime();
				clockTimer.reset();
				if (0.001f <= f && f <= 1000.f)
					clockFreq = f;
			}
		}
		else {
			clockFreq = 2.f;
		}

		const int channels = std::max(1, inputs[FM_INPUT].getChannels());
		for (int c = 0; c < channels; ++c) {
			const float pitch = params[FREQ_PARAM].getValue() + inputs[FM_INPUT].getVoltage(c) * params[FM_PARAM].getValue();
			const float pw = math::clamp(params[PW_PARAM].getValue() + inputs[PW_INPUT].getPolyVoltage(c) / 10.f * params[PWM_PARAM].getValue(), 0.01f, 0.99f);
			phases[c] = jxmp::lfoAdvance(phases[c], jxmp::lfoFreq(pitch, clockFreq), args.sampleTime);
			if (resetTriggers[c].process(inputs[RESET_INPUT].getPolyVoltage(c), 0.1f, 2.f))
				phases[c] = 0.f;

			// ±5 V bipolar, 0..10 V unipolar
			for (int s = 0; s < 4; ++s) {
				const float v = jxmp::lfoOut((jxmp::LfoShape) s, phases[c], pw, unipolar, invert);
				outputs[SIN_OUTPUT + s].setVoltage(unipolar ? 10.f * v : 5.f * v, c);
			}
		}
		for (int s = 0; s < NUM_OUTPUTS; ++s)
			outputs[s].setChannels(channels);
		lights[PHASE_LIGHT].setBrightnessSmooth(jxmp::lfoOut(jxmp::LFO_SIN, phases[0], 0.5f, true, false), args.sampleTime);
	}
};


struct LfoWidget : JXMPModuleWidget {
	LfoWidget(Lfo* const module) {
		setup(module, "JX-LFO", nvgRGB(0x9a, 0x6b, 0xd6), 10);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const float nameSize = 7.5f;

		// RATE: 2^param Hz, a tick every two octaves, labelled at both ends and at the default 2 Hz
		section(4.f, 20.5f, 42.8f, 33.f, "RATE");
		std::vector<st::Mark> rateMarks;
		for (int p = -8; p <= 10; p += 2)
			rateMarks.push_back({(float) p, ""});
		rateMarks.front().label = ".004";
		rateMarks.back().label = "1k";
		rateMarks.insert(rateMarks.begin() + 5, st::Mark{1.f, "2"});
		label(math::Vec(16.f, 25.f), "RATE", nameSize);
		knob(math::Vec(16.f, 41.5f), Lfo::FREQ_PARAM, 15.f, st::CAP_MAIN, -8.f, 10.f, rateMarks);
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(22.5f, 25.f)), module, Lfo::PHASE_LIGHT));
		label(math::Vec(37.f, 25.f), "RATE CV", nameSize);
		knob(math::Vec(37.f, 41.5f), Lfo::FM_PARAM, 7.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());

		// SHAPE
		section(4.f, 57.f, 42.8f, 28.f, "PULSE");
		label(math::Vec(16.f, 62.f), "PW", nameSize);
		knob(math::Vec(16.f, 75.f), Lfo::PW_PARAM, 9.f, st::CAP_WHITE, 0.01f, 0.99f,
		     {{0.01f, "1"}, {0.1f, ""}, {0.2f, ""}, {0.3f, ""}, {0.4f, ""}, {0.5f, "50"}, {0.6f, ""}, {0.7f, ""}, {0.8f, ""}, {0.9f, ""}, {0.99f, "99"}});
		label(math::Vec(37.f, 62.f), "PWM", nameSize);
		knob(math::Vec(37.f, 75.f), Lfo::PWM_PARAM, 7.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());

		// OUTPUT
		section(4.f, 88.5f, 42.8f, 29.f, "OUTPUT");
		label(math::Vec(14.f, 93.5f), "OFFSET", nameSize);
		toggle(math::Vec(14.f, 106.f), Lfo::OFFSET_PARAM, 2, {"UNI", "BI"});
		label(math::Vec(35.f, 93.5f), "INVERT", nameSize);
		toggle(math::Vec(35.f, 106.f), Lfo::INVERT_PARAM, 2, {"INV", "NORM"});

		// Back: sockets
		for (int i = 0; i < Lfo::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 32.f + i * 15.f)), module, Lfo::FM_INPUT + i));
		for (int i = 0; i < Lfo::NUM_OUTPUTS; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(37.f, 32.f + i * 15.f)), module, Lfo::SIN_OUTPUT + i));
	}
};

} // namespace


Model* modelJXMPLFO = createModel<Lfo, LfoWidget>("LFO");
