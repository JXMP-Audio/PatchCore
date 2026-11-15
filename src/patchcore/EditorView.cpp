/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Editor view: covers the rack area and shows the content built by the project's editor module (JX-EDIT).
 */

#include <set>
#include "PatchCore.hpp"
#include <patchcore/Editor.hpp>
#include <patchcore/Mixer.hpp>
#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>

namespace rack {
namespace patchcore {

/** The first active editor module in the rack, or nullptr. */
static app::ModuleWidget* findEditor() {
	if (!APP->scene || !APP->scene->rack)
		return nullptr;
	for (app::ModuleWidget* const mw : APP->scene->rack->getModules()) {
		EditorProvider* const p = dynamic_cast<EditorProvider*>(mw);
		if (p && p->editorActive())
			return mw;
	}
	return nullptr;
}

struct EditorView;
/** The Editor view of the current scene: each plugin instance has its own (a global flag opened the Editor in every
instance of a DAW session) */
static EditorView* currentView();

void showEditor(bool show);
bool editorShown();

bool editorAvailable() {
	return enabled() && findEditor() != nullptr;
}


struct EditorView;
static std::set<EditorView*>& allViews() {
	static std::set<EditorView*> views;
	return views;
}

struct EditorView : widget::OpaqueWidget {
	EditorView() {
		allViews().insert(this);
	}
	~EditorView() override {
		allViews().erase(this);
	}

	bool open = false;
	app::ModuleWidget* owner = nullptr;
	EditorProvider* ownerProvider = nullptr;
	widget::Widget* content = nullptr;

	void dropContent() {
		if (content) {
			forgetTouchedParam(content);
			removeChild(content);
			delete content;
			content = nullptr;
		}
		owner = nullptr;
		ownerProvider = nullptr;
	}

	void step() override {
		// Checked before the children step: the content's controls point at the editor module
		app::ModuleWidget* const editor = open && enabled() ? findEditor() : nullptr;
		if (!editor) {
			open = false;
			dropContent();
			hide();
			return;
		}
		if (editor != owner) {
			dropContent();
			owner = editor;
			ownerProvider = dynamic_cast<EditorProvider*>(editor);
			content = ownerProvider->createEditorView();
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
			showEditor(false);
			e.consume(this);
		}
	}
};

static EditorView* currentView() {
	if (APP == nullptr || APP->scene == nullptr)
		return nullptr;
	for (widget::Widget* w : APP->scene->children)
		if (EditorView* const v = dynamic_cast<EditorView*>(w))
			return v;
	return nullptr;
}

void editorProviderGone(EditorProvider* provider) {
	// Every view alive in the process (one per plugin instance); not through the scene, which may be mid-teardown
	for (EditorView* const v : allViews())
		if (provider && v->ownerProvider == provider)
			v->dropContent();
}

void showEditor(bool show) {
	EditorView* const v = currentView();
	if (!v)
		return;
	v->open = show && enabled() && findEditor() != nullptr;
	if (v->open)
		showMixer(false);
}

bool editorShown() {
	EditorView* const v = currentView();
	return v != nullptr && v->open;
}

widget::Widget* createEditorView() {
	EditorView* const v = new EditorView;
	v->hide();
	return v;
}

} // namespace patchcore
} // namespace rack
