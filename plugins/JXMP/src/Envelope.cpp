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
		configInput(GATE_INPUT, "Gate")->description = "Gate: the envelope runs while this is above 1 V. Polyphonic: one envelope per channel. In LOOP mode an unpatched gate means free-running.";
		configInput(RETRIG_INPUT, "Retrig")->description = "Retrigger: a trigger restarts the attack without waiting for the gate to close.";
		configInput(ATTACK_INPUT, "A CV")->description = "Attack time CV, scaled by the A CV AMOUNT knob (10 V = the full knob range).";
		configInput(DECAY_INPUT, "D CV")->description = "Decay time CV, scaled by the D CV AMOUNT knob (10 V = the full knob range).";
		configInput(SUSTAIN_INPUT, "S CV")->description = "Sustain level CV, scaled by the S CV AMOUNT knob (10 V = the full knob range).";
		configInput(RELEASE_INPUT, "R CV")->description = "Release time CV, scaled by the R CV AMOUNT knob (10 V = the full knob range).";
		configOutput(ENV_OUTPUT, "Env")->description = "Envelope: 0 to 10 V. Patch it to a VCA level (JX-AMP CV) or a filter cutoff (JX-FLT ENV).";
		configOutput(INV_OUTPUT, "Inv")->description = "Inverted envelope: 10 V minus the envelope, rests at 10 V and dips while a note plays.";
		configOutput(EOC_OUTPUT, "EOC")->description = "End of cycle: a 1 ms trigger when the envelope finishes (end of release, end of decay in AD, every cycle in LOOP).";
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
		setup(module, "JX-ENV", nvgRGB(0xf2, 0xc2, 0x30), 14);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const float nameSize = 7.5f;
		const float xs[4] = {12.f, 27.f, 42.f, 57.f};
		const char* names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
		// Times follow MIN_TIME * LAMBDA_BASE^param: 1 ms, 100 ms in the middle, 10 s
		const std::vector<st::Mark> timeMarks = {{0.f, "1ms"}, {0.25f, ""}, {0.5f, "100ms"}, {0.75f, ""}, {1.f, "10s"}};
		const std::vector<st::Mark> levelMarks =
			st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); });

		// ENVELOPE
		section(4.f, 20.5f, 63.12f, 31.f, "ENVELOPE");
		for (int i = 0; i < 4; ++i) {
			label(math::Vec(xs[i], 25.5f), names[i], 6.8f);
			knob(math::Vec(xs[i], 40.f), Adsr::ATTACK_PARAM + i, 9.f, st::CAP_WHITE, 0.f, 1.f, i == 2 ? levelMarks : timeMarks);
			addChild(createLightCentered<TinyLight<YellowLight>>(mm2px(math::Vec(xs[i] + 6.2f, 25.5f)), module, Adsr::ATTACK_LIGHT + i));
		}

		// CV AMOUNT
		section(4.f, 55.f, 63.12f, 24.f, "CV AMOUNT");
		const char* cvNames[4] = {"A CV", "D CV", "S CV", "R CV"};
		for (int i = 0; i < 4; ++i) {
			label(math::Vec(xs[i], 60.f), cvNames[i], 6.8f);
			knob(math::Vec(xs[i], 70.5f), Adsr::ATTACK_CV_PARAM + i, 7.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());
		}

		// SHAPE · MODE
		section(4.f, 83.f, 63.12f, 34.5f, "SHAPE · MODE");
		label(math::Vec(15.f, 88.f), "CURVE", nameSize);
		knob(math::Vec(15.f, 103.f), Adsr::CURVE_PARAM, 10.f, st::CAP_MAIN, -1.f, 1.f,
		     {{-1.f, "LIN"}, {-0.5f, ""}, {0.f, "0"}, {0.5f, ""}, {1.f, "EXP"}});
		icon(st::ICON_ENV, math::Vec(24.f, 88.f), 4.f);
		label(math::Vec(38.f, 88.f), "MODE", nameSize);
		toggle(math::Vec(38.f, 103.f), Adsr::MODE_PARAM, 3, {"LOOP", "AD", "ADSR"});
		label(math::Vec(57.f, 88.f), "PUSH", nameSize);
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(math::Vec(57.f, 103.f)), module, Adsr::PUSH_PARAM, Adsr::PUSH_LIGHT));

		// Back: sockets
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(15.f, 30.f)), module, Adsr::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(15.f, 45.f)), module, Adsr::RETRIG_INPUT));
		for (int i = 0; i < 4; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(33.f, 30.f + i * 15.f)), module, Adsr::ATTACK_INPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(55.f, 30.f)), module, Adsr::ENV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(55.f, 45.f)), module, Adsr::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(55.f, 60.f)), module, Adsr::EOC_OUTPUT));
	}
};

} // namespace


Model* modelJXMPADSR = createModel<Adsr, AdsrWidget>("ADSR");
