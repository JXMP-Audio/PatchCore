/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plugin.hpp"
#include "PatchbayLogic.hpp"

// Internal types stay local to this file: every plugin links into one binary
namespace {

template <int N>
struct Patchbay : engine::Module {
	enum ParamIds { NUM_PARAMS };
	enum InputIds { ENUMS(FRONT_INPUT, N), ENUMS(BACK_INPUT, N), NUM_INPUTS };
	enum OutputIds { ENUMS(FRONT_OUTPUT, N), ENUMS(BACK_OUTPUT, N), NUM_OUTPUTS };
	enum LightIds { ENUMS(FORWARD_LIGHT, N), ENUMS(REVERSE_LIGHT, N), NUM_LIGHTS };

	Patchbay() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int k = 0; k < N; ++k) {
			configInput(FRONT_INPUT + k, string::f("Ch %d", k + 1));
			configInput(BACK_INPUT + k, string::f("Ch %d", k + 1));
			configOutput(FRONT_OUTPUT + k, string::f("Ch %d", k + 1));
			configOutput(BACK_OUTPUT + k, string::f("Ch %d", k + 1));
		}
	}

	static void pass(engine::Input& in, engine::Output& out) {
		const int channels = in.getChannels();
		out.setChannels(channels);
		if (channels > 0)
			out.writeVoltages(in.getVoltages());
	}

	/** Dim when patched, brighter with signal. */
	static float level(engine::Input& in) {
		if (!in.isConnected())
			return 0.f;
		return math::clamp(std::fabs(in.getVoltageSum()) / 5.f, 0.25f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		for (int k = 0; k < N; ++k) {
			pass(inputs[FRONT_INPUT + k], outputs[BACK_OUTPUT + k]);
			pass(inputs[BACK_INPUT + k], outputs[FRONT_OUTPUT + k]);
			lights[FORWARD_LIGHT + k].setBrightnessSmooth(level(inputs[FRONT_INPUT + k]), args.sampleTime);
			lights[REVERSE_LIGHT + k].setBrightnessSmooth(level(inputs[BACK_INPUT + k]), args.sampleTime);
		}
	}
};


/** One half of a smart jack. Inactive halves have no hit box and draw nothing. */
struct SmartPort : PJ301MPort {
	math::Vec center;
	math::Vec fullSize;
	bool active = true;

	void setActive(const bool a) {
		if (a == active)
			return;
		active = a;
		box.size = a ? fullSize : math::Vec();
		box.pos = a ? center.minus(fullSize.div(2)) : center;
	}

	void draw(const DrawArgs& args) override {
		if (active)
			PJ301MPort::draw(args);
	}
};


template <int N>
struct PatchbayLabels : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, nvgRGB(0xd8, 0xdb, 0xe0));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFontSize(args.vg, 9.f);
		nvgText(args.vg, box.size.x / 2, mm2px(8.f), string::f("JX-PB%d", N).c_str(), nullptr);
		nvgFontSize(args.vg, 8.f);
		nvgText(args.vg, box.size.x / 2, mm2px(123.f), "JXMP", nullptr);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFontSize(args.vg, 7.f);
		for (int k = 0; k < N; ++k) {
			const math::Vec p = jackPos(k);
			nvgText(args.vg, p.x - mm2px(7.8f), p.y, string::f("%d", k + 1).c_str(), nullptr);
		}
	}

	static math::Vec jackPos(const int k) {
		const int rows = N == 16 ? 8 : N;
		const float pitch = N == 4 ? 22.f : 12.f;
		const float top = N == 4 ? 30.f : 24.f;
		const float x = (N == 16 && k >= 8) ? 29.32f : 9.f;
		return mm2px(math::Vec(x, top + (k % rows) * pitch));
	}
};


template <int N>
struct PatchbayWidget : app::ModuleWidget, patchcore::SidedModuleWidget {
	typedef Patchbay<N> M;
	SmartPort* frontIn[N];
	SmartPort* frontOut[N];
	SmartPort* backIn[N];
	SmartPort* backOut[N];

	PatchbayWidget(M* const module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, N == 16 ? "res/Panel8HP.svg" : "res/Panel4HP.svg")));

		PatchbayLabels<N>* const labels = new PatchbayLabels<N>;
		labels->box.size = box.size;
		addChild(labels);

		for (int k = 0; k < N; ++k) {
			const math::Vec p = PatchbayLabels<N>::jackPos(k);
			frontIn[k] = addSmartInput(p, M::FRONT_INPUT + k);
			frontOut[k] = addSmartOutput(p, M::FRONT_OUTPUT + k);
			backIn[k] = addSmartInput(p, M::BACK_INPUT + k);
			backOut[k] = addSmartOutput(p, M::BACK_OUTPUT + k);
			addChild(createLightCentered<TinyLight<GreenLight>>(p.plus(mm2px(math::Vec(7.f, -2.2f))), module, M::FORWARD_LIGHT + k));
			addChild(createLightCentered<TinyLight<YellowLight>>(p.plus(mm2px(math::Vec(7.f, 2.2f))), module, M::REVERSE_LIGHT + k));
		}
	}

	SmartPort* addSmartInput(const math::Vec pos, const int id) {
		SmartPort* const port = createInputCentered<SmartPort>(pos, module, id);
		port->center = pos;
		port->fullSize = port->box.size;
		addInput(port);
		return port;
	}

	SmartPort* addSmartOutput(const math::Vec pos, const int id) {
		SmartPort* const port = createOutputCentered<SmartPort>(pos, module, id);
		port->center = pos;
		port->fullSize = port->box.size;
		addOutput(port);
		return port;
	}

	patchcore::PortSide portSide(const engine::Port::Type, const int portId) const override {
		// Front ids come first in both port enums
		return portId < N ? patchcore::SIDE_FRONT : patchcore::SIDE_BACK;
	}
};


} // namespace


Model* modelJXMPPatchbay4 = createModel<Patchbay<4>, PatchbayWidget<4>>("Patchbay4");
Model* modelJXMPPatchbay8 = createModel<Patchbay<8>, PatchbayWidget<8>>("Patchbay8");
Model* modelJXMPPatchbay16 = createModel<Patchbay<16>, PatchbayWidget<16>>("Patchbay16");
