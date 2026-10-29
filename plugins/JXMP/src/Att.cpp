/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-ATT: four attenuverters with offsets. An unpatched input takes the input of the row above,
 * the first one is a 10 V constant, so a row without input is a manual voltage source. Polyphonic.
 */

#include "JXMPWidget.hpp"
#include "dsp/Att.hpp"

namespace {

struct Att : engine::Module {
	enum ParamIds { ENUMS(GAIN_PARAM, 4), ENUMS(OFFSET_PARAM, 4), NUM_PARAMS };
	enum InputIds { ENUMS(IN_INPUT, 4), NUM_INPUTS };
	enum OutputIds { ENUMS(OUT_OUTPUT, 4), NUM_OUTPUTS };
	enum LightIds { ENUMS(LEVEL_LIGHT, 4 * 2), NUM_LIGHTS };

	Att() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 4; ++i) {
			configParam(GAIN_PARAM + i, -2.f, 2.f, 1.f, string::f("Row %d gain", i + 1), "×");
			configParam(OFFSET_PARAM + i, -10.f, 10.f, 0.f, string::f("Row %d offset", i + 1), " V");
			configInput(IN_INPUT + i, string::f("In %d", i + 1))->description = "Row input. Unpatched, it takes the input of the row above; the first row then gives 10 V, so OFFSET/GAIN make a manual voltage.";
			configOutput(OUT_OUTPUT + i, string::f("Out %d", i + 1))->description = "Input × GAIN + OFFSET, limited to ±12 V.";
			configBypass(IN_INPUT + i, OUT_OUTPUT + i);
		}
	}

	void process(const ProcessArgs& args) override {
		bool patched[4];
		int channels = 1;
		for (int i = 0; i < 4; ++i) {
			patched[i] = inputs[IN_INPUT + i].isConnected();
			channels = std::max(channels, inputs[IN_INPUT + i].getChannels());
		}
		float level[4] = {};
		for (int c = 0; c < channels; ++c) {
			float in[4], normalled[4];
			for (int i = 0; i < 4; ++i)
				in[i] = inputs[IN_INPUT + i].getPolyVoltage(c);
			jxmp::normalInputs(in, patched, normalled);
			for (int i = 0; i < 4; ++i) {
				const float v = jxmp::attenuvert(normalled[i], params[GAIN_PARAM + i].getValue(), params[OFFSET_PARAM + i].getValue());
				outputs[OUT_OUTPUT + i].setVoltage(v, c);
				if (c == 0)
					level[i] = v;
			}
		}
		for (int i = 0; i < 4; ++i) {
			outputs[OUT_OUTPUT + i].setChannels(channels);
			lights[LEVEL_LIGHT + 2 * i + 0].setBrightnessSmooth(std::max(0.f, level[i] / 5.f), args.sampleTime);
			lights[LEVEL_LIGHT + 2 * i + 1].setBrightnessSmooth(std::max(0.f, -level[i] / 5.f), args.sampleTime);
		}
	}
};


struct AttWidget : JXMPModuleWidget {
	AttWidget(Att* const module) {
		setup(module, "JX-ATT", nvgRGB(0x6f, 0x86, 0xa8), 10);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();

		section(4.f, 20.5f, 42.8f, 97.f, "ATTENUVERT · OFFSET");
		label(math::Vec(19.f, 26.f), "GAIN", 7.5f);
		label(math::Vec(36.f, 26.f), "OFFSET", 7.5f);
		for (int i = 0; i < 4; ++i) {
			const float y = 37.f + i * 21.5f;
			label(math::Vec(8.f, y), string::f("%d", i + 1).c_str(), 8.f);
			knob(math::Vec(19.f, y), Att::GAIN_PARAM + i, 9.f, st::CAP_MAIN, -2.f, 2.f,
			     {{-2.f, "-2"}, {-1.f, ""}, {0.f, "0"}, {1.f, ""}, {2.f, "+2"}});
			knob(math::Vec(36.f, y), Att::OFFSET_PARAM + i, 9.f, st::CAP_WHITE, -10.f, 10.f,
			     st::marks(-10.f, 10.f, 2.f, 5, [](float v) { return v > 0.f ? string::f("+%d", (int) v) : string::f("%d", (int) v); }));
			addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(math::Vec(8.f, y + 5.f)), module, Att::LEVEL_LIGHT + 2 * i));
		}

		// Back: sockets
		for (int i = 0; i < 4; ++i) {
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 32.f + i * 18.f)), module, Att::IN_INPUT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(37.f, 32.f + i * 18.f)), module, Att::OUT_OUTPUT + i));
		}
	}
};

} // namespace


Model* modelJXMPAttenuverter = createModel<Att, AttWidget>("Attenuverter");
