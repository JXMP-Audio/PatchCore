/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-LGC: logic. AND, OR, XOR and NOT of two gates (high above 1 V), and a comparator: CMP OUT is high while
 * CMP IN is above THRESHOLD. Polyphonic. Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Shapers.hpp"

namespace {

struct Lgc : engine::Module {
	enum ParamIds { THRESH_PARAM, NUM_PARAMS };
	enum InputIds { A_INPUT, B_INPUT, CMP_INPUT, NUM_INPUTS };
	enum OutputIds { AND_OUTPUT, OR_OUTPUT, XOR_OUTPUT, NOT_A_OUTPUT, CMP_OUTPUT, NUM_OUTPUTS };
	enum LightIds { AND_LIGHT, OR_LIGHT, XOR_LIGHT, NOT_LIGHT, CMP_LIGHT, NUM_LIGHTS };

	Lgc() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESH_PARAM, -10.f, 10.f, 0.f, "Threshold", " V")->description = "CMP OUT is high while CMP IN is above this voltage.";
		configInput(A_INPUT, "A")->description = "Gate A (high above 1 V).";
		configInput(B_INPUT, "B")->description = "Gate B (high above 1 V).";
		configInput(CMP_INPUT, "Cmp in")->description = "Any voltage to compare with THRESHOLD (an LFO gives a pulse wave).";
		configOutput(AND_OUTPUT, "And")->description = "High while A and B are both high.";
		configOutput(OR_OUTPUT, "Or")->description = "High while A or B (or both) is high.";
		configOutput(XOR_OUTPUT, "Xor")->description = "High while exactly one of A and B is high.";
		configOutput(NOT_A_OUTPUT, "Not A")->description = "High while A is low.";
		configOutput(CMP_OUTPUT, "Cmp out")->description = "High (10 V) while CMP IN is above THRESHOLD.";
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max({1, inputs[A_INPUT].getChannels(), inputs[B_INPUT].getChannels()});
		for (int o = AND_OUTPUT; o <= NOT_A_OUTPUT; ++o)
			outputs[o].setChannels(channels);
		bool lit[4] = {};
		for (int c = 0; c < channels; ++c) {
			const jxmp::shapers::Gates g{inputs[A_INPUT].getPolyVoltage(c) > 1.f, inputs[B_INPUT].getPolyVoltage(c) > 1.f};
			const bool v[4] = {g.andGate(), g.orGate(), g.xorGate(), !g.a};
			for (int k = 0; k < 4; ++k) {
				outputs[AND_OUTPUT + k].setVoltage(v[k] ? 10.f : 0.f, c);
				lit[k] = lit[k] || v[k];
			}
		}
		for (int k = 0; k < 4; ++k)
			lights[AND_LIGHT + k].setBrightness(lit[k]);
		const int cc = std::max(1, inputs[CMP_INPUT].getChannels());
		outputs[CMP_OUTPUT].setChannels(cc);
		bool cmpLit = false;
		for (int c = 0; c < cc; ++c) {
			const bool high = inputs[CMP_INPUT].getVoltage(c) > params[THRESH_PARAM].getValue();
			outputs[CMP_OUTPUT].setVoltage(high ? 10.f : 0.f, c);
			cmpLit = cmpLit || high;
		}
		lights[CMP_LIGHT].setBrightness(cmpLit);
	}
};

struct LgcWidget : JXMPModuleWidget {
	LgcWidget(Lgc* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-LGC", nvgRGB(0x9a, 0xa6, 0xb4), 8);
		styled();
		section(3.f, 20.5f, 34.64f, 50.f, "GATES A · B");
		const char* names[4] = {"AND", "OR", "XOR", "NOT A"};
		for (int k = 0; k < 4; ++k) {
			label(math::Vec(14.f, 32.f + k * 10.f), names[k], 7.f);
			addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(math::Vec(28.f, 32.f + k * 10.f)), module, Lgc::AND_LIGHT + k));
		}
		section(3.f, 74.f, 34.64f, 43.f, "COMPARATOR");
		knob(math::Vec(20.32f, 92.f), Lgc::THRESH_PARAM, 12.f, st::CAP_MAIN, -10.f, 10.f,
		     st::marks(-10.f, 10.f, 2.f, 5, [](float v) { return v == 0.f ? std::string("0") : string::f("%+d", (int) v); }));
		label(math::Vec(20.32f, 104.5f), "THRESHOLD", 6.f);
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(math::Vec(33.f, 80.f)), module, Lgc::CMP_LIGHT));

		const float xs[2] = {11.f, 29.64f};
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 32.f)), module, Lgc::A_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 32.f)), module, Lgc::B_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 52.f)), module, Lgc::AND_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 52.f)), module, Lgc::OR_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 72.f)), module, Lgc::XOR_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 72.f)), module, Lgc::NOT_A_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[0], 94.f)), module, Lgc::CMP_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[1], 94.f)), module, Lgc::CMP_OUTPUT));
	}
};

} // namespace

Model* modelJXMPLogic = createModel<Lgc, LgcWidget>("Logic");
