/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <algorithm>
#include <cstring>

#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/SvgPanel.hpp>
#include <engine/Module.hpp>
#include <engine/PortInfo.hpp>
#include <plugin/Model.hpp>
#include <string.hpp>
#include <window/Svg.hpp>

namespace rack {
namespace patchcore {


bool panelColorAt(app::ModuleWidget* const mw, math::Vec point, float minArea, NVGcolor* const color) {
	app::SvgPanel* const panel = dynamic_cast<app::SvgPanel*>(mw->getPanel());
	if (panel == nullptr || !panel->svg || panel->svg->handle == nullptr)
		return false;

	// Topmost solid shape under the point that is larger than the printed socket artwork
	bool found = false;
	for (const NSVGshape* shape = panel->svg->handle->shapes; shape != nullptr; shape = shape->next) {
		if (shape->fill.type != NSVG_PAINT_COLOR || !(shape->flags & NSVG_FLAGS_VISIBLE) || shape->opacity < 0.99f)
			continue;
		if (point.x < shape->bounds[0] || point.x > shape->bounds[2] || point.y < shape->bounds[1] || point.y > shape->bounds[3])
			continue;
		const float area = (shape->bounds[2] - shape->bounds[0]) * (shape->bounds[3] - shape->bounds[1]);
		if (area < minArea)
			continue;
		const unsigned int c = shape->fill.color;
		if (((c >> 24) & 0xff) < 0xf0)
			continue;
		*color = nvgRGB(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
		found = true;
	}
	return found;
}


/** Short, uppercase jack label that fits under a socket */
static std::string jackLabel(app::PortWidget* const port) {
	std::string name;
	if (port->module != nullptr) {
		const std::vector<engine::PortInfo*>& infos = port->type == engine::Port::INPUT ? port->module->inputInfos
		                                                                                : port->module->outputInfos;
		if (port->portId >= 0 && port->portId < (int) infos.size() && infos[port->portId] != nullptr)
			name = infos[port->portId]->name;
	}
	if (name.empty())
		name = string::f("%s %d", port->type == engine::Port::INPUT ? "IN" : "OUT", port->portId + 1);

	name = string::uppercase(name);
	// Drop common suffixes that waste space on the back
	for (const char* suffix : {" INPUT", " OUTPUT", " IN", " OUT", " CV"}) {
		const size_t len = std::strlen(suffix);
		if (name.size() > len + 2 && name.compare(name.size() - len, len, suffix) == 0 && name.size() > 6) {
			name = name.substr(0, name.size() - len);
			break;
		}
	}
	if (name.size() > 6)
		name = name.substr(0, 6);
	return name;
}


static void collectPorts(widget::Widget* const w, app::ModuleWidget* const mw, std::vector<std::pair<app::PortWidget*, math::Rect>>& ports) {
	for (widget::Widget* const child : w->children) {
		if (app::PortWidget* const port = dynamic_cast<app::PortWidget*>(child)) {
			if (portSide(port) == SIDE_BACK)
				ports.push_back({port, math::Rect(port->getRelativeOffset(math::Vec(), mw), port->box.size)});
		}
		else
			collectPorts(child, mw, ports);
	}
}


void drawBackPanel(app::ModuleWidget* const mw, NVGcontext* const vg) {
	const Theme& t = theme();
	const float w = mw->box.size.x;
	const float h = mw->box.size.y;

	// Sheet-metal plate
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, w, h);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, h, nvgRGB(0x3b, 0x3e, 0x44), nvgRGB(0x2a, 0x2c, 0x31)));
	nvgFill(vg);

	// Folded edges
	nvgStrokeWidth(vg, 1.f);
	nvgBeginPath(vg);
	nvgMoveTo(vg, 0.5f, h);
	nvgLineTo(vg, 0.5f, 0.5f);
	nvgLineTo(vg, w, 0.5f);
	nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.12f));
	nvgStroke(vg);
	nvgBeginPath(vg);
	nvgMoveTo(vg, w - 0.5f, 0);
	nvgLineTo(vg, w - 0.5f, h);
	nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.5f));
	nvgStroke(vg);

	const std::string name = mw->model != nullptr ? mw->model->name : "Module";
	const int hp = (int) std::round(w / app::RACK_GRID_WIDTH);

	// Stencilled name, vertical
	nvgSave(vg);
	nvgTranslate(vg, w * 0.5f, h * 0.44f);
	nvgRotate(vg, -M_PI * 0.5f);
	const float stencilSize = std::min(28.f, w * 0.55f);
	draw::text(vg, labelFont(), stencilSize, nvgRGBAf(1, 1, 1, 0.07f), math::Vec(0, 0), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
	           string::uppercase(name), stencilSize * 0.3f);
	nvgRestore(vg);

	// Serial plate of JXMP modules, else the generic sticker
	const SidedModuleWidget* const sided = dynamic_cast<const SidedModuleWidget*>(mw);
	const std::string serial = sided != nullptr ? string::uppercase(sided->serialLabel()) : std::string();
	if (!serial.empty()) {
		// Two lines (model code, serial number) on a small metal plate where the generic sticker sits
		const size_t dot = serial.find(" · ");
		const std::string line1 = dot == std::string::npos ? serial : serial.substr(0, dot);
		const std::string line2 = dot == std::string::npos ? std::string() : serial.substr(dot + std::strlen(" · "));
		const float fs = 7.f;
		const float tw = std::max(textWidth(monoFont(), fs, line1, 0.f), textWidth(monoFont(), fs, line2, 0.f));
		const float pw = std::min(tw + 10.f, w - 4.f);
		const float ph = line2.empty() ? 12.f : 21.f;
		const math::Rect plate((w - pw) * 0.5f, 30.f, pw, ph);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, plate.pos.x + 1.f, plate.pos.y + 1.5f, pw, ph, 2.f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.5f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(plate), 2.f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, plate.pos.y, 0, plate.pos.y + ph, nvgRGB(0xd9, 0xdb, 0xdf), nvgRGB(0x9c, 0xa0, 0xa7)));
		nvgFill(vg);
		nvgStrokeWidth(vg, 0.6f);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
		nvgStroke(vg);
		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(plate));
		const NVGcolor ink = nvgRGB(0x2a, 0x2c, 0x30);
		const int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
		if (line2.empty()) {
			draw::text(vg, monoFont(), fs, ink, plate.getCenter(), align, line1);
		}
		else {
			draw::text(vg, monoFont(), fs, ink, math::Vec(w * 0.5f, plate.pos.y + 6.f), align, line1);
			draw::text(vg, monoFont(), fs, ink, math::Vec(w * 0.5f, plate.pos.y + 15.f), align, line2);
		}
		nvgRestore(vg);
	}
	else if (w >= app::RACK_GRID_WIDTH * 3) {
		std::string code = string::uppercase(name);
		code.erase(std::remove(code.begin(), code.end(), ' '), code.end());
		if (code.size() > 5)
			code = code.substr(0, 5);
		const std::string sticker = w >= app::RACK_GRID_WIDTH * 6 ? string::f("%s · %dHP", code.c_str(), hp) : code;
		const float sw = std::min(w - 8.f, textWidth(monoFont(), 9.f, sticker, 0.f) + 8.f);
		nvgBeginPath(vg);
		nvgRect(vg, (w - sw) * 0.5f + 1.f, 35.f, sw, 14.f);
		nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.5f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, (w - sw) * 0.5f, 34.f, sw, 14.f);
		nvgFillColor(vg, nvgRGB(0xec, 0xeb, 0xe6));
		nvgFill(vg);
		nvgSave(vg);
		nvgScissor(vg, (w - sw) * 0.5f, 34.f, sw, 14.f);
		draw::text(vg, monoFont(), 9.f, nvgRGB(0x22, 0x22, 0x22), math::Vec(w * 0.5f, 41.5f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, sticker);
		nvgRestore(vg);
	}

	// Power header
	const float pw = std::min(34.f, w - 10.f);
	const math::Rect power((w - pw) * 0.5f, h - 34.f, pw, 12.f);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(power.grow(math::Vec(2, 2))), 2.f);
	nvgFillColor(vg, nvgRGB(0x0a, 0x0a, 0x0a));
	nvgFill(vg);
	nvgBeginPath(vg);
	for (float x = power.pos.x + 3.f; x < power.getRight(); x += 4.f)
		nvgRect(vg, x, power.pos.y + 1.f, 1.f, power.size.y - 2.f);
	nvgFillColor(vg, nvgRGB(0xd7, 0xc3, 0x8f));
	nvgFill(vg);

	// Screws
	for (const math::Vec p : {math::Vec(app::RACK_GRID_WIDTH * 0.5f, app::RACK_GRID_WIDTH * 0.5f),
	                          math::Vec(w - app::RACK_GRID_WIDTH * 0.5f, h - app::RACK_GRID_WIDTH * 0.5f)}) {
		draw::screw(vg, p, 4.5f, 0.6f);
	}

	// Jack plates with labels; outputs are tinted amber
	std::vector<std::pair<app::PortWidget*, math::Rect>> ports;
	collectPorts(mw, mw, ports);
	for (const std::pair<app::PortWidget*, math::Rect>& p : ports) {
		const math::Rect& r = p.second;
		const bool output = p.first->type == engine::Port::OUTPUT;
		const math::Rect plate(r.pos.x - 3.f, r.pos.y - 3.f, r.size.x + 6.f, r.size.y + 15.f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, RECT_ARGS(plate), 4.f);
		nvgFillColor(vg, output ? nvgRGBAf(0.95f, 0.61f, 0.22f, 0.22f) : nvgRGBAf(0, 0, 0, 0.45f));
		nvgFill(vg);
		if (output) {
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, color::alpha(t.accent, 0.5f));
			nvgStroke(vg);
		}
		draw::text(vg, labelFont(), 7.5f, output ? nvgRGB(0xff, 0xd9, 0xa8) : nvgRGB(0xf1, 0xf1, 0xf1),
		           math::Vec(r.pos.x + r.size.x * 0.5f, r.getBottom() + 6.5f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, jackLabel(p.first), 0.3f);
	}
}


void drawModuleBack(app::ModuleWidget* const mw, const widget::Widget::DrawArgs& args) {
	NVGcontext* const vg = args.vg;
	nvgSave(vg);
	if (mw->module != nullptr && mw->module->isBypassed())
		nvgAlpha(vg, 0.33f);

	drawBackPanel(mw, vg);

	// Sockets only. Containers holding them may draw front artwork themselves, so draw the ports directly.
	for (app::PortWidget* const port : mw->getPorts()) {
		if (!port->isVisible())
			continue;
		const math::Vec offset = port->getRelativeOffset(math::Vec(), mw);
		widget::Widget::DrawArgs portArgs = args;
		portArgs.clipBox = math::Rect(args.clipBox.pos.minus(offset), args.clipBox.size);
		nvgSave(vg);
		nvgTranslate(vg, VEC_ARGS(offset));
		port->draw(portArgs);
		nvgRestore(vg);
	}
	nvgRestore(vg);
}


} // namespace patchcore
} // namespace rack
