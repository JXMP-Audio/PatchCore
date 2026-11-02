/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CHR: chorus / ensemble. CHORUS: two voices, one per side; ENSEMBLE: three voices per side with a little
 * fast vibrato, as string machines did. RATE, DEPTH, MIX, CV for rate and depth, stereo in and out.
 *
 */

#include "JXMPWidget.hpp"
#include "dsp/Chorus.hpp"

namespace {

namespace ch = jxmp::chorus;

struct Chr : engine::Module {
	enum ParamIds { MODE_PARAM, RATE_PARAM, DEPTH_PARAM, MIX_PARAM, NUM_PARAMS };
	enum InputIds { IN_L_INPUT, IN_R_INPUT, RATE_CV_INPUT, DEPTH_CV_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { RATE_LIGHT, NUM_LIGHTS };

	ch::Chorus chorus;

	Chr() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Toggle: lever up = the highest value
		configSwitch(MODE_PARAM, 0.f, 1.f, 1.f, "Mode", {"Ensemble", "Chorus"})
		    ->description = "CHORUS: a gentle doubling, one voice per side. ENSEMBLE: three voices per side with "
		                    "a little vibrato, the lush string-machine sound.";
		configParam(RATE_PARAM, 0.f, 1.f, 0.35f, "Rate", " Hz", 100.f, 0.05f)->description = "How fast the voices sway, 0.05-5 Hz.";
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth", "%", 0.f, 100.f)->description = "How far they sway.";
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f)->description = "Dry to wet; 50 % is the classic chorus.";
		configInput(IN_L_INPUT, "In L")->description = "Left (or mono) input.";
		configInput(IN_R_INPUT, "In R")->description = "Right input; unpatched, the left input is used.";
		configInput(RATE_CV_INPUT, "Rate CV")->description = "Adds to RATE, 1 V per 10 % of the knob.";
		configInput(DEPTH_CV_INPUT, "Depth CV")->description = "0..10 V adds to DEPTH.";
		configOutput(OUT_L_OUTPUT, "Out L")->description = "Left output: dry and chorus mixed by MIX.";
		configOutput(OUT_R_OUTPUT, "Out R")->description = "Right output: dry and chorus mixed by MIX (the voices sway against the left side).";
		configLight(RATE_LIGHT, "Rate");
	}

	/** Rack sends this when the module is added and on every rate change: the buffers are sized here, not on the
	audio thread (process() only checks, the rate is already set) */
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		chorus.setSampleRate(e.sampleRate);
	}

	void process(const ProcessArgs& args) override {
		chorus.setSampleRate(args.sampleRate);
		const float inL = inputs[IN_L_INPUT].getVoltageSum();
		const float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
		const float rateKnob = math::clamp(params[RATE_PARAM].getValue() + inputs[RATE_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const float rate = 0.05f * std::pow(100.f, rateKnob);
		const float depth = math::clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float wl, wr;
		chorus.process(inL, inR, rate, depth, params[MODE_PARAM].getValue() < 0.5f, &wl, &wr);
		float l, r;
		ch::mix(inL, wl, params[MIX_PARAM].getValue(), &l);
		ch::mix(inR, wr, params[MIX_PARAM].getValue(), &r);
		outputs[OUT_L_OUTPUT].setVoltage(l);
		outputs[OUT_R_OUTPUT].setVoltage(r);
		lights[RATE_LIGHT].setBrightness(0.5f + 0.5f * std::sin(2.f * float(M_PI) * chorus.phase));
	}
};


struct ChrWidget : JXMPModuleWidget {
	ChrWidget(Chr* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-CHR", nvgRGB(0xb9, 0xa2, 0xee), 10);
		styled();
		const float w = 50.8f;
		const std::vector<st::Mark> pct = st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); });

		section(3.f, 20.5f, w - 6.f, 24.f, "MODE · RATE");
		toggle(math::Vec(12.f, 33.f), Chr::MODE_PARAM, 2, {"CHORUS", "ENSMBL"});
		knob(math::Vec(35.f, 34.f), Chr::RATE_PARAM, 12.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, ".05"}, {0.25f, ""}, {0.5f, ".5"}, {0.75f, ""}, {1.f, "5"}});
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(44.f, 24.5f)), module, Chr::RATE_LIGHT));
		label(math::Vec(35.f, 46.5f), "RATE  HZ", 6.f);

		section(3.f, 49.f, w - 6.f, 32.f, "DEPTH");
		knob(math::Vec(w / 2, 64.f), Chr::DEPTH_PARAM, 14.f, st::CAP_WHITE, 0.f, 1.f, pct);

		section(3.f, 85.f, w - 6.f, 32.f, "MIX");
		knob(math::Vec(w / 2, 100.f), Chr::MIX_PARAM, 14.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, "DRY"}, {0.25f, ""}, {0.5f, "50"}, {0.75f, ""}, {1.f, "WET"}});

		// Back
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 34.f)), module, Chr::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(37.8f, 34.f)), module, Chr::IN_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 56.f)), module, Chr::RATE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(37.8f, 56.f)), module, Chr::DEPTH_CV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 80.f)), module, Chr::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(37.8f, 80.f)), module, Chr::OUT_R_OUTPUT));
	}
};

} // namespace


Model* modelJXMPChorus = createModel<Chr, ChrWidget>("Chorus");
