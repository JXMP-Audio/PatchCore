/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SEQ: 8-step CV/gate sequencer with an internal clock or an external one.
 */

#include "JXMPWidget.hpp"
#include "dsp/Seq.hpp"

namespace {

struct Seq : engine::Module {
	enum ParamIds { ENUMS(CV_PARAM, 8), ENUMS(GATE_PARAM, 8), TEMPO_PARAM, LENGTH_PARAM, RUN_PARAM, RESET_PARAM, NUM_PARAMS };
	enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, NUM_INPUTS };
	enum OutputIds { CV_OUTPUT, GATE_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(STEP_LIGHT, 8), ENUMS(GATE_LIGHT, 8), RUN_LIGHT, CLOCK_LIGHT, NUM_LIGHTS };

	jxmp::StepCounter counter;
	jxmp::InternalClock internalClock;
	dsp::SchmittTrigger clockTrigger, resetTrigger, resetButton;
	bool lastClockHigh = false;

	Seq() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 8; ++i) {
			configParam(CV_PARAM + i, 0.f, 10.f, 0.f, string::f("Step %d CV", i + 1), " V");
			configSwitch(GATE_PARAM + i, 0.f, 1.f, 1.f, string::f("Step %d gate", i + 1), {"Off", "On"});
		}
		configParam(TEMPO_PARAM, 30.f, 300.f, 120.f, "Tempo (internal clock)", " BPM");
		configParam(LENGTH_PARAM, 1.f, 8.f, 8.f, "Length")->snapEnabled = true;
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(RESET_PARAM, "Reset");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run (gate)");
		configOutput(CV_OUTPUT, "CV");
		configOutput(GATE_OUTPUT, "Gate");
	}

	void onReset() override {
		counter.reset();
	}

	void process(const ProcessArgs& args) override {
		const bool running = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f : params[RUN_PARAM].getValue() > 0.5f;
		const int length = (int) params[LENGTH_PARAM].getValue();

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f) | resetButton.process(params[RESET_PARAM].getValue()))
			counter.reset();

		// External clock if patched, else the internal one (only while running)
		bool clockHigh;
		if (inputs[CLOCK_INPUT].isConnected()) {
			clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
			clockHigh = clockTrigger.isHigh();
		}
		else {
			clockHigh = running && internalClock.process(params[TEMPO_PARAM].getValue(), args.sampleTime);
			if (!running)
				internalClock.phase = 0.f;
		}
		if (running && clockHigh && !lastClockHigh)
			counter.clock(length);
		lastClockHigh = clockHigh;

		const int step = std::min(counter.step, length - 1);
		outputs[CV_OUTPUT].setVoltage(params[CV_PARAM + step].getValue());
		const bool gate = running && clockHigh && params[GATE_PARAM + step].getValue() > 0.5f;
		outputs[GATE_OUTPUT].setVoltage(gate ? 10.f : 0.f);

		for (int i = 0; i < 8; ++i) {
			lights[STEP_LIGHT + i].setBrightness(i == step ? 1.f : (i < length ? 0.08f : 0.f));
			lights[GATE_LIGHT + i].setBrightness(params[GATE_PARAM + i].getValue() > 0.5f);
		}
		lights[RUN_LIGHT].setBrightness(running);
		lights[CLOCK_LIGHT].setBrightness(clockHigh);
	}
};


struct SeqWidget : JXMPModuleWidget {
	SeqWidget(Seq* const module) {
		setup(module, "JX-SEQ", nvgRGB(0x2f, 0xb8, 0xc4), 18);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const std::vector<st::Mark> volts = st::marks(0.f, 10.f, 1.f, 5, [](float v) { return string::f("%d", (int) v); });

		// STEPS: two rows of four, each with its number + step LED, CV knob and gate button
		section(4.f, 20.5f, 83.44f, 69.f, "STEPS");
		for (int i = 0; i < 8; ++i) {
			const float x = 15.f + (i % 4) * 20.5f;
			const float y = i < 4 ? 0.f : 34.f;
			label(math::Vec(x - 2.f, 26.f + y), string::f("%d", i + 1).c_str(), 8.f);
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(x + 3.f, 26.f + y)), module, Seq::STEP_LIGHT + i));
			knob(math::Vec(x, 38.5f + y), Seq::CV_PARAM + i, 9.f, st::CAP_MAIN, 0.f, 10.f, volts);
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(mm2px(math::Vec(x, 50.5f + y)), module,
			                                                                                 Seq::GATE_PARAM + i, Seq::GATE_LIGHT + i));
		}

		// CONTROL
		section(4.f, 93.f, 83.44f, 24.5f, "CONTROL");
		label(math::Vec(15.f, 98.f), "TEMPO", 7.5f);
		addChild(createLightCentered<TinyLight<GreenLight>>(mm2px(math::Vec(22.f, 98.f)), module, Seq::CLOCK_LIGHT));
		knob(math::Vec(15.f, 108.5f), Seq::TEMPO_PARAM, 9.f, st::CAP_WHITE, 30.f, 300.f,
		     {{30.f, "30"}, {60.f, ""}, {90.f, ""}, {120.f, "120"}, {150.f, ""}, {180.f, ""}, {210.f, ""}, {240.f, ""}, {270.f, ""}, {300.f, "300"}});
		label(math::Vec(35.5f, 98.f), "LENGTH", 7.5f);
		knob(math::Vec(35.5f, 108.5f), Seq::LENGTH_PARAM, 9.f, st::CAP_BLACK, 1.f, 8.f,
		     {{1.f, "1"}, {2.f, ""}, {3.f, ""}, {4.f, ""}, {5.f, ""}, {6.f, ""}, {7.f, ""}, {8.f, "8"}});
		label(math::Vec(56.f, 98.f), "RUN", 7.5f);
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(mm2px(math::Vec(56.f, 108.5f)), module,
		                                                                                 Seq::RUN_PARAM, Seq::RUN_LIGHT));
		label(math::Vec(76.5f, 98.f), "RESET", 7.5f);
		addParam(createParamCentered<VCVButton>(mm2px(math::Vec(76.5f, 108.5f)), module, Seq::RESET_PARAM));

		// Back: sockets
		for (int i = 0; i < Seq::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(16.f, 32.f + i * 16.f)), module, Seq::CLOCK_INPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(60.f, 32.f)), module, Seq::CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(60.f, 48.f)), module, Seq::GATE_OUTPUT));
	}
};

} // namespace


Model* modelJXMPSeq = createModel<Seq, SeqWidget>("SEQ");
