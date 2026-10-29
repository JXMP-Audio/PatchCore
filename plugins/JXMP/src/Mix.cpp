/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-MIX: four-channel mixer with level, CV and mute per channel, and a master level. Polyphonic.
 */

#include "JXMPWidget.hpp"
#include "dsp/Mix.hpp"

namespace {

struct Mix : engine::Module {
	enum ParamIds { ENUMS(LEVEL_PARAM, 4), ENUMS(MUTE_PARAM, 4), MASTER_PARAM, NUM_PARAMS };
	enum InputIds { ENUMS(IN_INPUT, 4), ENUMS(CV_INPUT, 4), NUM_INPUTS };
	enum OutputIds { MIX_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(MUTE_LIGHT, 4), ENUMS(LEVEL_LIGHT, 4), MASTER_LIGHT, NUM_LIGHTS };

	Mix() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 4; ++i) {
			configParam(LEVEL_PARAM + i, 0.f, 1.f, 0.8f, string::f("Channel %d level", i + 1), "%", 0.f, 100.f);
			configSwitch(MUTE_PARAM + i, 0.f, 1.f, 0.f, string::f("Channel %d mute", i + 1), {"On", "Muted"});
			configInput(IN_INPUT + i, string::f("In %d", i + 1))->description = "Channel input (audio or CV). Polyphonic; a mono input is spread over all channels of the mix.";
			configInput(CV_INPUT + i, string::f("CV %d", i + 1))->description = "Channel level CV: 0 V closed, 10 V open (times the channel knob).";
		}
		configParam(MASTER_PARAM, 0.f, 2.f, 1.f, "Master", "%", 0.f, 100.f);
		configOutput(MIX_OUTPUT, "Mix")->description = "Sum of the unmuted channels × their levels × MASTER.";
	}

	void process(const ProcessArgs& args) override {
		int channels = 1;
		for (int i = 0; i < 4; ++i)
			channels = std::max(channels, inputs[IN_INPUT + i].getChannels());

		float level[4];
		bool mute[4];
		for (int i = 0; i < 4; ++i) {
			level[i] = params[LEVEL_PARAM + i].getValue();
			mute[i] = params[MUTE_PARAM + i].getValue() > 0.5f;
		}
		const float master = params[MASTER_PARAM].getValue();

		float peak[4] = {};
		float masterPeak = 0.f;
		for (int c = 0; c < channels; ++c) {
			float in[4], cv[4];
			for (int i = 0; i < 4; ++i) {
				// Mono inputs are spread to every channel of a polyphonic mix
				in[i] = inputs[IN_INPUT + i].getPolyVoltage(c);
				cv[i] = jxmp::cvGain(inputs[CV_INPUT + i].getPolyVoltage(c), inputs[CV_INPUT + i].isConnected());
				if (!mute[i])
					peak[i] = std::max(peak[i], std::fabs(in[i] * level[i] * cv[i]));
			}
			const float v = math::clamp(jxmp::mix4(in, level, cv, mute, master), -12.f, 12.f);
			outputs[MIX_OUTPUT].setVoltage(v, c);
			masterPeak = std::max(masterPeak, std::fabs(v));
		}
		outputs[MIX_OUTPUT].setChannels(channels);

		for (int i = 0; i < 4; ++i) {
			lights[MUTE_LIGHT + i].setBrightness(mute[i]);
			lights[LEVEL_LIGHT + i].setBrightnessSmooth(peak[i] / 5.f, args.sampleTime);
		}
		lights[MASTER_LIGHT].setBrightnessSmooth(masterPeak / 5.f, args.sampleTime);
	}
};


struct MixWidget : JXMPModuleWidget {
	MixWidget(Mix* const module) {
		setup(module, "JX-MIX", nvgRGB(0xd8, 0x44, 0x3a), 14);

		// Front: JXMP style (research_docs/10-jxmp-design-guide.md)
		namespace st = jxmp::style;
		styled();
		const std::vector<st::Mark> percent =
			st::marks(0.f, 1.f, 0.1f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); });

		// CHANNELS: a strip per channel: name + level LED, level knob, mute button
		section(4.f, 20.5f, 63.12f, 50.f, "CHANNELS");
		for (int i = 0; i < 4; ++i) {
			const float x = 12.f + i * 15.7f;
			label(math::Vec(x - 1.5f, 25.5f), string::f("CH %d", i + 1).c_str(), 7.5f);
			addChild(createLightCentered<TinyLight<GreenLight>>(mm2px(math::Vec(x + 4.f, 25.5f)), module, Mix::LEVEL_LIGHT + i));
			knob(math::Vec(x, 39.f), Mix::LEVEL_PARAM + i, 9.f, st::CAP_WHITE, 0.f, 1.f, percent);
			label(math::Vec(x, 53.f), "MUTE", 6.5f);
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(math::Vec(x, 61.f)), module,
			                                                                               Mix::MUTE_PARAM + i, Mix::MUTE_LIGHT + i));
		}

		// MASTER
		section(4.f, 75.f, 63.12f, 42.f, "MASTER");
		label(math::Vec(35.56f, 80.f), "MASTER", 7.5f);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(46.f, 80.f)), module, Mix::MASTER_LIGHT));
		knob(math::Vec(35.56f, 97.f), Mix::MASTER_PARAM, 15.f, st::CAP_MAIN, 0.f, 2.f,
		     st::marks(0.f, 2.f, 0.2f, 5, [](float v) { return string::f("%d", (int) std::round(v * 100.f)); }));

		// Back: sockets
		for (int i = 0; i < 4; ++i) {
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 32.f + i * 16.f)), module, Mix::IN_INPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(31.f, 32.f + i * 16.f)), module, Mix::CV_INPUT + i));
		}
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(55.f, 32.f)), module, Mix::MIX_OUTPUT));
	}
};

} // namespace


Model* modelJXMPMixer = createModel<Mix, MixWidget>("Mixer");
