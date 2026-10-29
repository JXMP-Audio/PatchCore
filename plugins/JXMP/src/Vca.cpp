/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-AMP: two-channel VCA. Gain law based on VCV Fundamental's VCA (GPL-3.0-or-later).
 */

#include "JXMPWidget.hpp"
#include "dsp/Vca.hpp"

namespace {

struct Vca : engine::Module {
	enum ParamIds { LEVEL1_PARAM, LEVEL2_PARAM, RESPONSE1_PARAM, RESPONSE2_PARAM, NUM_PARAMS };
	enum InputIds { IN1_INPUT, CV1_INPUT, IN2_INPUT, CV2_INPUT, NUM_INPUTS };
	enum OutputIds { OUT1_OUTPUT, OUT2_OUTPUT, NUM_OUTPUTS };
	enum LightIds { LEVEL1_LIGHT, LEVEL2_LIGHT, NUM_LIGHTS };

	Vca() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(LEVEL1_PARAM, 0.f, 1.f, 1.f, "Channel 1 level", "%", 0.f, 100.f);
		configParam(LEVEL2_PARAM, 0.f, 1.f, 1.f, "Channel 2 level", "%", 0.f, 100.f);
		configSwitch(RESPONSE1_PARAM, 0.f, 1.f, 1.f, "Channel 1 response", {"Linear", "Exponential"});
		configSwitch(RESPONSE2_PARAM, 0.f, 1.f, 1.f, "Channel 2 response", {"Linear", "Exponential"});
		configInput(IN1_INPUT, "In 1");
		configInput(CV1_INPUT, "CV 1");
		configInput(IN2_INPUT, "In 2 (normalled to In 1)");
		configInput(CV2_INPUT, "CV 2");
		configOutput(OUT1_OUTPUT, "Out 1");
		configOutput(OUT2_OUTPUT, "Out 2");
		configBypass(IN1_INPUT, OUT1_OUTPUT);
		configBypass(IN2_INPUT, OUT2_OUTPUT);
	}

	/** Returns the peak level of the channel for its light. */
	float processChannel(engine::Input& in, engine::Input& cv, const float level, const bool exponential, engine::Output& out) {
		const int channels = std::max(1, in.getChannels());
		float peak = 0.f;
		for (int c = 0; c < channels; ++c) {
			const float gain = level * (cv.isConnected() ? jxmp::vcaGain(cv.getPolyVoltage(c) / 10.f, exponential) : 1.f);
			const float v = in.getVoltage(c) * gain;
			out.setVoltage(v, c);
			peak = std::max(peak, std::fabs(v));
		}
		out.setChannels(channels);
		return peak;
	}

	void process(const ProcessArgs& args) override {
		const float p1 = processChannel(inputs[IN1_INPUT], inputs[CV1_INPUT], params[LEVEL1_PARAM].getValue(),
		                                params[RESPONSE1_PARAM].getValue() > 0.5f, outputs[OUT1_OUTPUT]);
		// Channel 2 input is normalled from channel 1's
		engine::Input& in2 = inputs[IN2_INPUT].isConnected() ? inputs[IN2_INPUT] : inputs[IN1_INPUT];
		const float p2 = processChannel(in2, inputs[CV2_INPUT], params[LEVEL2_PARAM].getValue(),
		                                params[RESPONSE2_PARAM].getValue() > 0.5f, outputs[OUT2_OUTPUT]);
		lights[LEVEL1_LIGHT].setBrightnessSmooth(p1 / 10.f, args.sampleTime);
		lights[LEVEL2_LIGHT].setBrightnessSmooth(p2 / 10.f, args.sampleTime);
	}
};


struct VcaWidget : JXMPModuleWidget {
	VcaWidget(Vca* const module) {
		setup(module, "JX-AMP", nvgRGB(0x3c, 0xb4, 0x6e), 6);

		// Front: controls
		for (int ch = 0; ch < 2; ++ch) {
			const float y = ch == 0 ? 0.f : 50.f;
			addParam(createParamCentered<RoundBigBlackKnob>(mm2px(math::Vec(15.24f, 34.f + y)), module, Vca::LEVEL1_PARAM + ch));
			addParam(createParamCentered<CKSS>(mm2px(math::Vec(11.f, 52.f + y)), module, Vca::RESPONSE1_PARAM + ch));
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(22.f, 52.f + y)), module, Vca::LEVEL1_LIGHT + ch));
			label(math::Vec(15.24f, 23.5f + y), ch == 0 ? "LEVEL 1" : "LEVEL 2");
			label(math::Vec(11.f, 59.f + y), "LIN · EXP", 6.f);
		}

		// Back: sockets
		for (int ch = 0; ch < 2; ++ch) {
			const float y = ch == 0 ? 0.f : 50.f;
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(8.f, 32.f + y)), module, Vca::IN1_INPUT + ch * 2));
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(8.f, 47.f + y)), module, Vca::CV1_INPUT + ch * 2));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(22.5f, 40.f + y)), module, Vca::OUT1_OUTPUT + ch));
		}
	}
};

} // namespace


Model* modelJXMPVCA = createModel<Vca, VcaWidget>("VCA");
