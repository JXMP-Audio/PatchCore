/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SW: sequential switch. Each CLOCK moves one step (LENGTH 2-4, RESET back to 1). 4 → 1: OUT passes the
 * input of the current step. 1 → 4: IN goes to the output of the current step. Polyphonic.
 * Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Shapers.hpp"

namespace {

struct Sw : engine::Module {
	enum ParamIds { LENGTH_PARAM, NUM_PARAMS };
	enum InputIds { ENUMS(IN_INPUT, 4), SINGLE_INPUT, CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, ENUMS(SPLIT_OUTPUT, 4), NUM_OUTPUTS };
	enum LightIds { ENUMS(STEP_LIGHT, 4), NUM_LIGHTS };

	jxmp::shapers::Stepper stepper;
	dsp::SchmittTrigger clockTrigger, resetTrigger;

	Sw() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(LENGTH_PARAM, 2.f, 4.f, 4.f, "Length", " steps")->snapEnabled = true;
		paramQuantities[LENGTH_PARAM]->description = "How many steps before it comes back to step 1.";
		for (int i = 0; i < 4; ++i) {
			configInput(IN_INPUT + i, string::f("In %d", i + 1))->description = string::f("4 -> 1: passed to OUT on step %d.", i + 1);
			configOutput(SPLIT_OUTPUT + i, string::f("Out %d", i + 1))->description = string::f("1 -> 4: gets IN on step %d.", i + 1);
		}
		configInput(SINGLE_INPUT, "In")->description = "1 -> 4: sent to the output of the current step.";
		configInput(CLOCK_INPUT, "Clock")->description = "Every rising edge moves one step (e.g. JX-CLK).";
		configInput(RESET_INPUT, "Reset")->description = "Back to step 1.";
		configOutput(OUT_OUTPUT, "Out")->description = "4 -> 1: the input of the current step.";
	}

	/** The current step is saved, so a reloaded patch goes on where it was */
	json_t* dataToJson() override {
		json_t* const rootJ = json_object();
		json_object_set_new(rootJ, "step", json_integer(stepper.step));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* const stepJ = json_object_get(rootJ, "step"))
			stepper.restore((int) json_integer_value(stepJ));
	}

	void process(const ProcessArgs& args) override {
		const int length = (int) params[LENGTH_PARAM].getValue();
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			stepper.reset();
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f))
			stepper.clock(length);
		const int step = stepper.current(length);

		const int ch = std::max(1, inputs[IN_INPUT + step].getChannels());
		outputs[OUT_OUTPUT].setChannels(ch);
		for (int c = 0; c < ch; ++c)
			outputs[OUT_OUTPUT].setVoltage(inputs[IN_INPUT + step].getVoltage(c), c);

		const int sc = std::max(1, inputs[SINGLE_INPUT].getChannels());
		for (int i = 0; i < 4; ++i) {
			outputs[SPLIT_OUTPUT + i].setChannels(sc);
			for (int c = 0; c < sc; ++c)
				outputs[SPLIT_OUTPUT + i].setVoltage(i == step ? inputs[SINGLE_INPUT].getVoltage(c) : 0.f, c);
			lights[STEP_LIGHT + i].setBrightness(i == step ? 1.f : i < length ? 0.08f : 0.f);
		}
	}
};

struct SwWidget : JXMPModuleWidget {
	SwWidget(Sw* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-SW", nvgRGB(0xd6, 0xc0, 0x8a), 8);
		styled();
		section(3.f, 20.5f, 34.64f, 48.f, "STEP");
		for (int i = 0; i < 4; ++i) {
			label(math::Vec(13.f, 30.f + i * 10.f), string::f("%d", i + 1).c_str(), 8.f);
			addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(math::Vec(26.f, 30.f + i * 10.f)), module, Sw::STEP_LIGHT + i));
		}
		section(3.f, 72.f, 34.64f, 45.f, "LENGTH");
		knob(math::Vec(20.32f, 90.f), Sw::LENGTH_PARAM, 12.f, st::CAP_BLACK, 2.f, 4.f, {{2.f, "2"}, {3.f, "3"}, {4.f, "4"}});
		label(math::Vec(20.32f, 104.f), "4 > 1  ·  1 > 4", 6.f);

		const float xs[2] = {11.f, 29.64f};
		for (int i = 0; i < 4; ++i) {
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 30.f + i * 15.f)), module, Sw::IN_INPUT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 30.f + i * 15.f)), module, Sw::SPLIT_OUTPUT + i));
		}
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 92.f)), module, Sw::OUT_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 92.f)), module, Sw::SINGLE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 110.f)), module, Sw::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 110.f)), module, Sw::RESET_INPUT));
	}
};

} // namespace

Model* modelJXMPSwitch = createModel<Sw, SwWidget>("Switch");
