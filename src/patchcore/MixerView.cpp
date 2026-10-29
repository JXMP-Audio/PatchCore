/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Mixer view: covers the rack area and shows the content built by the project's console module.
 */

#include "PatchCore.hpp"
#include <patchcore/Mixer.hpp>
#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <plugin/Model.hpp>

#include <map>

namespace rack {
namespace patchcore {

static bool mixerOpen = false;

static std::map<plugin::Model*, std::string>& singletonGroups() {
	static std::map<plugin::Model*, std::string> groups;
	return groups;
}

void registerSingletonModel(plugin::Model* model, const std::string& group) {
	singletonGroups()[model] = group;
}

bool singletonTaken(plugin::Model* model) {
	const std::map<plugin::Model*, std::string>& groups = singletonGroups();
	const auto it = groups.find(model);
	if (it == groups.end() || !APP->scene || !APP->scene->rack)
		return false;
	for (app::ModuleWidget* const mw : APP->scene->rack->getModules()) {
		const auto other = groups.find(mw->model);
		if (other != groups.end() && other->second == it->second)
			return true;
	}
	return false;
}

/** The first active console in the rack, or nullptr. */
static app::ModuleWidget* findConsole() {
	if (!APP->scene || !APP->scene->rack)
		return nullptr;
	for (app::ModuleWidget* const mw : APP->scene->rack->getModules()) {
		MixerProvider* const p = dynamic_cast<MixerProvider*>(mw);
		if (p && p->mixerActive())
			return mw;
	}
	return nullptr;
}

void showMixer(bool show) {
	mixerOpen = show && enabled() && findConsole() != nullptr;
}

bool mixerShown() {
	return mixerOpen;
}

bool mixerAvailable() {
	return enabled() && findConsole() != nullptr;
}


struct MixerView : widget::OpaqueWidget {
	app::ModuleWidget* owner = nullptr;
	widget::Widget* content = nullptr;

	void dropContent() {
		if (content) {
			removeChild(content);
			delete content;
			content = nullptr;
		}
		owner = nullptr;
	}

	void step() override {
		// Checked before the children step: the content's controls point at the console module
		app::ModuleWidget* const console = mixerOpen && enabled() ? findConsole() : nullptr;
		if (!console) {
			mixerOpen = false;
			dropContent();
			hide();
			return;
		}
		if (console != owner) {
			dropContent();
			owner = console;
			content = dynamic_cast<MixerProvider*>(console)->createMixerView();
			addChild(content);
		}
		content->box = box.zeroPos();
		OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(box.zeroPos()));
		nvgFillColor(args.vg, nvgRGB(0x17, 0x18, 0x1a));
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
		// Lights glow in layer 1, which the rack only draws for its modules
		Widget::drawLayer(args, 1);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		OpaqueWidget::onHoverKey(e);
		if (e.isConsumed())
			return;
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			showMixer(false);
			e.consume(this);
		}
	}
};

widget::Widget* createMixerView() {
	MixerView* const v = new MixerView;
	v->hide();
	return v;
}

} // namespace patchcore
} // namespace rack
