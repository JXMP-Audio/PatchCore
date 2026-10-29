/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Quick search: a compact module finder that opens at the mouse (right-click on the rack or Enter).
 * Type to search; "@brand" and "#purpose" filter (case, spaces and dashes ignored), e.g. "vco@jxmp".
 * Arrows + Enter or a click add the module where the finder was opened; Esc or a click outside closes it.
 */

#include "PatchCore.hpp"
#include "QuickSearchLogic.hpp"
#include <patchcore/Mixer.hpp>

#include <algorithm>
#include <map>
#include <set>

#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <helpers.hpp>
#include <plugin.hpp>
#include <plugin/Model.hpp>
#include <plugin/Plugin.hpp>
#include <settings.hpp>
#include <string.hpp>
#include <tag.hpp>
#include <ui/Menu.hpp>
#include <ui/MenuItem.hpp>
#include <ui/TextField.hpp>
#include <widget/OpaqueWidget.hpp>

namespace rack {
namespace patchcore {


static constexpr const float kWidth = 360.f;
static constexpr const float kFieldHeight = 30.f;
static constexpr const float kFilterHeight = 24.f;
static constexpr const float kRowHeight = 36.f;
static constexpr const int kVisibleRows = 9;
static constexpr const float kPad = 8.f;


/** Width in HP and panel colour of a model, measured once from a widget without a module */
struct QsMeta {
	int hp = 0;
	NVGcolor color = nvgRGB(0x4a, 0x4d, 0x54);
};

static const QsMeta& qsMeta(plugin::Model* const model) {
	static std::map<plugin::Model*, QsMeta> cache;
	std::map<plugin::Model*, QsMeta>::iterator it = cache.find(model);
	if (it != cache.end())
		return it->second;
	QsMeta meta;
	try {
		app::ModuleWidget* const mw = model->createModuleWidget(NULL);
		meta.hp = (int) std::round(mw->box.size.x / app::RACK_GRID_WIDTH);
		meta.color = panelColor(mw);
		delete mw;
	}
	catch (Exception& e) {
		WARN("PatchCore: could not measure %s: %s", model->getFullName().c_str(), e.what());
	}
	return cache.emplace(model, meta).first->second;
}


struct QuickSearch;

struct QsField : ui::TextField {
	QuickSearch* qs = nullptr;
	void onSelectKey(const SelectKeyEvent& e) override;
	void onChange(const ChangeEvent& e) override;
};

struct QsFilterButton : widget::OpaqueWidget {
	QuickSearch* qs = nullptr;
	bool brand = false;
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
};

struct QsList : widget::OpaqueWidget {
	QuickSearch* qs = nullptr;
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	void onHover(const HoverEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;
};


/** Full-scene overlay; the finder panel sits at the mouse. A click outside the panel closes it. */
struct QuickSearch : widget::OpaqueWidget {
	widget::Widget* panel = nullptr;
	QsField* field = nullptr;
	QsFilterButton* purposeButton = nullptr;
	QsFilterButton* brandButton = nullptr;
	QsList* list = nullptr;

	math::Vec rackPos;
	std::vector<plugin::Model*> models;
	std::vector<qs::Entry> entries;
	struct Row {
		plugin::Model* model;
		qs::Section section;
		bool header;
	};
	std::vector<Row> rows;
	int selected = 0;
	int scroll = 0;
	int hovered = -1;

	QuickSearch() {
		panel = new widget::OpaqueWidget;
		panel->box.size = math::Vec(kWidth, kPad * 3 + kFieldHeight + kFilterHeight + kRowHeight * kVisibleRows + 18.f);
		addChild(panel);

		field = new QsField;
		field->qs = this;
		field->placeholder = "Search modules   ·   @brand   #purpose";
		field->box = math::Rect(kPad, kPad, kWidth - 2 * kPad, kFieldHeight);
		panel->addChild(field);

		purposeButton = new QsFilterButton;
		purposeButton->qs = this;
		purposeButton->box = math::Rect(kPad, kPad * 2 + kFieldHeight, 120.f, kFilterHeight);
		panel->addChild(purposeButton);

		brandButton = new QsFilterButton;
		brandButton->qs = this;
		brandButton->brand = true;
		brandButton->box = math::Rect(kPad + 128.f, kPad * 2 + kFieldHeight, 120.f, kFilterHeight);
		panel->addChild(brandButton);

		list = new QsList;
		list->qs = this;
		list->box = math::Rect(0, kPad * 3 + kFieldHeight + kFilterHeight, kWidth, kRowHeight * kVisibleRows);
		panel->addChild(list);
	}

	void collect() {
		models.clear();
		entries.clear();
		for (plugin::Plugin* const p : plugin::plugins) {
			for (plugin::Model* const m : p->models) {
				qs::Entry e;
				e.brand = p->brand;
				for (const int id : m->tagIds)
					if (id >= 0 && id < (int) tag::tagAliases.size())
						for (const std::string& alias : tag::tagAliases[id])
							e.tags.push_back(alias);
				const rack::settings::ModuleInfo* const mi = rack::settings::getModuleInfo(p->slug, m->slug);
				e.favorite = mi && mi->favorite;
				e.lastAdded = mi ? mi->lastAdded : 0.0;
				e.ours = p->slug == "JXMP";
				models.push_back(m);
				entries.push_back(e);
			}
		}
	}

	int indexOf(plugin::Model* const m) const {
		for (size_t i = 0; i < models.size(); ++i)
			if (models[i] == m)
				return (int) i;
		return -1;
	}

	void refresh() {
		const qs::Query q = qs::parseQuery(field->text);
		rows.clear();
		if (q.text.empty()) {
			int lastSection = -1;
			for (const qs::Item& it : qs::emptyQueryOrder(entries, q.tag, q.brand, 8)) {
				if (it.section != lastSection) {
					rows.push_back({nullptr, it.section, true});
					lastSection = it.section;
				}
				rows.push_back({models[it.index], it.section, false});
			}
		}
		else {
			for (plugin::Model* const m : searchModels(q.text)) {
				const int i = indexOf(m);
				if (i >= 0 && qs::passes(entries[i], q.tag, q.brand))
					rows.push_back({m, qs::SECTION_ALL, false});
				if (rows.size() >= 200)
					break;
			}
		}
		selected = firstSelectable(0, 1);
		scroll = 0;
	}

	int firstSelectable(int from, int dir) const {
		for (int i = from; i >= 0 && i < (int) rows.size(); i += dir)
			if (!rows[i].header)
				return i;
		return -1;
	}

	void moveSelection(int dir) {
		if (rows.empty())
			return;
		const int next = firstSelectable(selected + dir, dir);
		if (next >= 0)
			selected = next;
		if (selected < scroll)
			scroll = std::max(0, selected - (selected > 0 && rows[selected - 1].header ? 1 : 0));
		if (selected >= scroll + kVisibleRows)
			scroll = selected - kVisibleRows + 1;
	}

	void add(int row) {
		if (row < 0 || row >= (int) rows.size() || rows[row].header)
			return;
		plugin::Model* const model = rows[row].model;
		// Only one console (and other one-per-project modules) in the project
		if (singletonTaken(model))
			return;
		hide();
		addModelAt(model, rackPos);
	}

	/** Adds or replaces a "#purpose" / "@brand" part of the query. */
	void setFilter(bool brand, const std::string& value) {
		const qs::Query q = qs::parseQuery(field->text);
		std::string text = q.text;
		std::string b = brand ? value : q.brand;
		std::string t = brand ? q.tag : value;
		if (!t.empty())
			text += " #" + t;
		if (!b.empty())
			text += " @" + b;
		field->setText(qs::trim(text));
		field->cursor = field->selection = (int) field->text.size();
		refresh();
		APP->event->setSelectedWidget(field);
	}

	void onShow(const ShowEvent& e) override {
		OpaqueWidget::onShow(e);
		box.size = parent ? parent->box.size : box.size;
		rackPos = APP->scene->rack->getMousePos();
		const math::Vec mouse = APP->scene->getMousePos();
		panel->box.pos = mouse.minus(math::Vec(20.f, 16.f));
		panel->box.pos.x = math::clamp(panel->box.pos.x, 4.f, box.size.x - panel->box.size.x - 4.f);
		panel->box.pos.y = math::clamp(panel->box.pos.y, 4.f, box.size.y - panel->box.size.y - 4.f);
		collect();
		field->setText("");
		refresh();
		APP->event->setSelectedWidget(field);
	}

	void step() override {
		if (parent)
			box.size = parent->box.size;
		OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		const Theme& t = theme();
		NVGcontext* const vg = args.vg;
		const math::Rect r = panel->box;
		// Shadow and body
		nvgBeginPath(vg);
		nvgRoundedRect(vg, r.pos.x - 6.f, r.pos.y - 2.f, r.size.x + 12.f, r.size.y + 14.f, 10.f);
		nvgFillPaint(vg, nvgBoxGradient(vg, r.pos.x, r.pos.y + 4.f, r.size.x, r.size.y, 6.f, 14.f, nvgRGBAf(0, 0, 0, 0.55f), nvgRGBAf(0, 0, 0, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(r), 6.f);
		nvgFillColor(vg, t.chrome1);
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, t.line);
		nvgStroke(vg);
		// Hint line at the bottom
		draw::text(vg, uiFont(), 10.5f, t.textFaint, math::Vec(r.pos.x + kPad, r.pos.y + r.size.y - 9.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           "\xE2\x86\x91\xE2\x86\x93 select   Enter add   Ctrl+click favourite   Esc close");
		OpaqueWidget::draw(args);
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		// A click outside the panel closes the finder
		if (!e.isConsumed() && e.action == GLFW_PRESS) {
			hide();
			e.consume(this);
		}
	}
};


void QsField::onSelectKey(const SelectKeyEvent& e) {
	if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
		if (e.key == GLFW_KEY_ESCAPE) {
			qs->hide();
			e.consume(this);
			return;
		}
		if (e.key == GLFW_KEY_DOWN || e.key == GLFW_KEY_UP) {
			qs->moveSelection(e.key == GLFW_KEY_DOWN ? 1 : -1);
			e.consume(this);
			return;
		}
		if (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) {
			qs->add(qs->selected);
			e.consume(this);
			return;
		}
	}
	ui::TextField::onSelectKey(e);
}

void QsField::onChange(const ChangeEvent& e) {
	qs->refresh();
}


void QsFilterButton::draw(const DrawArgs& args) {
	const Theme& t = theme();
	const qs::Query q = qs::parseQuery(qs->field->text);
	const std::string value = brand ? q.brand : q.tag;
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, box.size.y / 2);
	nvgFillColor(args.vg, value.empty() ? t.chrome2 : color::alpha(t.accent, 0.25f));
	nvgFill(args.vg);
	nvgStrokeWidth(args.vg, 1.f);
	nvgStrokeColor(args.vg, value.empty() ? t.line : t.accent);
	nvgStroke(args.vg);
	const std::string label = value.empty() ? std::string(brand ? "Brand \xE2\x96\xBE" : "Purpose \xE2\x96\xBE")
	                                        : std::string(brand ? "@" : "#") + value + "  \xC3\x97";
	nvgSave(args.vg);
	nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
	draw::text(args.vg, uiFont(), 11.5f, value.empty() ? t.textDim : t.text, box.size.div(2), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
	nvgRestore(args.vg);
}

void QsFilterButton::onButton(const ButtonEvent& e) {
	if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	e.consume(this);
	const qs::Query q = qs::parseQuery(qs->field->text);
	if (!(brand ? q.brand : q.tag).empty()) {
		// The × of an active filter clears it
		qs->setFilter(brand, "");
		return;
	}
	// Menu of the values that exist among the installed modules, most used first
	std::map<std::string, int> counts;
	for (size_t i = 0; i < qs->models.size(); ++i) {
		if (brand)
			counts[qs->models[i]->plugin->brand]++;
		else
			for (const int id : qs->models[i]->tagIds)
				counts[tag::getTag(id)]++;
	}
	std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
		if (brand)
			return a.first < b.first;
		return a.second > b.second;
	});
	ui::Menu* const menu = createMenu();
	menu->addChild(createMenuLabel(brand ? "Brand" : "Purpose"));
	QuickSearch* const q2 = qs;
	const bool isBrand = brand;
	for (const std::pair<std::string, int>& entry : sorted) {
		const std::string name = entry.first;
		menu->addChild(createMenuItem(name, string::f("%d", entry.second), [=]() { q2->setFilter(isBrand, qs::normalize(name)); }));
	}
}


void QsList::draw(const DrawArgs& args) {
	const Theme& t = theme();
	NVGcontext* const vg = args.vg;
	nvgSave(vg);
	nvgScissor(vg, 0, 0, box.size.x, box.size.y);
	if (qs->rows.empty())
		draw::text(vg, uiFont(), 12.f, t.textFaint, math::Vec(box.size.x / 2, 30.f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "No modules found");
	static const char* const titles[4] = {"RECENT", "FAVOURITES", "JXMP AUDIO", "ALL MATCHING"};
	for (int i = qs->scroll; i < (int) qs->rows.size() && i < qs->scroll + kVisibleRows; ++i) {
		const QuickSearch::Row& row = qs->rows[i];
		const float y = (i - qs->scroll) * kRowHeight;
		if (row.header) {
			draw::text(vg, labelFont(), 10.f, t.accent, math::Vec(kPad + 2.f, y + kRowHeight - 10.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
			           titles[row.section], 1.5f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, kPad + 90.f, y + kRowHeight - 10.f);
			nvgLineTo(vg, box.size.x - kPad, y + kRowHeight - 10.f);
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, t.lineSoft);
			nvgStroke(vg);
			continue;
		}
		if (i == qs->selected || i == qs->hovered) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 4.f, y + 1.f, box.size.x - 8.f, kRowHeight - 2.f, 4.f);
			nvgFillColor(vg, i == qs->selected ? color::alpha(t.accent, 0.22f) : t.chrome2);
			nvgFill(vg);
		}
		// Thumbnail: panel colour with two knobs
		const QsMeta& meta = qsMeta(row.model);
		const math::Rect thumb(kPad + 2.f, y + 4.f, 18.f, kRowHeight - 8.f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(thumb), 2.f);
		nvgFillColor(vg, meta.color);
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 0x60));
		nvgStroke(vg);
		const bool light = meta.color.r * 0.299f + meta.color.g * 0.587f + meta.color.b * 0.114f > 0.55f;
		nvgBeginPath(vg);
		nvgCircle(vg, thumb.pos.x + 9.f, thumb.pos.y + 9.f, 3.5f);
		nvgCircle(vg, thumb.pos.x + 9.f, thumb.pos.y + 19.f, 2.5f);
		nvgFillColor(vg, light ? nvgRGB(0x22, 0x22, 0x22) : nvgRGB(0xd8, 0xd8, 0xd8));
		nvgFill(vg);

		const bool fav = row.model->isFavorite();
		draw::text(vg, uiFont(), 12.5f, t.text, math::Vec(kPad + 30.f, y + 12.5f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           (fav ? "\xE2\x98\x85 " : "") + row.model->name);
		std::string metaText = row.model->plugin->brand;
		if (meta.hp > 0)
			metaText += string::f(" · %d HP", meta.hp);
		if (!row.model->tagIds.empty())
			metaText += " · " + tag::getTag(row.model->tagIds.front());
		const bool taken = singletonTaken(row.model);
		if (taken)
			metaText += " · already in the project";
		draw::text(vg, uiFont(), 10.5f, taken ? t.rec : t.textFaint, math::Vec(kPad + 30.f, y + 26.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, metaText);
	}
	nvgRestore(vg);
}

void QsList::onHover(const HoverEvent& e) {
	OpaqueWidget::onHover(e);
	qs->hovered = qs->scroll + (int) (e.pos.y / kRowHeight);
}

void QsList::onHoverScroll(const HoverScrollEvent& e) {
	const int maxScroll = std::max(0, (int) qs->rows.size() - kVisibleRows);
	qs->scroll = math::clamp(qs->scroll + (e.scrollDelta.y < 0 ? 2 : -2), 0, maxScroll);
	e.consume(this);
}

void QsList::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	const int row = qs->scroll + (int) (e.pos.y / kRowHeight);
	e.consume(this);
	if (row < 0 || row >= (int) qs->rows.size() || qs->rows[row].header)
		return;
	if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
		qs->rows[row].model->setFavorite(!qs->rows[row].model->isFavorite());
		return;
	}
	qs->add(row);
}


widget::Widget* createQuickSearch() {
	return new QuickSearch;
}


} // namespace patchcore
} // namespace rack
