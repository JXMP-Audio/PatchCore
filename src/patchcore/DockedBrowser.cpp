/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include <app/ModuleWidget.hpp>
#include <app/RackScrollWidget.hpp>
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
#include <ui/ScrollWidget.hpp>
#include <ui/TextField.hpp>
#include <widget/FramebufferWidget.hpp>
#include <widget/OpaqueWidget.hpp>
#include <widget/ZoomWidget.hpp>

#ifdef NDEBUG
# undef DEBUG
#endif

#include "../CardinalCommon.hpp"

namespace rack {
namespace patchcore {


static constexpr const float kBrowserPad = 10.f;
static constexpr const float kRowHeight = 44.f;
static constexpr const float kPreviewHeight = 200.f;
static constexpr const size_t kMaxResults = 400;


/** Width in HP and panel color of a model, measured once from a widget without a module */
struct ModelMeta {
	int hp = 0;
	NVGcolor color = nvgRGB(0x4a, 0x4d, 0x54);
};


static const ModelMeta& modelMeta(plugin::Model* const model) {
	static std::map<plugin::Model*, ModelMeta> cache;
	std::map<plugin::Model*, ModelMeta>::iterator it = cache.find(model);
	if (it != cache.end())
		return it->second;

	ModelMeta meta;
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


static std::string firstTag(plugin::Model* const model) {
	return model->tagIds.empty() ? std::string() : tag::getTag(model->tagIds.front());
}


static bool isLight(NVGcolor c) {
	return c.r * 0.299f + c.g * 0.587f + c.b * 0.114f > 0.55f;
}


struct Chip {
	const char* label;
	const char* tagName;
	bool host;
};

static const std::vector<Chip> kChips = {
	{"All", nullptr, false},
	{"Oscillators", "Oscillator", false},
	{"Filters", "Filter", false},
	{"Envelopes", "Envelope generator", false},
	{"Modulation", "Low-frequency oscillator", false},
	{"Effects", "Effect", false},
	{"Sequencers", "Sequencer", false},
	{"Utilities", "Utility", false},
	{"Host", nullptr, true},
};


enum Location {
	LOCATION_ALL,
	LOCATION_FAVORITES,
	LOCATION_RECENT,
};


struct DockedBrowser;


struct SearchField : ui::TextField {
	DockedBrowser* browser;

	SearchField() {
		placeholder = "Search modules, tags, brands...";
	}

	// The inset background and magnifier are drawn by DockedBrowser, this only draws text and caret
	void draw(const DrawArgs& args) override {
		const Theme& t = theme();
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		const bool selected = this == APP->event->selectedWidget;
		const int begin = selected ? std::min(cursor, selection) : -1;
		const int end = selected ? std::max(cursor, selection) : -1;
		if (text.empty() && !selected)
			bndIconLabelCaret(args.vg, 0.0, 0.0, box.size.x, box.size.y, -1, t.textFaint, 13, placeholder.c_str(), t.accent, 0, -1);
		else
			bndIconLabelCaret(args.vg, 0.0, 0.0, box.size.x, box.size.y, -1, t.text, 13, text.c_str(), t.accent, begin, end);
		nvgResetScissor(args.vg);
	}

	void onChange(const ChangeEvent& e) override;
};


struct ResultRow : widget::OpaqueWidget {
	DockedBrowser* browser;
	plugin::Model* model;
	bool handoffPending = false;

	void draw(const DrawArgs& args) override;
	void step() override;

	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override;

	void onDragMove(const DragMoveEvent& e) override {
		// Once the pointer reaches the rack, the row hands the drag over to a new module
		if (APP->scene->rackScroll->box.contains(APP->scene->mousePos))
			handoffPending = true;
	}
};


struct ModuleWidgetHolder : widget::Widget {
	void draw(const DrawArgs& args) override {
		Widget::draw(args);
		// lights
		Widget::drawLayer(args, 1);
	}
};


struct AddButton : widget::OpaqueWidget {
	DockedBrowser* browser;

	void draw(const DrawArgs& args) override {
		const Theme& t = theme();
		const bool hovered = APP->event->hoveredWidget == this;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
		nvgFillColor(args.vg, hovered ? nvgRGB(0xff, 0xb0, 0x55) : t.accent);
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGB(0, 0, 0));
		nvgStroke(args.vg);
		draw::text(args.vg, uiFont(), 13.f, nvgRGB(0x1a, 0x10, 0x04), box.size.div(2), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "Add to rack");
	}

	void onButton(const ButtonEvent& e) override;
};


struct PreviewCard : widget::Widget {
	DockedBrowser* browser;
	plugin::Model* model = nullptr;
	widget::ZoomWidget* zoomWidget = nullptr;
	widget::FramebufferWidget* fb = nullptr;
	AddButton* addButton;

	static constexpr const float kStageWidth = 90.f;
	static constexpr const float kStageHeight = 180.f;

	PreviewCard() {
		addButton = new AddButton;
		addButton->box.size = math::Vec(92.f, 28.f);
		addChild(addButton);
	}

	math::Rect stageRect() const {
		return math::Rect(kBrowserPad, kBrowserPad, kStageWidth, kStageHeight);
	}

	void setModel(plugin::Model* const m) {
		if (m == model)
			return;
		model = m;

		if (zoomWidget != nullptr) {
			removeChild(zoomWidget);
			delete zoomWidget;
			zoomWidget = nullptr;
		}
		if (model == nullptr)
			return;

		app::ModuleWidget* mw;
		try {
			mw = model->createModuleWidget(NULL);
		}
		catch (Exception& e) {
			WARN("PatchCore: preview of %s failed: %s", model->getFullName().c_str(), e.what());
			return;
		}

		zoomWidget = new widget::ZoomWidget;
		fb = new widget::FramebufferWidget;
		fb->oversample = APP->window->pixelRatio < 2.0 ? 2.0 : 1.0;
		zoomWidget->addChild(fb);
		ModuleWidgetHolder* const holder = new ModuleWidgetHolder;
		holder->box.size = mw->box.size;
		holder->addChild(mw);
		fb->addChild(holder);
		mw->step();
		// The preview shows the front, so no sockets in PatchCore
		app::ModuleWidget_syncPatchCoreView(mw);

		const math::Rect stage = stageRect();
		const float zoom = std::min((kStageHeight - 12.f) / mw->box.size.y, (kStageWidth - 8.f) / mw->box.size.x);
		zoomWidget->setZoom(zoom);
		zoomWidget->box.size = mw->box.size.mult(zoom);
		zoomWidget->box.pos = math::Vec(stage.pos.x + (stage.size.x - zoomWidget->box.size.x) * 0.5f, stage.pos.y + 6.f);
		addChild(zoomWidget);
	}

	void step() override {
		addButton->box.pos = math::Vec(kBrowserPad + kStageWidth + 10.f, box.size.y - kBrowserPad - addButton->box.size.y - 4.f);
		addButton->setVisible(model != nullptr);
		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();

		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, t.chrome2);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0, 0.5f);
		nvgLineTo(vg, box.size.x, 0.5f);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);

		const math::Rect stage = stageRect();
		draw::insetBox(vg, stage, 4.f, nvgRGB(0x0c, 0x0d, 0x0f));

		if (model == nullptr) {
			draw::text(vg, uiFont(), 12.f, t.textFaint, math::Vec(stage.getRight() + 10.f, stage.pos.y + 12.f),
			           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "Select a module");
			return;
		}

		if (zoomWidget != nullptr) {
			nvgSave(vg);
			nvgScissor(vg, RECT_ARGS(stage));
			drawChild(zoomWidget, args);
			nvgRestore(vg);
		}
		drawChild(addButton, args);

		const float x = stage.getRight() + 10.f;
		const float w = box.size.x - x - kBrowserPad;
		nvgSave(vg);
		nvgScissor(vg, x, 0, w, box.size.y);
		draw::text(vg, uiFont(), 15.f, t.text, math::Vec(x, stage.pos.y + 10.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, model->name);
		const ModelMeta& meta = modelMeta(model);
		draw::text(vg, uiFont(), 11.f, t.textDim, math::Vec(x, stage.pos.y + 29.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
		           string::f("%s · %d HP", model->plugin->brand.c_str(), meta.hp));

		// Tag pills, wrapped
		float tx = x;
		float ty = stage.pos.y + 44.f;
		size_t shown = 0;
		for (int tagId : model->tagIds) {
			if (shown == 4)
				break;
			const std::string label = tag::getTag(tagId);
			const float tw = textWidth(uiFont(), 10.f, label, 0.f) + 10.f;
			if (tx + tw > x + w && tx > x) {
				tx = x;
				ty += 20.f;
			}
			nvgBeginPath(vg);
			nvgRoundedRect(vg, tx, ty, tw, 16.f, 3.f);
			nvgFillColor(vg, t.chrome3);
			nvgFill(vg);
			draw::text(vg, uiFont(), 10.f, t.textDim, math::Vec(tx + tw * 0.5f, ty + 8.f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
			tx += tw + 4.f;
			++shown;
		}

		if (!model->description.empty()) {
			nvgFontFaceId(vg, uiFont()->handle);
			nvgFontSize(vg, 11.f);
			nvgTextLetterSpacing(vg, 0.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, t.textFaint);
			nvgTextBox(vg, x, ty + 26.f, w, model->description.c_str(), nullptr);
		}
		nvgRestore(vg);
	}
};


struct DockedBrowser : widget::OpaqueWidget {
	SearchField* searchField;
	ui::ScrollWidget* scroll;
	PreviewCard* preview;

	std::string search;
	size_t chip = 0;
	Location location = LOCATION_ALL;
	std::string brand;
	plugin::Model* selected = nullptr;

	bool dirty = true;
	std::string lastFilterKey;
	size_t resultCount = 0;
	size_t totalCount = 0;
	size_t favoriteCount = 0;
	size_t recentCount = 0;
	size_t brandCount = 0;

	// Layout, set by step()
	math::Rect chipsRect;
	std::vector<math::Rect> chipRects;
	math::Rect locationsRect;
	math::Rect searchRect;

	DockedBrowser() {
		box.size.x = 268.f;

		searchField = new SearchField;
		searchField->browser = this;
		addChild(searchField);

		scroll = new ui::ScrollWidget;
		addChild(scroll);

		preview = new PreviewCard;
		preview->browser = this;
		preview->addButton->browser = this;
		addChild(preview);
	}

	bool isVisibleModel(plugin::Model* const model) const {
		const Chip& c = kChips[chip];
		if (c.host && model->plugin->slug != "Cardinal")
			return false;
		if (c.tagName != nullptr) {
			const int tagId = tag::findId(c.tagName);
			if (std::find(model->tagIds.begin(), model->tagIds.end(), tagId) == model->tagIds.end())
				return false;
		}
		if (!brand.empty() && model->plugin->brand != brand)
			return false;

		const rack::settings::ModuleInfo* const mi = rack::settings::getModuleInfo(model->plugin->slug, model->slug);
		if (location == LOCATION_FAVORITES && !(mi && mi->favorite))
			return false;
		if (location == LOCATION_RECENT && !(mi && mi->added > 0))
			return false;
		return true;
	}

	void refresh() {
		dirty = false;

		std::vector<plugin::Model*> all;
		std::set<std::string> brands;
		favoriteCount = recentCount = 0;
		for (plugin::Plugin* const p : plugin::plugins) {
			for (plugin::Model* const m : p->models) {
				all.push_back(m);
				brands.insert(p->brand);
				const rack::settings::ModuleInfo* const mi = rack::settings::getModuleInfo(p->slug, m->slug);
				if (mi && mi->favorite)
					++favoriteCount;
				if (mi && mi->added > 0)
					++recentCount;
			}
		}
		totalCount = all.size();
		brandCount = brands.size();

		std::vector<plugin::Model*> results;
		if (search.empty()) {
			results = all;
			if (location == LOCATION_RECENT) {
				std::sort(results.begin(), results.end(), [](plugin::Model* a, plugin::Model* b) {
					const rack::settings::ModuleInfo* ma = rack::settings::getModuleInfo(a->plugin->slug, a->slug);
					const rack::settings::ModuleInfo* mb = rack::settings::getModuleInfo(b->plugin->slug, b->slug);
					return (ma ? ma->lastAdded : 0.0) > (mb ? mb->lastAdded : 0.0);
				});
			}
			else {
				std::sort(results.begin(), results.end(), [](plugin::Model* a, plugin::Model* b) {
					return std::make_tuple(string::lowercase(a->name), a->plugin->brand)
					     < std::make_tuple(string::lowercase(b->name), b->plugin->brand);
				});
			}
		}
		else {
			results = searchModels(search);
		}

		// Keep the scroll position unless the filter changed
		const std::string filterKey = string::f("%s|%d|%d|%s", search.c_str(), (int) chip, (int) location, brand.c_str());
		if (filterKey != lastFilterKey)
			scroll->offset = math::Vec();
		lastFilterKey = filterKey;
		scroll->container->clearChildren();
		resultCount = 0;
		float y = 0.f;
		for (plugin::Model* const model : results) {
			if (!isVisibleModel(model))
				continue;
			++resultCount;
			if (resultCount > kMaxResults)
				continue;
			ResultRow* const row = new ResultRow;
			row->browser = this;
			row->model = model;
			row->box = math::Rect(0, y, box.size.x, kRowHeight);
			scroll->container->addChild(row);
			y += kRowHeight;
		}
	}

	void select(plugin::Model* const model) {
		selected = model;
		preview->setModel(model);
	}

	/** Where "Add to rack" puts a module: right of the selection, or the middle of the view */
	math::Vec defaultAddPosition() const {
		app::RackWidget* const rack = APP->scene->rack;
		if (rack->hasSelection()) {
			math::Vec pos(-INFINITY, 0);
			for (app::ModuleWidget* const mw : rack->getSelected()) {
				if (mw->box.getRight() > pos.x)
					pos = math::Vec(mw->box.getRight(), mw->box.pos.y);
			}
			return pos;
		}
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		const math::Vec center = scroll->offset.plus(scroll->box.size.div(2)).div(scroll->getZoom());
		return center.minus(math::Vec(0, app::RACK_GRID_HEIGHT / 2));
	}

	void addSelected() {
		if (selected == nullptr)
			return;
		app::ModuleWidget* const mw = addModelAt(selected, defaultAddPosition());
		APP->scene->rack->deselectAll();
		APP->scene->rack->select(mw);
		dirty = true;
	}

	void openBrandMenu() {
		ui::Menu* const menu = createMenu();
		menu->addChild(createMenuLabel("Brand"));
		menu->addChild(createCheckMenuItem("All brands", "", [=]() {return brand.empty();}, [=]() {brand.clear(); dirty = true;}));

		std::set<std::string> brands;
		for (plugin::Plugin* const p : plugin::plugins)
			brands.insert(p->brand);

		// Group by first letter so the menu stays short
		std::map<std::string, std::vector<std::string>> groups;
		for (const std::string& b : brands) {
			const char c = b.empty() ? '#' : std::toupper(b[0]);
			const std::string key = c < 'A' || c > 'Z' ? "#" : c <= 'E' ? "A - E" : c <= 'J' ? "F - J" : c <= 'O' ? "K - O" : c <= 'T' ? "P - T" : "U - Z";
			groups[key].push_back(b);
		}
		for (const auto& g : groups) {
			const std::vector<std::string> names = g.second;
			menu->addChild(createSubmenuItem(g.first, "", [=](ui::Menu* sub) {
				for (const std::string& b : names) {
					sub->addChild(createCheckMenuItem(b, "", [=]() {return brand == b;}, [=]() {brand = b; dirty = true;}));
				}
			}));
		}
	}

	void step() override {
		const float w = box.size.x;
		float y = 32.f;
		searchRect = math::Rect(kBrowserPad, y, w - kBrowserPad * 2, 30.f);
		searchField->box = math::Rect(searchRect.pos.x + 22.f, y, searchRect.size.x - 24.f, 30.f);
		y += 30.f + 8.f;

		// Chips wrap over several lines
		chipRects.clear();
		float x = kBrowserPad;
		for (const Chip& c : kChips) {
			const float cw = textWidth(uiFont(), 11.f, c.label, 0.f) + 16.f;
			if (x + cw > w - kBrowserPad) {
				x = kBrowserPad;
				y += 25.f;
			}
			chipRects.push_back(math::Rect(x, y, cw, 20.f));
			x += cw + 4.f;
		}
		y += 20.f + 10.f;
		chipsRect = math::Rect(0, 0, w, y);

		locationsRect = math::Rect(0, y, w, 4 * 24.f + 12.f);
		y += locationsRect.size.y;

		preview->box = math::Rect(0, box.size.y - kPreviewHeight, w, kPreviewHeight);
		scroll->box = math::Rect(0, y, w, std::max(0.f, preview->box.pos.y - y));
		for (widget::Widget* const row : scroll->container->children)
			row->box.size.x = w - 8.f;

		if (dirty)
			refresh();

		OpaqueWidget::step();
	}

	int locationAt(math::Vec pos) const {
		if (!locationsRect.contains(pos))
			return -1;
		const int i = (int) ((pos.y - locationsRect.pos.y - 6.f) / 24.f);
		return i >= 0 && i < 4 ? i : -1;
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.isConsumed())
			return;
		e.consume(this);
		if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		for (size_t i = 0; i < chipRects.size(); ++i) {
			if (chipRects[i].contains(e.pos)) {
				chip = i;
				dirty = true;
				return;
			}
		}

		switch (locationAt(e.pos)) {
			case 0:
				location = location == LOCATION_FAVORITES ? LOCATION_ALL : LOCATION_FAVORITES;
				dirty = true;
				break;
			case 1:
				location = location == LOCATION_RECENT ? LOCATION_ALL : LOCATION_RECENT;
				dirty = true;
				break;
			case 2:
				openBrandMenu();
				break;
			case 3:
				patchUtils::loadDialog();
				break;
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();
		const float w = box.size.x;

		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, box.size.y);
		nvgFillColor(vg, t.chrome1);
		nvgFill(vg);

		// Header
		draw::text(vg, uiFont(), 11.f, t.textDim, math::Vec(kBrowserPad, 18.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "MODULE BROWSER", 1.4f);
		draw::text(vg, uiFont(), 11.f, t.textDim, math::Vec(w - kBrowserPad, 18.f), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
		           string::f("%d OF %d", (int) resultCount, (int) totalCount), 1.f);

		// Search box
		draw::insetBox(vg, searchRect, 5.f, nvgRGB(0x0d, 0x0e, 0x10));
		const math::Vec lens(searchRect.pos.x + 13.f, searchRect.pos.y + searchRect.size.y * 0.5f - 1.f);
		nvgBeginPath(vg);
		nvgCircle(vg, lens.x, lens.y, 4.5f);
		nvgMoveTo(vg, lens.x + 3.5f, lens.y + 3.5f);
		nvgLineTo(vg, lens.x + 7.f, lens.y + 7.f);
		nvgStrokeWidth(vg, 1.5f);
		nvgStrokeColor(vg, t.textDim);
		nvgStroke(vg);

		// Chips
		for (size_t i = 0; i < kChips.size(); ++i) {
			const math::Rect& r = chipRects[i];
			const bool on = i == chip;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, RECT_ARGS(r), 10.f);
			nvgFillColor(vg, on ? t.accent : t.chrome2);
			nvgFill(vg);
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, on ? t.accent : t.line);
			nvgStroke(vg);
			draw::text(vg, uiFont(), 11.f, on ? nvgRGB(0x1a, 0x10, 0x04) : t.textDim, r.getCenter(),
			           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kChips[i].label);
		}
		for (const float ly : {chipsRect.size.y - 0.5f, locationsRect.getBottom() - 0.5f}) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, ly);
			nvgLineTo(vg, w, ly);
			nvgStrokeColor(vg, t.lineSoft);
			nvgStroke(vg);
		}

		// Locations
		const std::string brandLabel = brand.empty() ? "Brands" : "Brand: " + brand;
		const std::vector<std::pair<std::string, std::string>> locations = {
			{"★  Favorites", std::to_string(favoriteCount)},
			{"↺  Recently used", std::to_string(recentCount)},
			{"▤  " + brandLabel, brand.empty() ? std::to_string(brandCount) : "×"},
			{"□  Patch library", "Open"},
		};
		const int hoveredLocation = APP->event->hoveredWidget == this ? locationAt(APP->scene->mousePos.minus(getAbsoluteOffset(math::Vec()))) : -1;
		for (size_t i = 0; i < locations.size(); ++i) {
			const float ly = locationsRect.pos.y + 6.f + i * 24.f;
			const bool active = (i == 0 && location == LOCATION_FAVORITES) || (i == 1 && location == LOCATION_RECENT) || (i == 2 && !brand.empty());
			if ((int) i == hoveredLocation || active) {
				nvgBeginPath(vg);
				nvgRect(vg, 0, ly, w, 24.f);
				nvgFillColor(vg, t.chrome2);
				nvgFill(vg);
			}
			draw::text(vg, uiFont(), 12.f, active ? t.accent : (int) i == hoveredLocation ? t.text : t.textDim,
			           math::Vec(12.f, ly + 12.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, locations[i].first);
			draw::text(vg, monoFont(), 11.f, t.textFaint, math::Vec(w - 12.f, ly + 12.f), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, locations[i].second);
		}

		if (resultCount == 0) {
			draw::text(vg, uiFont(), 12.f, t.textFaint, math::Vec(w * 0.5f, scroll->box.pos.y + 30.f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
			           "No matching modules");
		}

		Widget::draw(args);

		// Right border
		nvgBeginPath(vg);
		nvgMoveTo(vg, w - 0.5f, 0);
		nvgLineTo(vg, w - 0.5f, box.size.y);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);
	}
};


void SearchField::onChange(const ChangeEvent& e) {
	browser->search = string::trim(text);
	browser->dirty = true;
}


void ResultRow::draw(const DrawArgs& args) {
	NVGcontext* const vg = args.vg;
	const Theme& t = theme();
	const bool isSelected = browser->selected == model;
	const bool hovered = APP->event->hoveredWidget == this;

	if (isSelected || hovered) {
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, isSelected ? nvgRGB(0x2b, 0x2f, 0x37) : t.chrome2);
		nvgFill(vg);
	}
	if (isSelected) {
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, 2.f, box.size.y);
		nvgFillColor(vg, t.accent);
		nvgFill(vg);
	}

	// Thumbnail: panel color with two knobs
	const ModelMeta& meta = modelMeta(model);
	const math::Rect thumb(10.f, (box.size.y - 34.f) * 0.5f, 22.f, 34.f);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(thumb), 2.f);
	nvgFillColor(vg, meta.color);
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.f);
	nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 0x60));
	nvgStroke(vg);
	const NVGcolor knob = isLight(meta.color) ? nvgRGB(0x22, 0x22, 0x22) : nvgRGB(0xd8, 0xd8, 0xd8);
	nvgBeginPath(vg);
	nvgCircle(vg, thumb.pos.x + 11.f, thumb.pos.y + 10.5f, 4.5f);
	nvgCircle(vg, thumb.pos.x + 11.f, thumb.pos.y + 22.f, 3.f);
	nvgFillColor(vg, knob);
	nvgFill(vg);

	nvgSave(vg);
	nvgScissor(vg, 0, 0, box.size.x - 6.f, box.size.y);
	draw::text(vg, uiFont(), 12.5f, t.text, math::Vec(42.f, box.size.y * 0.5f - 7.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, model->name);
	std::string metaText = model->plugin->brand;
	if (meta.hp > 0)
		metaText += string::f(" · %d HP", meta.hp);
	const std::string tagText = firstTag(model);
	if (!tagText.empty())
		metaText += " · " + tagText;
	draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(42.f, box.size.y * 0.5f + 8.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, metaText);
	nvgRestore(vg);
}


void ResultRow::step() {
	if (handoffPending) {
		handoffPending = false;
		if (APP->event->getDraggedWidget() == this) {
			// Create the module under the mouse and continue the drag on it
			app::ModuleWidget* const mw = addModelAtMouse(model);
			mw->dragOffset() = mw->box.size.div(2);
			mw->dragEnabled() = true;
			APP->event->setDraggedWidget(mw, GLFW_MOUSE_BUTTON_LEFT);
			browser->dirty = true;
		}
	}
	OpaqueWidget::step();
}


void ResultRow::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			model->setFavorite(!model->isFavorite());
			browser->dirty = true;
		}
		browser->select(model);
		e.consume(this);
	}
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		ui::Menu* const menu = createMenu();
		menu->addChild(createMenuLabel(model->name));
		menu->addChild(createMenuLabel(model->plugin->brand));
		model->appendContextMenu(menu, true);
		e.consume(this);
	}
}


void ResultRow::onDoubleClick(const DoubleClickEvent& e) {
	browser->select(model);
	browser->addSelected();
	e.consume(this);
}


void AddButton::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		browser->addSelected();
		e.consume(this);
	}
}


widget::Widget* createDockedBrowser() {
	return new DockedBrowser;
}


} // namespace patchcore
} // namespace rack
