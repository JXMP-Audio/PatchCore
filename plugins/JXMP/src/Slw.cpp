/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-SLW: slew limiter / glide with separate rise and fall times, linear or exponential. Polyphonic.
 */

#include "JXMPWidget.hpp"
#include "dsp/Slew.hpp"

namespace {

struct Slw : engine::Module {
	enum ParamIds { RISE_PARAM, FALL_PARAM, SHAPE_PARAM, NUM_PARAMS };
	enum InputIds { IN_INPUT, RISE_INPUT, FALL_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, NUM_OUTPUTS };
	enum LightIds { RISE_LIGHT, FALL_LIGHT, NUM_LIGHTS };

	jxmp::Slew slew[16];

	Slw() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RISE_PARAM, 0.f, 1.f, 0.f, "Rise time", " ms", 10000.f, 1.f);
		configParam(FALL_PARAM, 0.f, 1.f, 0.f, "Fall time", " ms", 10000.f, 1.f);
		configSwitch(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape", {"Linear", "Exponential"});
		configInput(IN_INPUT, "In");
		configInput(RISE_INPUT, "Rise CV");
		configInput(FALL_INPUT, "Fall CV");
		configOutput(OUT_OUTPUT, "Out");
		configBypass(IN_INPUT, OUT_OUTPUT);
	}

	/** Knob 0 = no slew, else 1 ms · 10^(4·knob) up to 10 s. CV adds 10 % of the range per volt. */
	static float seconds(float knob) {
		knob = math::clamp(knob, 0.f, 1.f);
		return knob <= 0.f ? 0.f : 0.001f * std::pow(10000.f, knob);
	}

	void process(const ProcessArgs& args) override {
		const bool exponential = params[SHAPE_PARAM].getValue() > 0.5f;
		const int channels = std::max(1, inputs[IN_INPUT].getChannels());
		bool rising = false, falling = false;
		for (int c = 0; c < channels; ++c) {
			const float rise = seconds(params[RISE_PARAM].getValue() + inputs[RISE_INPUT].getPolyVoltage(c) / 10.f);
			const float fall = seconds(params[FALL_PARAM].getValue() + inputs[FALL_INPUT].getPolyVoltage(c) / 10.f);
			const float in = inputs[IN_INPUT].getVoltage(c);
			const float before = slew[c].value;
			const float v = slew[c].process(in, rise, fall, exponential, args.sampleTime);
			rising = rising || v > before + 1e-6f;
			falling = falling || v < before - 1e-6f;
			outputs[OUT_OUTPUT].setVoltage(v, c);
		}
		outputs[OUT_OUTPUT].setChannels(channels);
		lights[RISE_LIGHT].setBrightnessSmooth(rising, args.sampleTime);
		lights[FALL_LIGHT].setBrightnessSmooth(falling, args.sampleTime);
	}
};


struct SlwWidget : JXMPModuleWidget {
	SlwWidget(Slw* const module) {
		setup(module, "JX-SLW", nvgRGB(0x8f, 0xae, 0x3a), 8);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const std::vector<st::Mark> times = {{0.f, "0"}, {0.25f, ""}, {0.5f, "100ms"}, {0.75f, ""}, {1.f, "10s"}};

		section(3.f, 20.5f, 34.64f, 66.f, "SLEW");
		label(math::Vec(20.32f, 25.5f), "RISE", 7.5f);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(27.f, 25.5f)), module, Slw::RISE_LIGHT));
		knob(math::Vec(20.32f, 39.f), Slw::RISE_PARAM, 13.f, st::CAP_MAIN, 0.f, 1.f, times);
		label(math::Vec(20.32f, 56.5f), "FALL", 7.5f);
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(27.f, 56.5f)), module, Slw::FALL_LIGHT));
		knob(math::Vec(20.32f, 70.f), Slw::FALL_PARAM, 13.f, st::CAP_MAIN, 0.f, 1.f, times);

		section(3.f, 90.5f, 34.64f, 27.f, "SHAPE");
		label(math::Vec(20.32f, 95.5f), "CURVE", 7.5f);
		toggle(math::Vec(20.32f, 107.f), Slw::SHAPE_PARAM, 2, {"EXP", "LIN"});

		// Back: sockets
		for (int i = 0; i < Slw::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(11.f, 32.f + i * 16.f)), module, Slw::IN_INPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(29.f, 32.f)), module, Slw::OUT_OUTPUT));
	}
};

} // namespace


Model* modelJXMPSlew = createModel<Slw, SlwWidget>("Slew");
