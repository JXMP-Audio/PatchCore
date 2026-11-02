/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-REV: stereo reverb — PLATE / HALL / ROOM, SIZE, DECAY (RT60), PREDELAY, TONE, MIX, CV for MIX and DECAY.
 * Meant for a JX-CONSOLE send / return. Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Reverb.hpp"

namespace {

namespace rv = jxmp::reverb;

/** DECAY knob 0..1 to seconds, 0.2 … 12 s, exponential */
inline float rt60(float k) {
	return 0.2f * std::pow(60.f, math::clamp(k, 0.f, 1.f));
}

/** DECAY shows seconds in its tooltip */
struct DecayQuantity : engine::ParamQuantity {
	float getDisplayValue() override {
		return rt60(getValue());
	}
	void setDisplayValue(float s) override {
		setValue(std::log(std::max(s, 0.2f) / 0.2f) / std::log(60.f));
	}
};

struct Rev : engine::Module {
	enum ParamIds { ALGO_PARAM, SIZE_PARAM, DECAY_PARAM, PREDELAY_PARAM, TONE_PARAM, MIX_PARAM, NUM_PARAMS };
	enum InputIds { IN_L_INPUT, IN_R_INPUT, MIX_CV_INPUT, DECAY_CV_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	rv::Fdn fdn;

	Rev() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// Toggle: lever up = the highest value
		configSwitch(ALGO_PARAM, 0.f, 2.f, 1.f, "Algorithm", {"Room", "Hall", "Plate"})
		    ->description = "PLATE: dense and bright. HALL: big and smooth. ROOM: small and close.";
		configParam(SIZE_PARAM, 0.25f, 2.f, 1.f, "Size", "%", 0.f, 100.f)->description = "How big the space is.";
		configParam<DecayQuantity>(DECAY_PARAM, 0.f, 1.f, 0.4f, "Decay", " s")->description = "How long the tail lasts (0.2 to 12 s).";
		configParam(PREDELAY_PARAM, 0.f, 0.2f, 0.02f, "Predelay", " ms", 0.f, 1000.f)
		    ->description = "A gap before the reverb starts: keeps the dry sound clear.";
		configParam(TONE_PARAM, 0.f, 1.f, 0.6f, "Tone", "%", 0.f, 100.f)->description = "Darker to brighter tail.";
		configParam(MIX_PARAM, 0.f, 1.f, 0.3f, "Mix", "%", 0.f, 100.f)
		    ->description = "Dry to wet. In a console send / return use 100 % (wet only).";
		configInput(IN_L_INPUT, "In L")->description = "Left (or mono) input.";
		configInput(IN_R_INPUT, "In R")->description = "Right input; unpatched, the left input is used.";
		configInput(MIX_CV_INPUT, "Mix CV")->description = "0..10 V adds to MIX.";
		configInput(DECAY_CV_INPUT, "Decay CV")->description = "0..10 V adds to DECAY.";
		configOutput(OUT_L_OUTPUT, "Out L")->description = "Left output: the dry input and the reverb (soft-limited at ±10 V), mixed by MIX.";
		configOutput(OUT_R_OUTPUT, "Out R")->description = "Right output: the dry input and the reverb (soft-limited at ±10 V), mixed by MIX.";
	}

	/** Rack sends this when the module is added and on every rate change: the buffers are sized here, not on the
	audio thread (process() only checks, the rate is already set) */
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		fdn.setSampleRate(e.sampleRate);
	}

	void process(const ProcessArgs& args) override {
		fdn.setSampleRate(args.sampleRate);
		const float inL = inputs[IN_L_INPUT].getVoltageSum();
		const float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
		const float decay = params[DECAY_PARAM].getValue() + inputs[DECAY_CV_INPUT].getVoltage() / 10.f;
		const float mix = math::clamp(params[MIX_PARAM].getValue() + inputs[MIX_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const int algo = (int) std::round(params[ALGO_PARAM].getValue());  // 2 plate, 1 hall, 0 room
		const rv::Algo a = algo == 2 ? rv::PLATE : algo == 1 ? rv::HALL : rv::ROOM;
		float wl, wr;
		fdn.process(inL, inR, a, params[SIZE_PARAM].getValue(), rt60(decay), params[PREDELAY_PARAM].getValue(),
		            params[TONE_PARAM].getValue(), &wl, &wr);
		// Equal-power dry/wet
		const float dry = std::cos(mix * float(M_PI) / 2), wet = std::sin(mix * float(M_PI) / 2);
		outputs[OUT_L_OUTPUT].setVoltage(inL * dry + wl * wet);
		outputs[OUT_R_OUTPUT].setVoltage(inR * dry + wr * wet);
	}
};

struct RevWidget : JXMPModuleWidget {
	RevWidget(Rev* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-REV", nvgRGB(0x8f, 0xc8, 0xf0), 12);
		styled();
		section(3.f, 20.5f, 54.96f, 30.f, "SPACE");
		toggle(math::Vec(15.f, 36.f), Rev::ALGO_PARAM, 3, {"PLATE", "HALL", "ROOM"});
		label(math::Vec(38.f, 25.f), "SIZE", 6.5f);
		knob(math::Vec(38.f, 37.f), Rev::SIZE_PARAM, 13.f, st::CAP_MAIN, 0.25f, 2.f,
		     st::marks(0.25f, 2.f, 0.25f, 7, [](float v) { return v < 1.f ? std::string("S") : std::string("L"); }));

		section(3.f, 54.f, 54.96f, 38.f, "TAIL");
		const std::vector<st::Mark> pct = st::marks(0.f, 1.f, 0.1f, 10, [](float v) { return v < 0.5f ? std::string("-") : std::string("+"); });
		label(math::Vec(16.f, 59.f), "DECAY", 6.5f);
		knob(math::Vec(16.f, 70.f), Rev::DECAY_PARAM, 11.f, st::CAP_WHITE, 0.f, 1.f,
		     {{0.f, ".2s"}, {0.25f, ""}, {0.5f, ""}, {0.75f, ""}, {1.f, "12s"}});
		label(math::Vec(44.f, 59.f), "PREDELAY", 6.5f);
		knob(math::Vec(44.f, 70.f), Rev::PREDELAY_PARAM, 9.f, st::CAP_GREY, 0.f, 0.2f,
		     {{0.f, "0"}, {0.05f, ""}, {0.1f, ""}, {0.15f, ""}, {0.2f, "200"}});
		label(math::Vec(16.f, 81.5f), "TONE", 6.5f);
		knob(math::Vec(30.5f, 84.f), Rev::TONE_PARAM, 8.f, st::CAP_GREY, 0.f, 1.f, pct);

		section(3.f, 95.f, 54.96f, 22.f, "MIX");
		knob(math::Vec(30.48f, 106.5f), Rev::MIX_PARAM, 11.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, "DRY"}, {0.25f, ""}, {0.5f, ""}, {0.75f, ""}, {1.f, "WET"}});

		// Back: stereo in, CVs, stereo out
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(16.f, 34.f)), module, Rev::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(45.f, 34.f)), module, Rev::IN_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(16.f, 56.f)), module, Rev::MIX_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(45.f, 56.f)), module, Rev::DECAY_CV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(16.f, 80.f)), module, Rev::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(45.f, 80.f)), module, Rev::OUT_R_OUTPUT));
	}
};

} // namespace


Model* modelJXMPReverb = createModel<Rev, RevWidget>("Reverb");
