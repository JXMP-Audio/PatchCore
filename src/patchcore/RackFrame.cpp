/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include <app/ModuleWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/RailWidget.hpp>
#include <app/common.hpp>
#include <string.hpp>

namespace rack {
namespace patchcore {


static constexpr const float kEarWidth = 30.f;
static constexpr const float kCapHeight = 10.f;
static constexpr const float kRailHeight = 18.f;
static constexpr const int kMinRowHP = 84;


struct RackFrameWidget : widget::TransparentWidget {
	app::RackWidget* const rack;

	struct Row {
		int index;
		float y;
		// occupied horizontal intervals, sorted and merged
		std::vector<std::pair<float, float>> used;
	};

	// Frame geometry in rack coordinates
	float frameX = 0.f;
	float frameW = 0.f;
	std::vector<Row> rows;

	explicit RackFrameWidget(app::RackWidget* const r)
		: rack(r) {}

	void updateRailVisibility() {
		for (widget::Widget* const child : parent->children) {
			if (dynamic_cast<app::RailWidget*>(child) != nullptr)
				child->setVisible(!enabled());
		}
	}

	void updateLayout() {
		std::map<int, Row> rowMap;
		float minX = INFINITY;
		float maxX = -INFINITY;

		for (widget::Widget* const w : rack->getModuleContainer()->children) {
			const math::Rect& b = w->box;
			const int index = (int) std::round((b.pos.y - app::RACK_OFFSET.y) / app::RACK_GRID_HEIGHT);
			Row& row = rowMap[index];
			row.index = index;
			row.y = app::RACK_OFFSET.y + index * app::RACK_GRID_HEIGHT;
			row.used.push_back({b.pos.x, b.pos.x + b.size.x});
			minX = std::min(minX, b.pos.x);
			maxX = std::max(maxX, b.pos.x + b.size.x);
		}

		rows.clear();

		if (rowMap.empty()) {
			frameX = app::RACK_OFFSET.x;
			frameW = kMinRowHP * app::RACK_GRID_WIDTH;
			rows.push_back(Row{0, app::RACK_OFFSET.y, {}});
			return;
		}

		// Auto-fit: snap to HP, leave some free HP on the right, never narrower than a standard 84 HP case
		minX = std::floor((minX - app::RACK_OFFSET.x) / app::RACK_GRID_WIDTH) * app::RACK_GRID_WIDTH + app::RACK_OFFSET.x;
		const float usedHP = std::ceil((maxX - minX) / app::RACK_GRID_WIDTH);
		const float frameHP = std::max<float>(kMinRowHP, std::ceil((usedHP + 4) / 4.f) * 4.f);
		frameX = minX;
		frameW = frameHP * app::RACK_GRID_WIDTH;

		// Rows are contiguous from the first to the last occupied one, like a real rack
		const int first = rowMap.begin()->first;
		const int last = rowMap.rbegin()->first;
		for (int i = first; i <= last; ++i) {
			Row row;
			std::map<int, Row>::iterator it = rowMap.find(i);
			if (it != rowMap.end())
				row = it->second;
			row.index = i;
			row.y = app::RACK_OFFSET.y + i * app::RACK_GRID_HEIGHT;

			std::sort(row.used.begin(), row.used.end());
			std::vector<std::pair<float, float>> merged;
			for (const std::pair<float, float>& u : row.used) {
				if (!merged.empty() && u.first <= merged.back().second)
					merged.back().second = std::max(merged.back().second, u.second);
				else
					merged.push_back(u);
			}
			row.used = merged;
			rows.push_back(row);
		}
	}

	void step() override {
		updateRailVisibility();

		// Front/back: modules show either controls or sockets, cables only exist on the back
		for (widget::Widget* const w : rack->getModuleContainer()->children) {
			if (app::ModuleWidget* const mw = dynamic_cast<app::ModuleWidget*>(w))
				app::ModuleWidget_syncPatchCoreView(mw);
		}
		rack->getCableContainer()->setVisible(!hideFrontPorts());

		if (enabled())
			updateLayout();
		TransparentWidget::step();
	}

	math::Rect frameRect() const {
		const float top = rows.front().y;
		const float bottom = rows.back().y + app::RACK_GRID_HEIGHT;
		return math::Rect(frameX - kEarWidth, top - kCapHeight, frameW + kEarWidth * 2, bottom - top + kCapHeight * 2);
	}

	void drawRail(NVGcontext* const vg, const math::Rect& clip, const Row& row, float y) {
		const bool back = isBackView();

		// Brushed aluminium strip
		nvgBeginPath(vg);
		nvgRect(vg, frameX, y, frameW, kRailHeight);
		NVGpaint paint = nvgLinearGradient(vg, 0, y, 0, y + kRailHeight,
		                                   back ? nvgRGB(0x8e, 0x91, 0x95) : nvgRGB(0xe2, 0xe5, 0xe9),
		                                   back ? nvgRGB(0x50, 0x53, 0x58) : nvgRGB(0x7e, 0x83, 0x8b));
		nvgFillPaint(vg, paint);
		nvgFill(vg);

		// Mid highlight, gives the rail its rounded profile
		nvgBeginPath(vg);
		nvgRect(vg, frameX, y + kRailHeight * 0.62f, frameW, kRailHeight * 0.38f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, y + kRailHeight * 0.62f, 0, y + kRailHeight,
		                                   nvgRGBAf(1, 1, 1, 0), nvgRGBAf(1, 1, 1, back ? 0.15f : 0.35f)));
		nvgFill(vg);

		// Shadow cast onto the recess
		nvgBeginPath(vg);
		nvgRect(vg, frameX, y + kRailHeight, frameW, 3.f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
		nvgFill(vg);

		// Threaded holes, one per HP, only where no module covers the rail
		float x0 = frameX;
		std::vector<std::pair<float, float>> gaps;
		for (const std::pair<float, float>& u : row.used) {
			if (u.first > x0)
				gaps.push_back({x0, u.first});
			x0 = std::max(x0, u.second);
		}
		if (x0 < frameX + frameW)
			gaps.push_back({x0, frameX + frameW});

		for (const std::pair<float, float>& g : gaps) {
			const float start = std::max(g.first, clip.pos.x - app::RACK_GRID_WIDTH);
			const float end = std::min(g.second, clip.pos.x + clip.size.x + app::RACK_GRID_WIDTH);
			const int firstHP = (int) std::ceil((start - frameX) / app::RACK_GRID_WIDTH);
			for (int hp = firstHP;; ++hp) {
				const float cx = frameX + (hp + 0.5f) * app::RACK_GRID_WIDTH;
				if (cx > end)
					break;
				if (cx < g.first || cx > g.second)
					continue;
				draw::hole(vg, math::Vec(cx, y + kRailHeight * 0.5f), 2.6f);
			}
		}
	}

	void drawEar(NVGcontext* const vg, const Row& row, float x, bool left) {
		const float y = row.y;
		const float h = app::RACK_GRID_HEIGHT;

		// Powder-coated steel
		nvgBeginPath(vg);
		nvgRect(vg, x, y, kEarWidth, h);
		nvgFillPaint(vg, nvgLinearGradient(vg, x, 0, x + kEarWidth, 0, nvgRGB(0x1a, 0x1b, 0x1e), nvgRGB(0x24, 0x26, 0x2a)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, x + kEarWidth * 0.3f, y, kEarWidth * 0.4f, h);
		nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.035f));
		nvgFill(vg);

		// Row separator
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, y + 0.5f);
		nvgLineTo(vg, x + kEarWidth, y + 0.5f);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);

		// Mounting slots with screws
		for (const float sy : {y + 16.f, y + h - 16.f}) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + kEarWidth * 0.5f - 8.f, sy - 5.5f, 16.f, 11.f, 5.5f);
			nvgFillColor(vg, nvgRGB(0x05, 0x05, 0x06));
			nvgFill(vg);
			draw::screw(vg, math::Vec(x + kEarWidth * 0.5f - 1.f, sy), 4.6f, left ? 0.4f : -0.6f);
		}

		// Handle
		const math::Rect handle(x + 8.f, y + h * 0.3f, kEarWidth - 16.f, h * 0.4f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, handle.pos.x + 2.f, handle.pos.y + 3.f, handle.size.x, handle.size.y, 7.f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(handle), 7.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, handle.pos.x, 0, handle.pos.x + handle.size.x, 0,
		                                   nvgRGB(0xa5, 0xa9, 0xb0), nvgRGB(0x3c, 0x3f, 0x44)));
		nvgFill(vg);

		// Row number
		if (left)
			draw::text(vg, monoFont(), 11.f, theme().textFaint, math::Vec(x + kEarWidth * 0.5f, y + h - 34.f),
			           NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, string::f("%02d", row.index + 1));
	}

	void draw(const DrawArgs& args) override {
		if (!enabled())
			return;

		NVGcontext* const vg = args.vg;
		const Theme& t = theme();

		// Studio wall behind the rack
		nvgBeginPath(vg);
		nvgRect(vg, RECT_ARGS(args.clipBox));
		nvgFillColor(vg, t.wall);
		nvgFill(vg);

		if (rows.empty())
			return;

		const math::Rect frame = frameRect();

		// Frame drop shadow
		nvgBeginPath(vg);
		nvgRect(vg, RECT_ARGS(frame.grow(math::Vec(40, 40))));
		nvgFillPaint(vg, nvgBoxGradient(vg, frame.pos.x + 4.f, frame.pos.y + 10.f, frame.size.x, frame.size.y, 6.f, 30.f,
		                                nvgRGBAf(0, 0, 0, 0.75f), nvgRGBAf(0, 0, 0, 0)));
		nvgFill(vg);

		// Frame body, visible as top/bottom caps
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(frame), 3.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, frame.pos.y, 0, frame.pos.y + kCapHeight,
		                                   nvgRGB(0x2c, 0x2e, 0x33), nvgRGB(0x15, 0x16, 0x18)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGB(0, 0, 0));
		nvgStroke(vg);

		const bool back = isBackView();

		for (const Row& row : rows) {
			const math::Rect rowRect(frameX, row.y, frameW, app::RACK_GRID_HEIGHT);
			if (!args.clipBox.intersects(rowRect.grow(math::Vec(kEarWidth, 0))))
				continue;

			// Recess behind empty HP
			nvgBeginPath(vg);
			nvgRect(vg, RECT_ARGS(rowRect));
			nvgFillPaint(vg, nvgLinearGradient(vg, 0, row.y, 0, row.y + app::RACK_GRID_HEIGHT * 0.25f,
			                                   back ? nvgRGB(0x0b, 0x0c, 0x0e) : nvgRGB(0x06, 0x06, 0x07),
			                                   back ? nvgRGB(0x1a, 0x1c, 0x20) : nvgRGB(0x14, 0x15, 0x18)));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, RECT_ARGS(rowRect));
			nvgFillPaint(vg, nvgBoxGradient(vg, frameX, row.y + 8.f, frameW, app::RACK_GRID_HEIGHT - 10.f, 0.f, 22.f,
			                                nvgRGBAf(0, 0, 0, 0), nvgRGBAf(0, 0, 0, 0.85f)));
			nvgFill(vg);

			drawRail(vg, args.clipBox, row, row.y);
			drawRail(vg, args.clipBox, row, row.y + app::RACK_GRID_HEIGHT - kRailHeight);

			drawEar(vg, row, frameX - kEarWidth, true);
			drawEar(vg, row, frameX + frameW, false);
		}
	}
};


/** Paints the back of all modules. As the last child of RackWidget, its layer 1 is drawn after the modules'
lights and displays (which some module widgets draw themselves) and before the cables on layer 2. */
struct BackOverlayWidget : widget::TransparentWidget {
	app::RackWidget* const rack;

	explicit BackOverlayWidget(app::RackWidget* const r)
		: rack(r) {}

	void draw(const DrawArgs& args) override {}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !isBackView())
			return;

		const Theme& t = theme();
		for (widget::Widget* const w : rack->getModuleContainer()->children) {
			app::ModuleWidget* const mw = dynamic_cast<app::ModuleWidget*>(w);
			if (mw == nullptr || !mw->isVisible() || !args.clipBox.intersects(mw->box))
				continue;

			DrawArgs moduleArgs = args;
			moduleArgs.clipBox = math::Rect(args.clipBox.pos.minus(mw->box.pos), args.clipBox.size);
			nvgSave(args.vg);
			nvgTranslate(args.vg, VEC_ARGS(mw->box.pos));
			drawModuleBack(mw, moduleArgs);
			if (rack->isSelected(mw)) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 1.0, 1.0, mw->box.size.x - 2.0, mw->box.size.y - 2.0);
				nvgStrokeWidth(args.vg, 2.0);
				nvgStrokeColor(args.vg, t.accent);
				nvgStroke(args.vg);
			}
			nvgRestore(args.vg);
		}
	}
};


widget::Widget* createRackFrame(app::RackWidget* const rack) {
	RackFrameWidget* const frame = new RackFrameWidget(rack);
	rack->addChildBelow(frame, rack->getModuleContainer());
	rack->addChild(new BackOverlayWidget(rack));
	return frame;
}


} // namespace patchcore
} // namespace rack
