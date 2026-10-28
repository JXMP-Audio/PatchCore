/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <map>

#include <app/ModuleWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <app/SvgPanel.hpp>
#include <context.hpp>
#include <widget/OpaqueWidget.hpp>
#include <window/Svg.hpp>

namespace rack {
namespace patchcore {


NVGcolor panelColor(app::ModuleWidget* const mw) {
	NVGcolor fallback = nvgRGB(0x4a, 0x4d, 0x54);
	app::SvgPanel* const panel = dynamic_cast<app::SvgPanel*>(mw->getPanel());
	if (panel == nullptr || !panel->svg || panel->svg->handle == nullptr)
		return fallback;

	const NSVGimage* const image = panel->svg->handle;
	const float imageArea = image->width * image->height;

	// The panel background is the first solid shape that covers most of the image
	for (const NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
		if (shape->fill.type != NSVG_PAINT_COLOR || !(shape->flags & NSVG_FLAGS_VISIBLE))
			continue;
		const float area = (shape->bounds[2] - shape->bounds[0]) * (shape->bounds[3] - shape->bounds[1]);
		if (area < imageArea * 0.5f)
			continue;
		const unsigned int c = shape->fill.color;
		return nvgRGB(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
	}
	return fallback;
}


struct Navigator : widget::OpaqueWidget {
	static constexpr const float kPad = 12.f;
	static constexpr const float kMapTop = 34.f;
	static constexpr const float kMaxMapHeight = 220.f;

	std::map<app::ModuleWidget*, NVGcolor> colors;
	size_t lastModuleCount = 0;

	// Layout of the last draw, used to map clicks back to rack coordinates
	math::Rect world;
	math::Rect map;
	float scale = 1.f;

	void computeLayout() {
		world = APP->scene->rack->getModuleContainer()->getChildrenBoundingBox();
		if (!world.size.isFinite() || world.size.area() <= 0.f)
			world = math::Rect(app::RACK_OFFSET, math::Vec(84 * app::RACK_GRID_WIDTH, app::RACK_GRID_HEIGHT));
		world = world.grow(math::Vec(40.f, 20.f));

		const float mapWidth = box.size.x - kPad * 2;
		scale = std::min(mapWidth / world.size.x, kMaxMapHeight / world.size.y);
		map = math::Rect(kPad, kMapTop, mapWidth, world.size.y * scale);
	}

	math::Vec toMap(math::Vec p) const {
		return map.pos.plus(p.minus(world.pos).mult(scale)).plus(math::Vec((map.size.x - world.size.x * scale) * 0.5f, 0));
	}

	math::Vec toWorld(math::Vec p) const {
		const math::Vec origin = map.pos.plus(math::Vec((map.size.x - world.size.x * scale) * 0.5f, 0));
		return world.pos.plus(p.minus(origin).div(scale));
	}

	void centerViewOn(math::Vec mapPos) {
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		const float zoom = scroll->getZoom();
		const math::Vec target = toWorld(mapPos);
		scroll->offset = target.mult(zoom).minus(scroll->box.size.div(2));
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && map.contains(e.pos)) {
			centerViewOn(e.pos);
			e.consume(this);
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		scroll->offset = scroll->offset.plus(e.mouseDelta.div(scale).mult(scroll->getZoom()));
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		const Theme& t = theme();

		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, t.chrome1);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.5f, 0);
		nvgLineTo(vg, 0.5f, box.size.y);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);

		draw::text(vg, uiFont(), 11.f, t.textDim, math::Vec(kPad, 18.f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "NAVIGATOR", 1.4f);

		computeLayout();
		draw::insetBox(vg, map.grow(math::Vec(2, 2)), 4.f, nvgRGB(0x0b, 0x0c, 0x0e));

		const std::vector<app::ModuleWidget*> modules = APP->scene->rack->getModules();
		if (modules.size() != lastModuleCount) {
			colors.clear();
			lastModuleCount = modules.size();
		}

		nvgScissor(vg, RECT_ARGS(map));
		for (app::ModuleWidget* const mw : modules) {
			std::map<app::ModuleWidget*, NVGcolor>::iterator it = colors.find(mw);
			if (it == colors.end())
				it = colors.emplace(mw, panelColor(mw)).first;
			const math::Vec p0 = toMap(mw->box.pos);
			const math::Vec p1 = toMap(mw->box.pos.plus(mw->box.size));
			nvgBeginPath(vg);
			nvgRect(vg, p0.x, p0.y, std::max(1.f, p1.x - p0.x - 0.5f), p1.y - p0.y);
			nvgFillColor(vg, it->second);
			nvgFill(vg);
		}

		// Visible part of the rack
		app::RackScrollWidget* const scroll = APP->scene->rackScroll;
		const float zoom = scroll->getZoom();
		math::Rect view = math::Rect::fromCorners(toMap(scroll->offset.div(zoom)),
		                                          toMap(scroll->offset.plus(scroll->box.size).div(zoom)));
		// Keep the outline visible when the whole rack is on screen
		view = view.intersect(map.shrink(math::Vec(1, 1)));
		nvgBeginPath(vg);
		nvgRect(vg, RECT_ARGS(view));
		nvgFillColor(vg, color::alpha(t.accent, 0.08f));
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.5f);
		nvgStrokeColor(vg, t.accent);
		nvgStroke(vg);
		nvgResetScissor(vg);

		// Shortcut legend
		static const std::vector<std::pair<std::string, std::string>> keys = {
			{"Tab", "flip rack"},
			{"B", "browser"},
			{"Space", "play/stop"},
			{"", "back: drag jack to jack"},
			{"", "front: drag knob to turn"},
		};
		float y = box.size.y - 14.f - (keys.size() - 1) * 19.f;
		for (const std::pair<std::string, std::string>& k : keys) {
			float x = kPad;
			if (!k.first.empty()) {
				const float w = textWidth(monoFont(), 10.f, k.first, 0.f) + 8.f;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y - 7.f, w, 14.f, 3.f);
				nvgFillColor(vg, t.chrome3);
				nvgFill(vg);
				nvgStrokeWidth(vg, 1.f);
				nvgStrokeColor(vg, nvgRGB(0, 0, 0));
				nvgStroke(vg);
				draw::text(vg, monoFont(), 10.f, t.textDim, math::Vec(x + w * 0.5f, y + 0.5f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, k.first);
				x += w + 6.f;
			}
			draw::text(vg, uiFont(), 11.f, t.textFaint, math::Vec(x, y), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, k.second);
			y += 19.f;
		}
	}
};


widget::Widget* createNavigator() {
	return new Navigator;
}


} // namespace patchcore
} // namespace rack
