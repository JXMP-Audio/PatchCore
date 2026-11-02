/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-DRM: four drum voices in one module — KICK, SNARE, HAT (closed on TRIG, open on OPEN; closed chokes open)
 * and CLAP. Each with TUNE, DECAY, TONE, a trigger and an accent input and its own output; MIX sums them.
 *
 */

#include "JXMPWidget.hpp"
#include "dsp/Drums.hpp"

namespace {

namespace dr = jxmp::drums;

enum Voice { KICK, SNARE, HAT, CLAP, NUM_VOICES };

struct Drm : engine::Module {
	enum ParamIds { ENUMS(TUNE_PARAM, NUM_VOICES), ENUMS(DECAY_PARAM, NUM_VOICES), ENUMS(TONE_PARAM, NUM_VOICES), NUM_PARAMS };
	enum InputIds { ENUMS(TRIG_INPUT, NUM_VOICES), ENUMS(ACCENT_INPUT, NUM_VOICES), HAT_OPEN_INPUT, NUM_INPUTS };
	enum OutputIds { ENUMS(VOICE_OUTPUT, NUM_VOICES), MIX_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(HIT_LIGHT, NUM_VOICES), NUM_LIGHTS };

	dr::Kick kick;
	dr::Snare snare;
	dr::Hat hat;
	dr::Clap clap;
	dsp::SchmittTrigger trig[NUM_VOICES], openTrig;
	float flash[NUM_VOICES] = {};

	Drm() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const char* names[NUM_VOICES] = {"Kick", "Snare", "Hat", "Clap"};
		const char* tune[NUM_VOICES] = {"Pitch of the body, 35-90 Hz.", "Pitch of the drum body.", "Pitch of the metal.",
		                                "Pitch of the hand clap noise."};
		const char* decay[NUM_VOICES] = {"How long the kick booms.", "How long the snare wires rattle.",
		                                 "How long an OPEN hat rings (closed hats stay short).", "How long the clap's room tail is."};
		const char* tone[NUM_VOICES] = {"Click and drive: rounder to harder.", "Body to snare wires.", "Darker to brighter.",
		                                "Wider to narrower noise band."};
		for (int v = 0; v < NUM_VOICES; ++v) {
			configParam(TUNE_PARAM + v, 0.f, 1.f, 0.5f, string::f("%s tune", names[v]), "%", 0.f, 100.f)->description = tune[v];
			configParam(DECAY_PARAM + v, 0.f, 1.f, 0.5f, string::f("%s decay", names[v]), "%", 0.f, 100.f)->description = decay[v];
			configParam(TONE_PARAM + v, 0.f, 1.f, 0.4f, string::f("%s tone", names[v]), "%", 0.f, 100.f)->description = tone[v];
			configInput(TRIG_INPUT + v, string::f("%s trig", names[v]))->description =
			    v == HAT ? std::string("Plays a closed hat (and chokes a ringing open one), e.g. from JX-EUC GATE.")
			             : string::f("Plays the %s on a rising edge, e.g. from JX-EUC GATE.", names[v]);
			configInput(ACCENT_INPUT + v, string::f("%s accent", names[v]))->description =
			    "High while the trigger comes: that hit is louder (e.g. JX-EUC ACCENT).";
			configOutput(VOICE_OUTPUT + v, names[v])->description = string::f("The %s alone, about ±5 V.", names[v]);
		}
		configInput(HAT_OPEN_INPUT, "Hat open")->description = "Plays an open hat that rings for the HAT DECAY.";
		configOutput(MIX_OUTPUT, "Mix")->description = "All four voices together.";
	}

	void process(const ProcessArgs& args) override {
		const float dt = args.sampleTime;
		for (int v = 0; v < NUM_VOICES; ++v) {
			if (!trig[v].process(inputs[TRIG_INPUT + v].getVoltage(), 0.1f, 1.f))
				continue;
			const bool accent = inputs[ACCENT_INPUT + v].getVoltage() > 1.f;
			switch (v) {
				case KICK: kick.trigger(accent); break;
				case SNARE: snare.trigger(accent); break;
				case HAT: hat.trigger(false, accent); break;
				default: clap.trigger(accent); break;
			}
			flash[v] = 1.f;
		}
		if (openTrig.process(inputs[HAT_OPEN_INPUT].getVoltage(), 0.1f, 1.f)) {
			hat.trigger(true, inputs[ACCENT_INPUT + HAT].getVoltage() > 1.f);
			flash[HAT] = 1.f;
		}

		auto p = [&](int id, int v) { return params[id + v].getValue(); };
		const float out[NUM_VOICES] = {
		    kick.process(dt, p(TUNE_PARAM, KICK), p(DECAY_PARAM, KICK), p(TONE_PARAM, KICK)),
		    snare.process(dt, p(TUNE_PARAM, SNARE), p(DECAY_PARAM, SNARE), p(TONE_PARAM, SNARE)),
		    hat.process(dt, p(TUNE_PARAM, HAT), p(DECAY_PARAM, HAT), p(TONE_PARAM, HAT)),
		    clap.process(dt, p(TUNE_PARAM, CLAP), p(DECAY_PARAM, CLAP), p(TONE_PARAM, CLAP)),
		};
		float mix = 0.f;
		for (int v = 0; v < NUM_VOICES; ++v) {
			outputs[VOICE_OUTPUT + v].setVoltage(out[v]);
			mix += out[v];
			flash[v] = std::max(0.f, flash[v] - dt * 8.f);
			lights[HIT_LIGHT + v].setBrightness(flash[v]);
		}
		outputs[MIX_OUTPUT].setVoltage(math::clamp(mix * 0.7f, -10.f, 10.f));
	}
};


struct DrmWidget : JXMPModuleWidget {
	DrmWidget(Drm* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-DRM", nvgRGB(0xb5, 0x46, 0x2e), 18);
		styled();

		const char* titles[NUM_VOICES] = {"KICK", "SNARE", "HAT", "CLAP"};
		const float xs[NUM_VOICES] = {13.f, 35.f, 57.f, 79.f};
		const std::vector<st::Mark> pct = st::marks(0.f, 1.f, 0.1f, 10, [](float v) { return v < 0.5f ? std::string("-") : std::string("+"); });
		section(3.f, 20.5f, 85.44f, 96.f, "VOICES");
		for (int v = 0; v < NUM_VOICES; ++v) {
			const float x = xs[v];
			label(math::Vec(x - 2.5f, 27.f), titles[v], 7.f);
			addChild(createLightCentered<SmallLight<RedLight>>(mm2px(math::Vec(x + 6.5f, 27.f)), module, Drm::HIT_LIGHT + v));
			label(math::Vec(x, 35.f), "TUNE", 6.f);
			knob(math::Vec(x, 46.f), Drm::TUNE_PARAM + v, 11.f, st::CAP_MAIN, 0.f, 1.f, pct);
			label(math::Vec(x, 61.f), "DECAY", 6.f);
			knob(math::Vec(x, 72.f), Drm::DECAY_PARAM + v, 11.f, st::CAP_WHITE, 0.f, 1.f, pct);
			label(math::Vec(x, 87.f), "TONE", 6.f);
			knob(math::Vec(x, 98.f), Drm::TONE_PARAM + v, 11.f, st::CAP_GREY, 0.f, 1.f, pct);
		}

		// Back: per voice TRIG, ACCENT, OUT in a column; HAT OPEN and MIX underneath
		for (int v = 0; v < NUM_VOICES; ++v) {
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[v], 32.f)), module, Drm::TRIG_INPUT + v));
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[v], 50.f)), module, Drm::ACCENT_INPUT + v));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[v], 70.f)), module, Drm::VOICE_OUTPUT + v));
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(xs[HAT], 92.f)), module, Drm::HAT_OPEN_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[CLAP], 94.f)), module, Drm::MIX_OUTPUT));
	}
};

} // namespace


Model* modelJXMPDrums = createModel<Drm, DrmWidget>("Drums");
