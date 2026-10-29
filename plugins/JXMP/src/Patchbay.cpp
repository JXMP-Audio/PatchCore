/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plugin.hpp"
#include "PatchbayLogic.hpp"
#include "Serial.hpp"

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
	/** Drag tracking: the cable being dragged, which channels own it, and each channel's direction before it */
	app::CableWidget* lastDrag = nullptr;
	bool owned[N] = {};
	jxmp::Dir lastDir[N] = {};

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

	static bool hasCompleteCable(app::PortWidget* const port) {
		for (app::CableWidget* const cw : APP->scene->rack->getCablesOnPort(port)) {
			if (cw->isComplete())
				return true;
		}
		return false;
	}

	bool ownsPort(app::PortWidget* const port, const int k) const {
		return port == frontIn[k] || port == frontOut[k] || port == backIn[k] || port == backOut[k];
	}

	void step() override {
		app::CableWidget* const cw = APP->scene->rack->getIncompleteCable();
		const jxmp::Need dragging = cw ? jxmp::need(cw->outputPort != nullptr, cw->inputPort != nullptr) : jxmp::Need::Nothing;

		if (cw != lastDrag) {
			for (int k = 0; k < N; ++k)
				owned[k] = cw != nullptr && jxmp::takesOwnership(lastDir[k], ownsPort(cw->outputPort, k) || ownsPort(cw->inputPort, k));
			lastDrag = cw;
		}

		for (int k = 0; k < N; ++k) {
			jxmp::ChannelCables c;
			c.frontIn = hasCompleteCable(frontIn[k]);
			c.frontOut = hasCompleteCable(frontOut[k]);
			c.backIn = hasCompleteCable(backIn[k]);
			c.backOut = hasCompleteCable(backOut[k]);
			const jxmp::Dir dir = jxmp::direction(c);
			lastDir[k] = dir;

			// Classic look has no sides: only the front pair takes cables
			if (!patchcore::enabled()) {
				const jxmp::Roles r = jxmp::roles(dir, dragging);
				setRole(frontIn[k], frontOut[k], r.front);
				backIn[k]->setActive(false);
				backOut[k]->setActive(false);
				continue;
			}

			// A cable dragged out of this free channel: its end follows what the user hovers
			if (dir == jxmp::Dir::Free && cw != nullptr && owned[k] && (ownsPort(cw->outputPort, k) || ownsPort(cw->inputPort, k))) {
				const bool front = cw->outputPort == frontOut[k] || cw->inputPort == frontIn[k];
				const jxmp::Role current = cw->outputPort != nullptr ? jxmp::Role::Out : jxmp::Role::In;
				const bool foreignOut = cw->hoveredOutputPort != nullptr && !ownsPort(cw->hoveredOutputPort, k);
				const bool foreignIn = cw->hoveredInputPort != nullptr && !ownsPort(cw->hoveredInputPort, k);
				const jxmp::Role role = jxmp::ownedDragRole(current, foreignOut, foreignIn);
				if (role != current) {
					cw->outputPort = role == jxmp::Role::Out ? (front ? frontOut[k] : backOut[k]) : nullptr;
					cw->inputPort = role == jxmp::Role::In ? (front ? frontIn[k] : backIn[k]) : nullptr;
					cw->updateCable();
				}
				setRole(front ? frontIn[k] : backIn[k], front ? frontOut[k] : backOut[k], role);
				continue;
			}

			const jxmp::Roles r = jxmp::roles(dir, dragging);
			setRole(frontIn[k], frontOut[k], r.front);
			setRole(backIn[k], backOut[k], r.back);
		}

		ModuleWidget::step();
	}

	static void setRole(SmartPort* const in, SmartPort* const out, const jxmp::Role role) {
		in->setActive(role == jxmp::Role::In);
		out->setActive(role == jxmp::Role::Out);
	}

	std::string serialLabel() const override {
		return string::f("JX-PB%d · S/N ", N) + jxmp::serialNumber(module ? module->id : -1);
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
