/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-CLK: the master clock. Follows, in this order: pulses on CLOCK IN (e.g. Host MIDI's clock), the PatchCore
 * transport (standalone: its own; in a DAW: the host's), or its own TEMPO when RUN is on. Outputs ×4, ×2, beat,
 * /2, /4, /8, /16 with swing on ×4 and ×2, RESET on start, RUN while running. All sockets on the back.
 * Design: research_docs/14-jxmp-modules-set3-design.md.
 */

#include "JXMPWidget.hpp"
#include "dsp/Clock.hpp"
#include "plugincontext.hpp"

namespace {

namespace ck = jxmp::clock;

enum Source { SRC_STOPPED, SRC_EXT, SRC_HOST, SRC_INT };

struct Clk : engine::Module {
	enum ParamIds { TEMPO_PARAM, RUN_PARAM, SWING_PARAM, PPQN_PARAM, NUM_PARAMS };
	enum InputIds { CLOCK_INPUT, NUM_INPUTS };
	enum OutputIds { ENUMS(DIV_OUTPUT, ck::kOutputs), RESET_OUTPUT, RUN_OUTPUT, NUM_OUTPUTS };
	enum LightIds { BEAT_LIGHT, EXT_LIGHT, HOST_LIGHT, INT_LIGHT, NUM_LIGHTS };

	CardinalPluginContext* const pcontext;
	dsp::SchmittTrigger clockTrigger;
	dsp::PulseGenerator resetPulse;
	ck::PulseFollower follower;

	Source source = SRC_STOPPED;
	double beats = 0.0;
	uint32_t lastProcessCounter = 0;
	bool wasRunning = false;
	/** Read by the display */
	float shownBpm = 120.f;

	Clk() : pcontext(static_cast<CardinalPluginContext*>(APP)) {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TEMPO_PARAM, 30.f, 300.f, 120.f, "Tempo", " BPM")
		    ->description = "Its own tempo, used when nothing is on CLOCK IN and the transport is stopped.";
		// Toggles: lever up = the highest value
		configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run", {"Stop", "Run"})
		    ->description = "Starts and stops the clock's own tempo (the transport and CLOCK IN run it by themselves).";
		configParam(SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%")
		    ->description = "Delays every second x4 and x2 tick: 50 % straight, 66 % triplet feel, 75 % hard shuffle.";
		configSwitch(PPQN_PARAM, 0.f, 2.f, 2.f, "Clock in pulses per beat", {"1 (beats)", "4 (sixteenths)", "24 (MIDI clock)"})
		    ->description = "How many CLOCK IN pulses make one beat. Host MIDI's clock output gives 24.";
		configInput(CLOCK_INPUT, "Clock in")->description =
		    "Clock pulses, e.g. the clock output of the Host MIDI module (tempo from the DAW or a MIDI device); "
		    "PPQN on the front says how many make one beat.";
		const char* names[ck::kOutputs] = {"x4", "x2", "Beat", "/2", "/4", "/8", "/16"};
		const char* what[ck::kOutputs] = {"Sixteenth notes (swing applies)", "Eighth notes (swing applies)", "Every beat",
		                                  "Every 2 beats", "Every 4 beats (a bar in 4/4)", "Every 8 beats", "Every 16 beats"};
		for (int k = 0; k < ck::kOutputs; ++k)
			configOutput(DIV_OUTPUT + k, names[k])->description = std::string(what[k]) + ", 10 V gates.";
		configOutput(RESET_OUTPUT, "Reset")->description = "A trigger when the clock starts: patch it to the RESET of sequencers. Patching this jack restarts the clock, so everything starts in step.";
		configOutput(RUN_OUTPUT, "Run")->description = "10 V while the clock runs.";
		configLight(BEAT_LIGHT, "Beat");
	}

	bool resetWasConnected = false;

	void onReset() override {
		follower.reset();
		beats = 0.0;
		wasRunning = false;
	}

	void process(const ProcessArgs& args) override {
		const double dt = args.sampleTime;
		const bool resetConnected = outputs[RESET_OUTPUT].isConnected();
		const bool ext = inputs[CLOCK_INPUT].isConnected();
		static const int ppqns[] = {1, 4, 24};
		follower.ppqn = ppqns[math::clamp((int) std::round(params[PPQN_PARAM].getValue()), 0, 2)];
		follower.process(ext && clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f), dt);

		const bool hostPlaying = pcontext != nullptr && pcontext->playing && pcontext->bbtValid;
		bool started = false;
		if (ext) {
			source = follower.running ? SRC_EXT : SRC_STOPPED;
			beats = follower.beats();
			started = follower.started;
			if (follower.bpm() > 0.0)
				shownBpm = (float) follower.bpm();
		}
		else if (hostPlaying) {
			// The transport: resync to its position on every audio block, move on per sample in between
			if (source != SRC_HOST || pcontext->processCounter != lastProcessCounter) {
				lastProcessCounter = pcontext->processCounter;
				const double tickBeats = pcontext->ticksPerBeat > 0.0 ? pcontext->tick / pcontext->ticksPerBeat : 0.0;
				beats = (double) (pcontext->bar - 1) * pcontext->beatsPerBar + (pcontext->beat - 1) + tickBeats;
				started = source != SRC_HOST || pcontext->reset;
			}
			else
				beats += dt * pcontext->beatsPerMinute / 60.0;
			source = SRC_HOST;
			shownBpm = (float) pcontext->beatsPerMinute;
		}
		else if (params[RUN_PARAM].getValue() > 0.5f) {
			// Patching RESET restarts the own clock too: on opening a patch the clock runs before the cables are
			// in, so the sequencers would start at random places in the bar; the restart lines them up
			if (source != SRC_INT || (resetConnected && !resetWasConnected)) {
				beats = 0.0;
				started = true;
			}
			else
				beats += dt * params[TEMPO_PARAM].getValue() / 60.0;
			source = SRC_INT;
			shownBpm = params[TEMPO_PARAM].getValue();
		}
		else {
			source = SRC_STOPPED;
			shownBpm = params[TEMPO_PARAM].getValue();
		}

		const bool running = source != SRC_STOPPED;
		if (started && running)
			resetPulse.trigger(1e-3f);
		wasRunning = running;
		resetWasConnected = resetConnected;

		const float swing = params[SWING_PARAM].getValue() / 100.f;
		for (int k = 0; k < ck::kOutputs; ++k)
			outputs[DIV_OUTPUT + k].setVoltage(running && ck::gate(beats, ck::kPeriods[k], swing, ck::swungOutput(k)) ? 10.f : 0.f);
		outputs[RESET_OUTPUT].setVoltage(resetPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[RUN_OUTPUT].setVoltage(running ? 10.f : 0.f);

		lights[BEAT_LIGHT].setBrightness(running && ck::gate(beats, 1.0, 0.5f, false) ? 1.f : 0.f);
		lights[EXT_LIGHT].setBrightness(source == SRC_EXT);
		lights[HOST_LIGHT].setBrightness(source == SRC_HOST);
		lights[INT_LIGHT].setBrightness(source == SRC_INT);
	}
};


/** Tempo window: the BPM in amber digits and where the tempo comes from. */
struct BpmDisplay : widget::TransparentWidget {
	Clk* module = nullptr;

	void draw(const DrawArgs& args) override {
		namespace st = jxmp::style;
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 2.5f);
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x10));
		nvgFill(vg);
		const NVGcolor lit = nvgRGB(0xff, 0xc0, 0x50);
		const float bpm = module ? module->shownBpm : 120.f;
		const Source src = module ? module->source : SRC_STOPPED;
		st::text(vg, math::Vec(box.size.x * 0.62f, box.size.y * 0.5f), string::f("%.1f", bpm), box.size.y * 0.62f,
		         src == SRC_STOPPED ? color::alpha(lit, 0.45f) : lit, st::FONT_NUMBER, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, 0.5f);
		const char* const what = src == SRC_EXT ? "CLOCK IN" : src == SRC_HOST ? "TRANSPORT" : src == SRC_INT ? "OWN TEMPO" : "STOPPED";
		st::text(vg, math::Vec(box.size.x * 0.68f, box.size.y * 0.36f), "BPM", box.size.y * 0.24f, color::alpha(lit, 0.7f), st::FONT_LABEL,
		         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.8f);
		st::text(vg, math::Vec(box.size.x * 0.68f, box.size.y * 0.68f), what, box.size.y * 0.2f, color::alpha(lit, 0.7f), st::FONT_LABEL,
		         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.4f);
	}
};


struct ClkWidget : JXMPModuleWidget {
	ClkWidget(Clk* const module) {
		namespace st = jxmp::style;
		setup(module, "JX-CLK", nvgRGB(0xff, 0x7f, 0x6a), 12);
		styled();

		// Tempo
		// The divider stops short: BEAT and its lamp sit at its end (as TRIG'D on JX-SCP)
		section(3.f, 20.5f, 40.f, 44.f, "TEMPO");
		label(math::Vec(48.f, 20.5f), "BEAT", 5.5f);
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(math::Vec(54.f, 20.5f)), module, Clk::BEAT_LIGHT));
		BpmDisplay* const lcd = new BpmDisplay;
		lcd->module = module;
		lcd->box = math::Rect(mm2px(math::Vec(6.f, 24.f)), mm2px(math::Vec(48.96f, 12.f)));
		addChild(lcd);
		knob(math::Vec(18.f, 51.f), Clk::TEMPO_PARAM, 14.f, st::CAP_MAIN, 30.f, 300.f,
		     st::marks(30.f, 300.f, 30.f, 9, [](float v) { return string::f("%d", (int) v); }));
		toggle(math::Vec(44.f, 51.f), Clk::RUN_PARAM, 2, {"RUN", "STOP"});

		// Swing and where the tempo comes from
		section(3.f, 67.f, 54.96f, 20.f, "SWING · SOURCE");
		knob(math::Vec(16.f, 78.f), Clk::SWING_PARAM, 10.f, st::CAP_WHITE, 50.f, 75.f,
		     st::marks(50.f, 75.f, 5.f, 5, [](float v) { return string::f("%d", (int) v); }));
		const struct { const char* name; int light; float y; } srcs[3] = {
		    {"CLOCK IN", Clk::EXT_LIGHT, 72.f}, {"TRANSPORT", Clk::HOST_LIGHT, 78.f}, {"OWN TEMPO", Clk::INT_LIGHT, 84.f}};
		for (const auto& s : srcs) {
			label(math::Vec(40.f, s.y), s.name, 5.5f);
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(math::Vec(53.f, s.y)), module, s.light));
		}

		// Pulses per beat of CLOCK IN (the socket is on the back)
		section(3.f, 90.f, 54.96f, 27.f, "CLOCK IN · PPQN");
		label(math::Vec(20.f, 101.f), "PULSES PER BEAT", 5.5f);
		label(math::Vec(20.f, 106.f), "ON CLOCK IN (BACK)", 5.5f);
		toggle(math::Vec(47.f, 104.f), Clk::PPQN_PARAM, 3, {"24", "4", "1"});

		// Back: clock in, then the outputs
		addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(30.48f, 32.f)), module, Clk::CLOCK_INPUT));
		const float xs[3] = {12.f, 30.48f, 48.96f};
		const int order[9] = {Clk::DIV_OUTPUT + 0, Clk::DIV_OUTPUT + 1, Clk::DIV_OUTPUT + 2, Clk::DIV_OUTPUT + 3, Clk::DIV_OUTPUT + 4,
		                      Clk::DIV_OUTPUT + 5, Clk::DIV_OUTPUT + 6, Clk::RESET_OUTPUT, Clk::RUN_OUTPUT};
		for (int i = 0; i < 9; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(xs[i % 3], 52.f + (i / 3) * 20.f)), module, order[i]));
	}
};

} // namespace


Model* modelJXMPClock = createModel<Clk, ClkWidget>("Clock");
