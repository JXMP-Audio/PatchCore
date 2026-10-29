/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-OSC: VCO with octave switch, shape morph, sub oscillator and unison.
 * Oscillator core and pitch handling based on VCV Fundamental's VCO (GPL-3.0-or-later).
 */

#include "JXMPWidget.hpp"
#include "dsp/Osc.hpp"
#include "dsp/OscillatorCore.hpp"

namespace {

using simd::float_4;
typedef jxmp::VoltageControlledOscillator<16, 16, float_4> Oscillator;

struct Osc : engine::Module {
	enum ParamIds {
		SYNC_PARAM, FREQ_PARAM, FM_PARAM, PW_PARAM, PW_CV_PARAM, LINEAR_PARAM,
		OCTAVE_PARAM, MORPH_PARAM, MORPH_CV_PARAM, SUB_LEVEL_PARAM, SUB_OCT_PARAM, VOICES_PARAM, DETUNE_PARAM,
		NUM_PARAMS
	};
	enum InputIds { PITCH_INPUT, FM_INPUT, SYNC_INPUT, PW_INPUT, MORPH_INPUT, NUM_INPUTS };
	enum OutputIds { SIN_OUTPUT, TRI_OUTPUT, SAW_OUTPUT, SQR_OUTPUT, SUB_OUTPUT, MIX_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUM_LIGHTS };

	/** Up to 7 unison voices per channel, 4 per SIMD oscillator */
	Oscillator osc[16][2];
	jxmp::SubDivider sub[16];

	Osc() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync mode", {"Soft", "Hard"});
		configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(FM_PARAM, -1.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configParam(PW_CV_PARAM, -1.f, 1.f, 0.f, "Pulse width modulation", "%", 0.f, 100.f);
		configSwitch(LINEAR_PARAM, 0.f, 1.f, 0.f, "FM mode", {"1V/octave", "Linear"});
		configParam(OCTAVE_PARAM, -2.f, 2.f, 0.f, "Octave")->snapEnabled = true;
		configParam(MORPH_PARAM, 0.f, 3.f, 0.f, "Morph (sine, triangle, saw, square)");
		configParam(MORPH_CV_PARAM, -1.f, 1.f, 0.f, "Morph CV", "%", 0.f, 100.f);
		configParam(SUB_LEVEL_PARAM, 0.f, 1.f, 0.f, "Sub level in mix", "%", 0.f, 100.f);
		configSwitch(SUB_OCT_PARAM, 0.f, 1.f, 0.f, "Sub octave", {"-1 octave", "-2 octaves"});
		configParam(VOICES_PARAM, 1.f, 7.f, 1.f, "Unison voices")->snapEnabled = true;
		configParam(DETUNE_PARAM, 0.f, 50.f, 15.f, "Unison detune", " cents");
		configInput(PITCH_INPUT, "V/Oct");
		configInput(FM_INPUT, "FM");
		configInput(SYNC_INPUT, "Sync");
		configInput(PW_INPUT, "PWM");
		configInput(MORPH_INPUT, "Morph");
		configOutput(SIN_OUTPUT, "Sin");
		configOutput(TRI_OUTPUT, "Tri");
		configOutput(SAW_OUTPUT, "Saw");
		configOutput(SQR_OUTPUT, "Sqr");
		configOutput(SUB_OUTPUT, "Sub");
		configOutput(MIX_OUTPUT, "Mix");
	}

	void process(const ProcessArgs& args) override {
		const float freqParam = params[FREQ_PARAM].getValue() / 12.f + params[OCTAVE_PARAM].getValue();
		const float fmParam = params[FM_PARAM].getValue();
		const float pwParam = params[PW_PARAM].getValue();
		const float pwCvParam = params[PW_CV_PARAM].getValue();
		const bool linear = params[LINEAR_PARAM].getValue() > 0.f;
		const bool soft = params[SYNC_PARAM].getValue() <= 0.f;
		const int subOct = params[SUB_OCT_PARAM].getValue() > 0.5f ? 2 : 1;
		const float subLevel = params[SUB_LEVEL_PARAM].getValue();

		float offsets[8];
		const int voices = jxmp::unisonOffsets((int) params[VOICES_PARAM].getValue(), params[DETUNE_PARAM].getValue(), offsets);
		const float norm = 1.f / std::sqrt((float) voices);
		const int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);

		for (int c = 0; c < channels; ++c) {
			const float pitchIn = inputs[PITCH_INPUT].getVoltage(c);
			const float fmIn = inputs[FM_INPUT].getPolyVoltage(c);
			const float pw = pwParam + inputs[PW_INPUT].getPolyVoltage(c) / 10.f * pwCvParam;
			const float_4 sync(inputs[SYNC_INPUT].getPolyVoltage(c));
			float sum[4] = {};

			for (int g = 0; g * 4 < voices; ++g) {
				Oscillator& o = osc[c][g];
				const int lanes = std::min(4, voices - g * 4);
				o.channels = lanes;
				o.analog = true;
				o.soft = soft;

				float_4 detune;
				for (int l = 0; l < 4; ++l)
					detune[l] = l < lanes ? offsets[g * 4 + l] / 1200.f : 0.f;
				float_4 pitch = freqParam + pitchIn + detune;
				float_4 freq;
				if (!linear) {
					pitch += fmIn * fmParam;
					freq = dsp::FREQ_C4 * dsp::approxExp2_taylor5(pitch + 30.f) / std::pow(2.f, 30.f);
				}
				else {
					freq = dsp::FREQ_C4 * dsp::approxExp2_taylor5(pitch + 30.f) / std::pow(2.f, 30.f);
					freq += dsp::FREQ_C4 * fmIn * fmParam;
				}
				o.freq = simd::clamp(freq, 0.f, args.sampleRate / 2.f);
				o.setPulseWidth(float_4(pw));
				o.syncEnabled = inputs[SYNC_INPUT].isConnected();
				o.process(args.sampleTime, sync);

				const float_4 s = o.sin(), t = o.tri(), w = o.saw(), q = o.sqr();
				for (int l = 0; l < lanes; ++l) {
					sum[0] += s[l];
					sum[1] += t[l];
					sum[2] += w[l];
					sum[3] += q[l];
				}
			}

			const float subV = sub[c].process(osc[c][0].phase[0], subOct);
			const float morph = params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getPolyVoltage(c) / 10.f * 3.f * params[MORPH_CV_PARAM].getValue();
			float out[6];
			for (int i = 0; i < 4; ++i)
				out[i] = 5.f * sum[i] * norm;
			out[4] = 5.f * subV;
			out[5] = jxmp::morph(out[0], out[1], out[2], out[3], morph) + subLevel * out[4];

			bool finite = true;
			for (int i = 0; i < 6; ++i) {
				finite = finite && std::isfinite(out[i]);
				outputs[SIN_OUTPUT + i].setVoltage(math::clamp(out[i], -12.f, 12.f), c);
			}
			if (!finite) {
				// Never let a bad state stick: restart this channel's oscillators
				osc[c][0] = Oscillator();
				osc[c][1] = Oscillator();
				for (int i = 0; i < 6; ++i)
					outputs[SIN_OUTPUT + i].setVoltage(0.f, c);
			}
		}
		for (int i = 0; i < NUM_OUTPUTS; ++i)
			outputs[i].setChannels(channels);
	}
};


struct OscWidget : JXMPModuleWidget {
	OscWidget(Osc* const module) {
		setup(module, "JX-OSC", nvgRGB(0xee, 0x7b, 0x1f), 14);

		// Front: JXMP style, three framed sections
		namespace st = jxmp::style;
		styled();
		const std::vector<std::string> zeroToTen = {"0", "", "2", "", "4", "", "6", "", "8", "", "10"};

		section(3.f, 20.5f, 65.1f, 28.f, "PITCH");
		knob(math::Vec(15.f, 33.5f), Osc::FREQ_PARAM, 16.f, st::CAP_MAIN, 10, zeroToTen);
		label(math::Vec(15.f, 46.6f), "FREQ");
		knob(math::Vec(34.f, 33.f), Osc::OCTAVE_PARAM, 9.f, st::CAP_BLACK, 4, {"-2", "-1", "0", "+1", "+2"});
		label(math::Vec(34.f, 43.f), "OCTAVE");
		knob(math::Vec(47.5f, 33.f), Osc::FM_PARAM, 7.f, st::CAP_GREY, 10);
		label(math::Vec(47.5f, 43.f), "FM");
		label(math::Vec(57.f, 26.f), "LIN", 6.f);
		addParam(createParamCentered<CKSS>(mm2px(math::Vec(57.f, 33.f)), module, Osc::LINEAR_PARAM));
		label(math::Vec(64.f, 26.f), "HARD", 6.f);
		addParam(createParamCentered<CKSS>(mm2px(math::Vec(64.f, 33.f)), module, Osc::SYNC_PARAM));
		label(math::Vec(60.5f, 43.f), "FM · SYNC", 6.f);

		section(3.f, 52.5f, 65.1f, 28.f, "SHAPE");
		knob(math::Vec(13.f, 65.f), Osc::PW_PARAM, 9.f, st::CAP_WHITE, 10);
		label(math::Vec(13.f, 75.f), "PW");
		knob(math::Vec(25.f, 65.f), Osc::PW_CV_PARAM, 7.f, st::CAP_GREY, 10);
		label(math::Vec(25.f, 75.f), "PWM");
		const math::Vec morphPos(43.f, 64.5f);
		knob(morphPos, Osc::MORPH_PARAM, 10.f, st::CAP_MAIN, 3);
		const st::Icon waves[4] = {st::ICON_SIN, st::ICON_TRI, st::ICON_SAW, st::ICON_SQR};
		for (int w = 0; w < 4; ++w) {
			const float a = -0.83f * M_PI + 1.66f * M_PI * w / 3.f - M_PI / 2;
			icon(waves[w], morphPos.plus(math::Vec(std::cos(a), std::sin(a)).mult(9.f)), 3.4f);
		}
		label(math::Vec(43.f, 77.6f), "MORPH");
		knob(math::Vec(60.f, 65.f), Osc::MORPH_CV_PARAM, 7.f, st::CAP_GREY, 10);
		label(math::Vec(60.f, 75.f), "CV");

		section(3.f, 84.5f, 65.1f, 28.f, "UNISON · SUB");
		knob(math::Vec(13.f, 97.f), Osc::VOICES_PARAM, 9.f, st::CAP_BLACK, 6, {"1", "2", "3", "4", "5", "6", "7"});
		label(math::Vec(13.f, 107.f), "VOICES");
		knob(math::Vec(29.f, 97.f), Osc::DETUNE_PARAM, 9.f, st::CAP_WHITE, 10);
		label(math::Vec(29.f, 107.f), "DETUNE");
		knob(math::Vec(45.f, 97.f), Osc::SUB_LEVEL_PARAM, 9.f, st::CAP_WHITE, 10);
		label(math::Vec(45.f, 107.f), "SUB");
		label(math::Vec(60.f, 90.5f), "-1", 6.f);
		addParam(createParamCentered<CKSS>(mm2px(math::Vec(60.f, 97.f)), module, Osc::SUB_OCT_PARAM));
		label(math::Vec(60.f, 103.5f), "-2", 6.f);
		label(math::Vec(60.f, 107.f), "OCT", 6.f);

		// Back: sockets
		for (int i = 0; i < Osc::NUM_INPUTS; ++i)
			addInput(createInputCentered<PJ301MPort>(mm2px(math::Vec(13.f, 30.f + i * 15.f)), module, Osc::PITCH_INPUT + i));
		for (int i = 0; i < 4; ++i)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(42.f, 30.f + i * 15.f)), module, Osc::SIN_OUTPUT + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(58.f, 30.f)), module, Osc::SUB_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(math::Vec(58.f, 45.f)), module, Osc::MIX_OUTPUT));
	}
};

} // namespace


Model* modelJXMPVCO = createModel<Osc, OscWidget>("VCO");
