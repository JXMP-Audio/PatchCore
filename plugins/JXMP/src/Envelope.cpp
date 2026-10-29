/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-ENV: ADSR with curve, AD and loop modes, inverted output and end-of-cycle trigger.
 * Time ranges follow VCV Fundamental's ADSR (GPL-3.0-or-later).
 */

#include "JXMPWidget.hpp"
#include "dsp/Envelope.hpp"

namespace {

struct Adsr : engine::Module {
	enum ParamIds {
		ATTACK_PARAM, DECAY_PARAM, SUSTAIN_PARAM, RELEASE_PARAM,
		ATTACK_CV_PARAM, DECAY_CV_PARAM, SUSTAIN_CV_PARAM, RELEASE_CV_PARAM,
		CURVE_PARAM, MODE_PARAM, PUSH_PARAM,
		NUM_PARAMS
	};
	enum InputIds { GATE_INPUT, RETRIG_INPUT, ATTACK_INPUT, DECAY_INPUT, SUSTAIN_INPUT, RELEASE_INPUT, NUM_INPUTS };
	enum OutputIds { ENV_OUTPUT, INV_OUTPUT, EOC_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ATTACK_LIGHT, DECAY_LIGHT, SUSTAIN_LIGHT, RELEASE_LIGHT, PUSH_LIGHT, NUM_LIGHTS };

	static constexpr float MIN_TIME = 1e-3f;
	static constexpr float MAX_TIME = 10.f;
	static constexpr float LAMBDA_BASE = MAX_TIME / MIN_TIME;

	jxmp::Envelope env[16];
	dsp::SchmittTrigger retrig[16];
	dsp::PulseGenerator eoc[16];
	dsp::ClockDivider lightDivider;

	Adsr() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.5f, "Attack", " ms", LAMBDA_BASE, MIN_TIME * 1000);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " ms", LAMBDA_BASE, MIN_TIME * 1000);
		configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.5f, "Sustain", "%", 0, 100);
		configParam(RELEASE_PARAM, 0.f, 1.f, 0.5f, "Release", " ms", LAMBDA_BASE, MIN_TIME * 1000);
		configParam(ATTACK_CV_PARAM, -1.f, 1.f, 0.f, "Attack CV", "%", 0, 100);
		configParam(DECAY_CV_PARAM, -1.f, 1.f, 0.f, "Decay CV", "%", 0, 100);
		configParam(SUSTAIN_CV_PARAM, -1.f, 1.f, 0.f, "Sustain CV", "%", 0, 100);
		configParam(RELEASE_CV_PARAM, -1.f, 1.f, 0.f, "Release CV", "%", 0, 100);
		configParam(CURVE_PARAM, -1.f, 1.f, 0.f, "Curve", "%", 0, 100);
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"ADSR", "AD", "Loop"});
		configButton(PUSH_PARAM, "Push");
		configInput(GATE_INPUT, "Gate");
		configInput(RETRIG_INPUT, "Retrig");
		configInput(ATTACK_INPUT, "A CV");
		configInput(DECAY_INPUT, "D CV");
		configInput(SUSTAIN_INPUT, "S CV");
		configInput(RELEASE_INPUT, "R CV");
		configOutput(ENV_OUTPUT, "Env");
		configOutput(INV_OUTPUT, "Inv");
		configOutput(EOC_OUTPUT, "EOC");
		lightDivider.setDivision(128);
	}

	float knob(int param, int cvParam, int input, int c) {
		const float v = params[param].getValue() + inputs[input].getPolyVoltage(c) / 10.f * params[cvParam].getValue();
		return math::clamp(v, 0.f, 1.f);
	}

	static float seconds(float knob01) {
		return MIN_TIME * std::pow(LAMBDA_BASE, knob01);
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max(1, inputs[GATE_INPUT].getChannels());
		const bool push = params[PUSH_PARAM].getValue() > 0.f;
		const float shape = params[CURVE_PARAM].getValue();
		const jxmp::Envelope::Mode mode = (jxmp::Envelope::Mode) (int) std::round(params[MODE_PARAM].getValue());
		const bool gateConnected = inputs[GATE_INPUT].isConnected() || push;

		for (int c = 0; c < channels; ++c) {
			const bool gate = push || inputs[GATE_INPUT].getVoltage(c) >= 1.f;
			const bool trig = retrig[c].process(inputs[RETRIG_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			const float v = env[c].process(args.sampleTime, gate, trig, gateConnected,
			                               seconds(knob(ATTACK_PARAM, ATTACK_CV_PARAM, ATTACK_INPUT, c)),
			                               seconds(knob(DECAY_PARAM, DECAY_CV_PARAM, DECAY_INPUT, c)),
			                               knob(SUSTAIN_PARAM, SUSTAIN_CV_PARAM, SUSTAIN_INPUT, c),
			                               seconds(knob(RELEASE_PARAM, RELEASE_CV_PARAM, RELEASE_INPUT, c)), shape, mode);
			if (env[c].eoc)
				eoc[c].trigger(1e-3f);
			outputs[ENV_OUTPUT].setVoltage(10.f * v, c);
			outputs[INV_OUTPUT].setVoltage(10.f - 10.f * v, c);
			outputs[EOC_OUTPUT].setVoltage(eoc[c].process(args.sampleTime) ? 10.f : 0.f, c);
		}
		outputs[ENV_OUTPUT].setChannels(channels);
		outputs[INV_OUTPUT].setChannels(channels);
		outputs[EOC_OUTPUT].setChannels(channels);

		if (lightDivider.process()) {
			const jxmp::Envelope::Stage st = env[0].stage;
			lights[ATTACK_LIGHT].setBrightness(st == jxmp::Envelope::ATTACK);
			lights[DECAY_LIGHT].setBrightness(st == jxmp::Envelope::DECAY);
			lights[SUSTAIN_LIGHT].setBrightness(st == jxmp::Envelope::SUSTAIN);
			lights[RELEASE_LIGHT].setBrightness(st == jxmp::Envelope::RELEASE);
			lights[PUSH_LIGHT].setBrightness(push);
		}
	}
};


struct AdsrWidget : JXMPModuleWidget {
	AdsrWidget(Adsr* const module) {
		setup(module, "JX-ENV", nvgRGB(0xf2, 0xc2, 0x30), 10);

		// Front: controls
		const float xs[4] = {7.5f, 19.f, 31.5f, 43.3f};
		const char* names[4] = {"A", "D", "S", "R"};
		for (int i = 0; i < 4; ++i) {
			label(math::Vec(xs[i], 24.f), names[i], 9.f);
			addParam(createParamCentered<RoundBlackKnob>(mm2px(math::Vec(xs[i], 33.f)), module, Adsr::ATTACK_PARAM + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(xs[i], 42.f)), module, Adsr::ATTACK_LIGHT + i));
			addParam(createParamCentered<Trimpot>(mm2px(math::Vec(xs[i], 52.f)), module, Adsr::ATTACK_CV_PARAM + i));
		}
		label(math::Vec(25.4f, 58.5f), "CV AMOUNT", 6.f);

		label(math::Vec(13.f, 70.f), "CURVE");
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(math::Vec(13.f, 82.f)), module, Adsr::CURVE_PARAM));
		label(math::Vec(36.f, 70.f), "ADSR · AD · LOOP", 6.f);
		addParam(createParamCentered<CKSSThreeHorizontal>(mm2px(math::Vec(36.f, 78.f)), module, Adsr::MODE_PARAM));
		label(math::Vec(36.f, 90.f), "PUSH");
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(math::Vec(36.f, 99.f)), module, Adsr::PUSH_PARAM, Adsr::PUSH_LIGHT));

		// Back: sockets
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(9.f, 32.f)), module, Adsr::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(9.f, 48.f)), module, Adsr::RETRIG_INPUT));
		for (int i = 0; i < 4; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(25.4f, 32.f + i * 16.f)), module, Adsr::ATTACK_INPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(41.8f, 32.f)), module, Adsr::ENV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(41.8f, 48.f)), module, Adsr::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(41.8f, 64.f)), module, Adsr::EOC_OUTPUT));
	}
};

} // namespace


Model* modelJXMPADSR = createModel<Adsr, AdsrWidget>("ADSR");
