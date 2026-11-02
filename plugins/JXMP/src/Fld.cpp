/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-FLD: wavefolder. FOLD amplifies the input up to ten times and folds it back at ±5 V (more harmonics the
 * further you turn), SYMMETRY shifts it for uneven folds. CV for both. Polyphonic.
 * Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Shapers.hpp"

namespace {

struct Fld : engine::Module {
	enum ParamIds { FOLD_PARAM, SYM_PARAM, FOLD_CV_PARAM, NUM_PARAMS };
	enum InputIds { IN_INPUT, FOLD_CV_INPUT, SYM_CV_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	Fld() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(FOLD_PARAM, 0.f, 1.f, 0.3f, "Fold", "%", 0.f, 100.f)->description = "How many times the wave is folded: brighter and more metallic.";
		configParam(SYM_PARAM, -1.f, 1.f, 0.f, "Symmetry", "%", 0.f, 100.f)->description = "Shifts the wave before folding: uneven, hollower folds.";
		configParam(FOLD_CV_PARAM, -1.f, 1.f, 0.f, "Fold CV amount", "%", 0.f, 100.f)->description = "How much FOLD CV moves FOLD (± for inverted).";
		configInput(IN_INPUT, "In")->description = "Audio to fold, e.g. a sine or triangle from JX-OSC (±5 V).";
		configInput(FOLD_CV_INPUT, "Fold CV")->description = "Modulates FOLD (an envelope or LFO), scaled by the CV knob.";
		configInput(SYM_CV_INPUT, "Sym CV")->description = "0..±5 V shifts SYMMETRY.";
		configOutput(OUT_OUTPUT, "Out")->description = "The folded wave, within ±5 V.";
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max(1, inputs[IN_INPUT].getChannels());
		outputs[OUT_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; ++c) {
			const float amount = params[FOLD_PARAM].getValue() + params[FOLD_CV_PARAM].getValue() * inputs[FOLD_CV_INPUT].getPolyVoltage(c) / 10.f;
			const float sym = params[SYM_PARAM].getValue() + inputs[SYM_CV_INPUT].getPolyVoltage(c) / 5.f;
			outputs[OUT_OUTPUT].setVoltage(jxmp::shapers::fold(inputs[IN_INPUT].getVoltage(c), amount, math::clamp(sym, -1.f, 1.f)), c);
		}
	}
};

struct FldWidget : JXMPModuleWidget {
	FldWidget(Fld* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-FLD", nvgRGB(0xb6, 0xe0, 0x4a), 6);
		styled();
		const float x = 15.24f;
		section(3.f, 20.5f, 24.48f, 32.f, "FOLD");
		knob(math::Vec(x, 36.f), Fld::FOLD_PARAM, 14.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, "0"}, {0.25f, ""}, {0.5f, "5"}, {0.75f, ""}, {1.f, "10"}});
		section(3.f, 56.f, 24.48f, 28.f, "SYMMETRY");
		knob(math::Vec(x, 70.f), Fld::SYM_PARAM, 10.f, st::CAP_WHITE, -1.f, 1.f, st::bipolar());
		section(3.f, 88.f, 24.48f, 29.f, "FOLD CV");
		knob(math::Vec(x, 102.f), Fld::FOLD_CV_PARAM, 8.f, st::CAP_GREY, -1.f, 1.f, st::bipolar());

		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 34.f)), module, Fld::IN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 54.f)), module, Fld::FOLD_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 74.f)), module, Fld::SYM_CV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(x, 96.f)), module, Fld::OUT_OUTPUT));
	}
};

} // namespace

Model* modelJXMPFolder = createModel<Fld, FldWidget>("Folder");
