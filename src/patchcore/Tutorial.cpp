/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Interactive tutorials (research_docs/15): the step panel, the highlight around the target, Show me, and the
// one-time "New to modular?" offer. The lesson logic is in TutorialLogic.hpp; this file only reads the rack into
// a RackState every frame and acts on it.

#include <app/CableWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <engine/Engine.hpp>
#include <helpers.hpp>
#include <history.hpp>
#include <logger.hpp>
#include <patch.hpp>
#include <plugin.hpp>
#include <string.hpp>
#include <system.hpp>
#include <widget/OpaqueWidget.hpp>
#include <window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

#include "../CardinalCommon.hpp"
#include "../patchcore/PatchCore.hpp"
#include <patchcore/Mixer.hpp>

#include "TutorialLogic.hpp"

namespace rack {
namespace patchcore {

namespace tu = ::patchcore_tutorial;


static std::string tutorialsDir() {
	plugin::Plugin* const p = plugin::getPlugin("JXMP");
	return p ? system::join(p->path, "res/tutorials") : "";
}

static std::string readFile(const std::string& path) {
	std::ifstream f(path.c_str());
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static bool loadLesson(const std::string& id, tu::Lesson* out, std::string* error) {
	return tu::parseLesson(readFile(system::join(tutorialsDir(), id, "lesson.json")), out, error);
}

static bool isDone(const std::string& id) {
	for (const std::string& d : settings.tutorialsDone)
		if (d == id)
			return true;
	return false;
}


std::vector<TutorialInfo> tutorialList() {
	std::vector<TutorialInfo> list;
	std::vector<tu::IndexEntry> index;
	std::string error;
	if (!tu::parseIndex(readFile(system::join(tutorialsDir(), "index.json")), &index, &error)) {
		WARN("PatchCore tutorials: index.json: %s", error.c_str());
		return list;
	}
	for (const tu::IndexEntry& e : index) {
		tu::Lesson l;
		TutorialInfo info;
		info.id = e.id;
		info.title = e.title;
		info.description = e.description;
		info.available = loadLesson(e.id, &l, &error);
		if (!info.available)
			WARN("PatchCore tutorials: %s: %s", e.id.c_str(), error.c_str());
		info.done = isDone(e.id);
		list.push_back(info);
	}
	return list;
}


// ---------------------------------------------------------------------------------------------------------
// Rack snapshot

struct PeakHold {
	float peak = 0.f;
	double at = 0.0;
};

static float portPeak(engine::Port& port, PeakHold& hold, double t) {
	float v = 0.f;
	for (int c = 0; c < port.getChannels(); ++c)
		v = std::max(v, std::fabs(port.getVoltage(c)));
	if (v >= hold.peak || t - hold.at > tu::Runner::kHold) {
		hold.peak = v;
		hold.at = t;
	}
	return hold.peak;
}

static int sideOf(app::ModuleWidget* mw, bool output, int index) {
	if (mw == nullptr)
		return tu::SIDE_FRONT;
	app::PortWidget* const pw = output ? mw->getOutput(index) : mw->getInput(index);
	return pw != nullptr && portSide(pw) == SIDE_BACK ? tu::SIDE_BACK : tu::SIDE_FRONT;
}

static tu::RackState snapshot(std::map<std::string, PeakHold>& peaks, double t) {
	tu::RackState s;
	s.side = isBackView() ? tu::SIDE_BACK : tu::SIDE_FRONT;
	for (int64_t id : APP->engine->getModuleIds()) {
		engine::Module* const m = APP->engine->getModule(id);
		if (m == nullptr || m->model == nullptr)
			continue;
		tu::ModuleState ms;
		ms.id = id;
		ms.slug = m->model->slug;
		ms.plugin = m->model->plugin ? m->model->plugin->slug : "";
		s.modules.push_back(ms);
		app::ModuleWidget* const mw = APP->scene->rack->getModule(id);
		for (int out = 0; out < 2; ++out) {
			// inputs and outputs are vectors of different types (Input / Output), both Ports
			const int n = (int) (out ? m->outputs.size() : m->inputs.size());
			for (int i = 0; i < n; ++i) {
				engine::Port& port = out ? (engine::Port&) m->outputs[i] : (engine::Port&) m->inputs[i];
				engine::PortInfo* const info = out ? m->outputInfos[i] : m->inputInfos[i];
				tu::PortState p;
				p.moduleId = id;
				p.output = out != 0;
				p.index = i;
				p.name = info ? info->name : "";
				p.side = sideOf(mw, p.output, i);
				p.peak = portPeak(port, peaks[std::to_string(id) + (out ? "/o" : "/i") + std::to_string(i)], t);
				s.ports.push_back(p);
			}
		}
		for (int i = 0; i < (int) m->paramQuantities.size(); ++i) {
			engine::ParamQuantity* const pq = m->paramQuantities[i];
			if (pq == nullptr)
				continue;
			tu::ParamState p;
			p.moduleId = id;
			p.index = i;
			p.name = pq->name;
			p.display = pq->getDisplayValue();
			p.scaled = pq->getScaledValue();
			s.params.push_back(p);
		}
	}
	for (int64_t cid : APP->engine->getCableIds()) {
		engine::Cable* const c = APP->engine->getCable(cid);
		if (c == nullptr || c->inputModule == nullptr || c->outputModule == nullptr)
			continue;
		tu::CableState k;
		k.outModule = c->outputModule->id;
		k.outIndex = c->outputId;
		k.inModule = c->inputModule->id;
		k.inIndex = c->inputId;
		s.cables.push_back(k);
	}
	return s;
}


// ---------------------------------------------------------------------------------------------------------
// From references to widgets, and Show me

static widget::Widget* refWidget(const tu::Ref& r, const tu::RackState& s) {
	const tu::ModuleState* const m = tu::findModule(s, r.module);
	if (m == nullptr)
		return nullptr;
	app::ModuleWidget* const mw = APP->scene->rack->getModule(m->id);
	if (mw == nullptr)
		return nullptr;
	if (r.kind == tu::Ref::MODULE)
		return mw;
	if (r.kind == tu::Ref::PARAM) {
		const tu::ParamState* const p = tu::findParam(s, r);
		return p ? mw->getParam(p->index) : nullptr;
	}
	const tu::PortState* const p = tu::findPort(s, r);
	if (p == nullptr)
		return nullptr;
	return p->output ? (widget::Widget*) mw->getOutput(p->index) : (widget::Widget*) mw->getInput(p->index);
}

static plugin::Model* findModel(const std::string& slug) {
	if (plugin::Model* const m = plugin::getModel("JXMP", slug))
		return m;
	return plugin::getModel("Cardinal", slug);
}

/** Right of the rightmost module, in the same row */
static math::Vec freeSpot() {
	math::Vec pos(0.f, 0.f);
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (mw->box.getRight() > pos.x)
			pos = math::Vec(mw->box.getRight() + RACK_GRID_WIDTH, mw->box.pos.y);
	}
	return pos;
}

static void perform(const tu::Action& a, std::map<std::string, PeakHold>& peaks, double t) {
	// Each action reads the rack again: a module added by the previous action must be found
	const tu::RackState s = snapshot(peaks, t);
	app::RackWidget* const rack = APP->scene->rack;
	switch (a.type) {
		case tu::Action::SET_SIDE:
			setViewSide(a.side == tu::SIDE_BACK ? VIEW_BACK : VIEW_FRONT);
			break;
		case tu::Action::ADD_MODULE:
			if (plugin::Model* const model = findModel(a.module))
				addModelAt(model, freeSpot());  // pushes its own history action
			break;
		case tu::Action::ADD_CABLE: {
			const tu::PortState* const from = tu::findPort(s, a.from);
			const tu::PortState* const to = tu::findPort(s, a.to);
			app::ModuleWidget* const outMw = from ? rack->getModule(from->moduleId) : nullptr;
			app::ModuleWidget* const inMw = to ? rack->getModule(to->moduleId) : nullptr;
			if (outMw == nullptr || inMw == nullptr)
				break;
			history::ComplexAction* const complex = new history::ComplexAction;
			complex->name = "show me: patch cable";
			// An input holds one cable: the old one goes first
			for (app::CableWidget* old : rack->getCompleteCablesOnPort(inMw->getInput(to->index))) {
				history::CableRemove* const h = new history::CableRemove;
				h->setCable(old);
				complex->push(h);
				rack->removeCable(old);
				delete old;
			}
			engine::Cable* const cable = new engine::Cable;
			cable->outputModule = outMw->module;
			cable->outputId = from->index;
			cable->inputModule = inMw->module;
			cable->inputId = to->index;
			APP->engine->addCable(cable);
			app::CableWidget* const cw = new app::CableWidget;
			cw->setCable(cable);
			cw->color = rack->getNextCableColor();
			rack->addCable(cw);
			history::CableAdd* const h = new history::CableAdd;
			h->setCable(cw);
			complex->push(h);
			APP->history->push(complex);
			break;
		}
		case tu::Action::SET_PARAM:
		case tu::Action::MOVE_PARAM: {
			const tu::ParamState* const p = tu::findParam(s, a.param);
			engine::Module* const m = p ? APP->engine->getModule(p->moduleId) : nullptr;
			engine::ParamQuantity* const pq = m ? m->paramQuantities[p->index] : nullptr;
			if (pq == nullptr)
				break;
			const float oldValue = pq->getValue();
			if (a.type == tu::Action::SET_PARAM) {
				pq->setDisplayValue((float) a.value);
				// The display <-> value conversion can land just outside the range: nudge it in
				for (int k = 0; k < 50; ++k) {
					const float d = pq->getDisplayValue();
					const int dir = a.hasMin && d < a.min ? 1 : a.hasMax && d > a.max ? -1 : 0;
					if (dir == 0)
						break;
					if (pq->snapEnabled)
						pq->setValue(pq->getValue() + dir);
					else
						pq->setScaledValue(pq->getScaledValue() + dir * 0.002f);
				}
			}
			else {
				const float cur = pq->getScaledValue();
				pq->setScaledValue(cur + a.value <= 1.f ? cur + (float) a.value : cur - (float) a.value);
			}
			history::ParamChange* const h = new history::ParamChange;
			h->name = "show me: turn knob";
			h->moduleId = p->moduleId;
			h->paramId = p->index;
			h->oldValue = oldValue;
			h->newValue = pq->getValue();
			APP->history->push(h);
			break;
		}
	}
}


// ---------------------------------------------------------------------------------------------------------
// The layer: panel + highlight + first-start offer

struct TutorialLayer;
/** The tutorial layer of the current scene (each plugin instance has its own scene and layer) */
static TutorialLayer* currentLayer();

struct TutorialPanel : widget::OpaqueWidget {
	TutorialLayer* layer;
	math::Rect backRect, nextRect, exitRect, showMeRect;
	bool dragging = false;
	bool placed = false;

	static constexpr float kW = 340.f, kH = 200.f;

	TutorialPanel(TutorialLayer* l) : layer(l) {
		box.size = math::Vec(kW, kH);
		const float by = kH - 40.f;
		backRect = math::Rect(16.f, by, 70.f, 28.f);
		showMeRect = math::Rect(96.f, by, 80.f, 28.f);
		exitRect = math::Rect(kW - 166.f, by, 70.f, 28.f);
		nextRect = math::Rect(kW - 86.f, by, 70.f, 28.f);
	}

	void onButton(const ButtonEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override {
		if (dragging)
			box.pos = box.pos.plus(e.mouseDelta);
	}
	void onDragEnd(const DragEndEvent& e) override {
		dragging = false;
	}
	void draw(const DrawArgs& args) override;
};

struct TutorialLayer : widget::Widget {
	tu::Lesson lesson;
	tu::Runner runner;
	std::string lessonId;
	bool active = false;
	std::vector<tu::ModuleState> startModules;
	/** The lesson after this one ("" for the last), read once when the lesson begins */
	std::string nextId;
	/** runner.finishCount already recorded */
	int finishSeen = 0;
	std::map<std::string, PeakHold> peaks;
	tu::RackState state;
	TutorialPanel* panel;
	int scrolledStep = -1;
	/** First-start offer strip and its two buttons */
	math::Rect offerStart, offerLater, offerBox;

	TutorialLayer() {
		panel = new TutorialPanel(this);
		panel->hide();
		addChild(panel);
	}

	double now() const {
		return system::getTime();
	}

	void begin(const std::string& id, const tu::Lesson& l) {
		const std::vector<tu::ModuleState> loaded = snapshot(peaks, now()).modules;
		if (loaded.empty()) {
			// The start patch did not load (the load reported its error already)
			WARN("PatchCore tutorials: %s: the start patch is empty or did not load", id.c_str());
			return;
		}
		lessonId = id;
		lesson = l;
		active = true;
		peaks.clear();
		startModules = loaded;
		nextId = findNextLessonId();
		finishSeen = 0;
		setViewSide(lesson.startSide == tu::SIDE_BACK ? VIEW_BACK : VIEW_FRONT);
		runner.start(lesson, now());
		scrolledStep = -1;
		panel->placed = false;
		panel->show();
	}

	void end() {
		active = false;
		panel->hide();
	}

	void finishLesson() {
		if (!isDone(lessonId)) {
			settings.tutorialsDone.push_back(lessonId);
			saveSettings();
		}
	}

	/** The lesson after this one, "" if it is the last (reads index.json and the lessons: not every frame) */
	std::string findNextLessonId() const {
		const std::vector<TutorialInfo> list = tutorialList();
		for (size_t i = 0; i + 1 < list.size(); ++i)
			if (list[i].id == lessonId)
				return list[i + 1].id;
		return "";
	}

	void showMe() {
		if (runner.phase != tu::Runner::ACTIVE)
			return;
		const std::vector<tu::Action> actions = tu::showMeActions(runner.current(), state);
		for (const tu::Action& a : actions)
			perform(a, peaks, now());
	}

	bool offerVisible() const {
		return !settings.tutorialOffered && !active;
	}

	void step() override {
		if (parent != nullptr)
			box = math::Rect(math::Vec(), parent->box.size);
		if (!panel->placed && box.size.x > 0.f) {
			panel->box.pos = box.size.minus(panel->box.size).minus(math::Vec(24.f, 60.f));
			panel->placed = true;
		}
		if (active) {
			const double t = now();
			state = snapshot(peaks, t);
			if (tu::patchReplaced(startModules, state)) {
				end();
			}
			else {
				runner.update(state, t);
				// The lesson ends through Next on its summary step (in the panel's click) or here
				if (runner.finishCount != finishSeen) {
					finishSeen = runner.finishCount;
					finishLesson();
				}
				scrollToTarget();
			}
		}
		// The offer sits under the top bar (and under the audio notice when one shows)
		float y = APP->scene->menuBar != nullptr ? APP->scene->menuBar->box.size.y : 0.f;
		if (!audioNotice().empty())
			y += 26.f;
		offerBox = math::Rect(0.f, y, box.size.x, 26.f);
		offerStart = math::Rect(box.size.x - 190.f, y + 3.f, 80.f, 20.f);
		offerLater = math::Rect(box.size.x - 100.f, y + 3.f, 80.f, 20.f);
		Widget::step();
	}

	/** Once per step: a reference to a module that is there but has no such port or param (renamed?) goes to the log */
	void logUnresolved(const tu::Step& st) {
		std::vector<tu::Ref> refs = st.highlight;
		collectRefs(st.done, &refs);
		for (const tu::Ref& r : refs) {
			if (r.kind == tu::Ref::MODULE || tu::findModule(state, r.module) == nullptr)
				continue;
			const bool found = r.kind == tu::Ref::PARAM ? tu::findParam(state, r) != nullptr : tu::findPort(state, r) != nullptr;
			if (!found)
				WARN("PatchCore tutorials: %s step %d: %s has no %s '%s'", lessonId.c_str(), runner.step + 1, r.module.c_str(),
				     r.kind == tu::Ref::PARAM ? "param" : r.kind == tu::Ref::INPUT ? "input" : "output", r.name.c_str());
		}
	}

	static void collectRefs(const tu::Condition& c, std::vector<tu::Ref>* out) {
		if (c.type == tu::Cond::Param || c.type == tu::Cond::Signal || (c.type == tu::Cond::Cable && c.hasFrom))
			out->push_back(c.ref);
		if (c.type == tu::Cond::Cable && c.hasTo)
			out->push_back(c.to);
		for (const tu::Condition& part : c.parts)
			collectRefs(part, out);
	}

	/** Once per step: bring the first target into view */
	void scrollToTarget() {
		if (runner.phase != tu::Runner::ACTIVE || scrolledStep == runner.step)
			return;
		scrolledStep = runner.step;
		const tu::Step& st = runner.current();
		logUnresolved(st);
		if (st.highlight.empty())
			return;
		widget::Widget* const w = refWidget(st.highlight[0], state);
		app::ModuleWidget* mw = dynamic_cast<app::ModuleWidget*>(w);
		if (mw == nullptr && w != nullptr)
			mw = w->getAncestorOfType<app::ModuleWidget>();
		if (mw == nullptr)
			return;
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		const math::Vec a = mw->getRelativeOffset(math::Vec(), this);
		const math::Vec b = mw->getRelativeOffset(mw->box.size, this);
		const math::Rect view = scroll->box;
		if (a.x >= view.pos.x && b.x <= view.getRight() && a.y >= view.pos.y && b.y <= view.getBottom())
			return;
		scroll->setGridOffset(mw->box.pos.minus(RACK_OFFSET).div(RACK_GRID_SIZE).minus(math::Vec(4.f, 0.f)));
	}

	void onButton(const ButtonEvent& e) override {
		if (offerVisible() && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && offerBox.contains(e.pos)) {
			if (offerStart.contains(e.pos)) {
				settings.tutorialOffered = true;
				saveSettings();
				const std::vector<TutorialInfo> list = tutorialList();
				if (!list.empty())
					openTutorial(list[0].id);
			}
			else if (offerLater.contains(e.pos)) {
				settings.tutorialOffered = true;
				saveSettings();
			}
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}

	void drawHighlight(NVGcontext* vg) {
		if (!active || runner.phase == tu::Runner::FINISHED || mixerShown())
			return;
		const math::Rect clip = APP->scene->rackScroll->box;
		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(clip));
		const float pulse = 0.4f + 0.6f * (0.5f + 0.5f * std::sin((float) (now() * 2.0 * M_PI * 1.5)));
		for (const tu::Ref& r : runner.current().highlight) {
			widget::Widget* const w = refWidget(r, state);
			if (w == nullptr || !w->isVisible())
				continue;
			const math::Vec a = w->getRelativeOffset(math::Vec(), this);
			const math::Vec b = w->getRelativeOffset(w->box.size, this);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, a.x - 3.f, a.y - 3.f, b.x - a.x + 6.f, b.y - a.y + 6.f, 5.f);
			nvgStrokeWidth(vg, 2.f);
			nvgStrokeColor(vg, nvgRGBAf(0.94f, 0.6f, 0.24f, pulse));
			nvgStroke(vg);
		}
		nvgRestore(vg);
	}

	void drawOffer(NVGcontext* vg) {
		if (!offerVisible())
			return;
		nvgBeginPath(vg);
		nvgRect(vg, RECT_ARGS(offerBox));
		nvgFillColor(vg, nvgRGB(0x1f, 0x3a, 0x2a));
		nvgFill(vg);
		const NVGcolor fg = nvgRGB(0xbf, 0xf0, 0xcf);
		draw::text(vg, uiFont(), 12.f, fg, math::Vec(16.f, offerBox.getCenter().y), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           "New to modular? Start the first lesson and patch your first sound.");
		draw::bevelBox(vg, offerStart, 4.f, nvgRGB(0xf0, 0x9a, 0x3c), nvgRGB(0xc8, 0x76, 0x22), nvgRGB(0, 0, 0));
		draw::text(vg, uiFont(), 11.f, nvgRGB(0x16, 0x12, 0x0c), offerStart.getCenter(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "Start");
		draw::text(vg, uiFont(), 11.f, fg, offerLater.getCenter(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "Not now");
	}

	void draw(const DrawArgs& args) override {
		drawHighlight(args.vg);
		drawOffer(args.vg);
		Widget::draw(args);
	}
};


void TutorialPanel::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	e.consume(this);
	tu::Runner& r = layer->runner;
	const double t = layer->now();
	if (exitRect.contains(e.pos)) {
		layer->end();
		return;
	}
	if (backRect.contains(e.pos)) {
		r.back(t);
		return;
	}
	if (nextRect.contains(e.pos)) {
		if (r.phase == tu::Runner::FINISHED) {
			const std::string next = layer->nextId;
			layer->end();
			if (!next.empty())
				openTutorial(next);
			return;
		}
		r.ctx.nextPressed = true;
		r.pressNext(layer->state, t);
		return;
	}
	if (showMeRect.contains(e.pos) && r.phase == tu::Runner::ACTIVE && tu::canShowMe(r.current())) {
		layer->showMe();
		return;
	}
	// Anywhere else on the panel: drag it
	dragging = true;
}


static void drawPanelButton(NVGcontext* vg, const math::Rect& r, const std::string& label, bool enabled, bool primary) {
	const Theme& t = theme();
	if (primary && enabled)
		draw::bevelBox(vg, r, 5.f, nvgRGB(0xf0, 0x9a, 0x3c), nvgRGB(0xc8, 0x76, 0x22), nvgRGB(0, 0, 0));
	else
		draw::bevelBox(vg, r, 5.f, nvgRGB(0x4a, 0x4d, 0x54), nvgRGB(0x2f, 0x32, 0x37), nvgRGB(0, 0, 0));
	draw::text(vg, uiFont(), 12.f, enabled ? (primary ? nvgRGB(0x16, 0x12, 0x0c) : t.text) : t.textFaint, r.getCenter(),
	           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
}


void TutorialPanel::draw(const DrawArgs& args) {
	NVGcontext* const vg = args.vg;
	const Theme& t = theme();
	const tu::Runner& r = layer->runner;
	const tu::Lesson& l = layer->lesson;

	draw::bevelBox(vg, box.zeroPos(), 8.f, t.chrome2, t.chrome1, nvgRGB(0, 0, 0));
	draw::text(vg, labelFont(), 11.f, t.textDim, math::Vec(16.f, 22.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, string::uppercase(l.title), 1.4f);
	const bool finished = r.phase == tu::Runner::FINISHED;
	if (!finished)
		draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(kW - 16.f, 22.f), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
		           string::f("Step %d / %d", r.step + 1, (int) l.steps.size()));

	// Body text: the step, the "gone" notice, or the summary
	std::string body;
	if (finished) {
		body = "You learned:";
		for (const std::string& s : l.learned)
			body += "\n\xE2\x80\xA2 " + s;
	}
	else {
		body = r.current().text;
		const std::vector<std::string> gone = tu::goneModules(r.current(), layer->state, r.seen);
		if (!gone.empty())
			body = "The " + gone[0] + " is gone \xE2\x80\x93 press Show me to put it back.";
	}
	nvgSave(vg);
	nvgFontFaceId(vg, uiFont()->handle);
	nvgFontSize(vg, 12.5f);
	nvgTextLineHeight(vg, 1.3f);
	nvgFillColor(vg, t.text);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgTextBox(vg, 16.f, 40.f, kW - 32.f, body.c_str(), nullptr);
	nvgRestore(vg);

	// Status line above the buttons
	std::string status;
	NVGcolor statusColor = t.textFaint;
	if (!finished && r.needsTurn(layer->state))
		status = "Press Tab to turn the rack around";
	else if (r.phase == tu::Runner::DONE) {
		status = "\xE2\x9C\x93 Done";
		statusColor = t.play;
	}
	else if (!finished && r.isTask() && !r.reviewing) {
		tu::StepContext probe = r.ctx;
		status = "Waiting for: " + tu::waitingFor(r.current().done, layer->state, probe, layer->now());
	}
	nvgSave(vg);
	nvgIntersectScissor(vg, 16.f, kH - 64.f, kW - 32.f, 20.f);
	draw::text(vg, uiFont(), 11.f, statusColor, math::Vec(16.f, kH - 54.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, status);
	nvgRestore(vg);

	drawPanelButton(vg, backRect, "Back", r.step > 0 || finished, false);
	if (!finished && tu::canShowMe(r.current()))
		drawPanelButton(vg, showMeRect, "Show me", r.phase == tu::Runner::ACTIVE, false);
	drawPanelButton(vg, exitRect, "Exit", true, false);
	if (finished)
		drawPanelButton(vg, nextRect, layer->nextId.empty() ? "Close" : "Next lesson", true, true);
	else
		drawPanelButton(vg, nextRect, "Next", r.nextEnabled(), true);

	OpaqueWidget::draw(args);
}


void openTutorial(const std::string& id) {
	tu::Lesson lesson;
	std::string error;
	if (!loadLesson(id, &lesson, &error)) {
		WARN("PatchCore tutorials: %s: %s", id.c_str(), error.c_str());
		return;
	}
	// A running lesson ends only when the new start patch is in (cancelling the save prompt keeps it)
	patchUtils::loadPathDialog(system::join(tutorialsDir(), id, "start.vcv"), true, [id, lesson]() {
		if (TutorialLayer* const layer = currentLayer())
			layer->begin(id, lesson);
	});
}


static TutorialLayer* currentLayer() {
	if (APP == nullptr || APP->scene == nullptr)
		return nullptr;
	for (widget::Widget* w : APP->scene->children)
		if (TutorialLayer* const layer = dynamic_cast<TutorialLayer*>(w))
			return layer;
	return nullptr;
}


widget::Widget* createTutorialLayer() {
	return new TutorialLayer;
}

} // namespace patchcore
} // namespace rack
