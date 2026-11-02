/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-RNG: ring modulator. Multiplies IN A (carrier) by IN B (modulator): bell, metal and robot tones. MIX from
 * A dry to the product, with CV. Polyphonic.
 */

#include "JXMPWidget.hpp"
#include "dsp/Shapers.hpp"

namespace {

struct RingMod : engine::Module {
	enum ParamIds { MIX_PARAM, NUM_PARAMS };
	enum InputIds { A_INPUT, B_INPUT, MIX_CV_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	RingMod() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f)->description = "From A alone (dry) to A × B (ring).";
		configInput(A_INPUT, "In A")->description = "Carrier, e.g. an oscillator (±5 V).";
		configInput(B_INPUT, "In B")->description = "Modulator, e.g. a second oscillator at another pitch; an LFO gives tremolo.";
		configInput(MIX_CV_INPUT, "Mix CV")->description = "0..10 V adds to MIX.";
		configOutput(OUT_OUTPUT, "Out")->description = "A × B (5 V × 5 V = 5 V), mixed with A by MIX.";
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max({1, inputs[A_INPUT].getChannels(), inputs[B_INPUT].getChannels()});
		outputs[OUT_OUTPUT].setChannels(channels);
		for (int c = 0; c < channels; ++c) {
			const float mix = params[MIX_PARAM].getValue() + inputs[MIX_CV_INPUT].getPolyVoltage(c) / 10.f;
			outputs[OUT_OUTPUT].setVoltage(jxmp::shapers::ring(inputs[A_INPUT].getPolyVoltage(c), inputs[B_INPUT].getPolyVoltage(c), mix), c);
		}
	}
};

struct RingModWidget : JXMPModuleWidget {
	RingModWidget(RingMod* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-RNG", nvgRGB(0xc0, 0x7a, 0x3e), 6);
		styled();
		const float x = 15.24f;
		section(3.f, 20.5f, 24.48f, 32.f, "MIX");
		knob(math::Vec(x, 36.f), RingMod::MIX_PARAM, 14.f, st::CAP_MAIN, 0.f, 1.f,
		     {{0.f, "DRY"}, {0.25f, ""}, {0.5f, "50"}, {0.75f, ""}, {1.f, "RING"}});
		section(3.f, 56.f, 24.48f, 61.f, "A x B");
		label(math::Vec(x, 64.f), "CARRIER A", 6.f);
		label(math::Vec(x, 70.f), "MODULATOR B", 6.f);
		label(math::Vec(x, 80.f), "BELLS, METAL,", 5.5f);
		label(math::Vec(x, 85.f), "ROBOT VOICES", 5.5f);
		label(math::Vec(x, 95.f), "LFO ON B:", 5.5f);
		label(math::Vec(x, 100.f), "TREMOLO", 5.5f);

		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 34.f)), module, RingMod::A_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 54.f)), module, RingMod::B_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(x, 74.f)), module, RingMod::MIX_CV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(x, 96.f)), module, RingMod::OUT_OUTPUT));
	}
};

} // namespace

Model* modelJXMPRing = createModel<RingMod, RingModWidget>("Ring");
