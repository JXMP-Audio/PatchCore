/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PatchCore.hpp"

#include <asset.hpp>
#include <color.hpp>
#include <context.hpp>
#include <logger.hpp>
#include <system.hpp>
#include <app/CableWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <patch.hpp>
#include <widget/FramebufferWidget.hpp>

#include <cstdlib>

#include <jansson.h>

#ifdef NDEBUG
# undef DEBUG
#endif

#include "../CardinalCommon.hpp"

namespace rack {
namespace patchcore {


Settings settings;
ViewSide viewSide = VIEW_FRONT;


static Theme makeDarkTheme() {
	Theme t;
	t.chrome0 = color::fromHexString("#111214");
	t.chrome1 = color::fromHexString("#18191c");
	t.chrome2 = color::fromHexString("#202226");
	t.chrome3 = color::fromHexString("#2a2c31");
	t.chrome4 = color::fromHexString("#34373d");
	t.line = color::fromHexString("#3a3d44");
	t.lineSoft = color::fromHexString("#2b2d32");
	t.text = color::fromHexString("#d7d9dd");
	t.textDim = color::fromHexString("#8c919a");
	t.textFaint = color::fromHexString("#5e636b");
	t.accent = color::fromHexString("#f29b38");
	t.select = color::fromHexString("#5aaeff");
	t.rec = color::fromHexString("#e5413a");
	t.play = color::fromHexString("#4fd17a");
	t.lcdBg = color::fromHexString("#0a130d");
	t.lcdFg = color::fromHexString("#86f5a8");
	t.lcdDim = color::fromHexString("#2c5a3a");
	t.wall = color::fromHexString("#141518");
	return t;
}


const Theme& theme() {
	static const Theme dark = makeDarkTheme();
	return dark;
}


bool enabled() {
	return settings.theme != THEME_CLASSIC && !isMini();
}


bool isBackView() {
	return enabled() && viewSide == VIEW_BACK;
}


PortSide portSide(app::PortWidget* const port) {
	app::ModuleWidget* const mw = port->getAncestorOfType<app::ModuleWidget>();
	if (const SidedModuleWidget* const sided = dynamic_cast<const SidedModuleWidget*>(mw))
		return sided->portSide(port->type, port->portId);
	return SIDE_FRONT;
}


bool hasBackPorts(app::ModuleWidget* const mw) {
	if (dynamic_cast<SidedModuleWidget*>(mw) == nullptr)
		return false;
	for (app::PortWidget* const port : mw->getPorts()) {
		if (portSide(port) == SIDE_BACK)
			return true;
	}
	return false;
}


PortSide cableSide(app::CableWidget* const cw) {
	if (cw->outputPort != nullptr && cw->inputPort != nullptr)
		return portSide(cw->outputPort) == portSide(cw->inputPort) ? portSide(cw->outputPort) : SIDE_FRONT;
	if (cw->outputPort != nullptr)
		return portSide(cw->outputPort);
	if (cw->inputPort != nullptr)
		return portSide(cw->inputPort);
	return SIDE_FRONT;
}


static std::string settingsPath() {
	return asset::config("PatchCore.json");
}


void loadSettings() {
	const std::string path = settingsPath();
	if (!system::isFile(path))
		return;

	json_error_t error;
	json_t* const rootJ = json_load_file(path.c_str(), 0, &error);
	if (!rootJ) {
		WARN("PatchCore: could not parse %s: %s", path.c_str(), error.text);
		return;
	}

	if (json_t* const j = json_object_get(rootJ, "theme")) {
		const std::string s = json_string_value(j) ? json_string_value(j) : "";
		settings.theme = s == "classic" ? THEME_CLASSIC : THEME_DARK;
	}
	if (json_t* const j = json_object_get(rootJ, "sheen"))
		settings.sheen = math::clamp((float) json_number_value(j), 0.f, 1.f);
	if (json_t* const j = json_object_get(rootJ, "mountScrews"))
		settings.mountScrews = json_boolean_value(j);
	if (json_t* const j = json_object_get(rootJ, "navigatorVisible"))
		settings.navigatorVisible = json_boolean_value(j);
	if (json_t* const j = json_object_get(rootJ, "transportVisible"))
		settings.transportVisible = json_boolean_value(j);
	if (json_t* const j = json_object_get(rootJ, "wheelKnobs"))
		settings.wheelKnobs = json_boolean_value(j);
	if (json_t* const j = json_object_get(rootJ, "wheelSensitivity"))
		settings.wheelSensitivity = math::clamp((int) json_integer_value(j), 0, 2);

	json_decref(rootJ);
}


void saveSettings() {
	json_t* const rootJ = json_object();
	json_object_set_new(rootJ, "theme", json_string(settings.theme == THEME_CLASSIC ? "classic" : "dark"));
	json_object_set_new(rootJ, "sheen", json_real(settings.sheen));
	json_object_set_new(rootJ, "mountScrews", json_boolean(settings.mountScrews));
	json_object_set_new(rootJ, "navigatorVisible", json_boolean(settings.navigatorVisible));
	json_object_set_new(rootJ, "transportVisible", json_boolean(settings.transportVisible));
	json_object_set_new(rootJ, "wheelKnobs", json_boolean(settings.wheelKnobs));
	json_object_set_new(rootJ, "wheelSensitivity", json_integer(settings.wheelSensitivity));

	const std::string path = settingsPath();
	if (json_dump_file(rootJ, path.c_str(), JSON_INDENT(2)) != 0)
		WARN("PatchCore: could not save %s", path.c_str());

	json_decref(rootJ);
}


std::shared_ptr<window::Font> uiFont() {
	return APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
}


std::shared_ptr<window::Font> labelFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}


std::shared_ptr<window::Font> monoFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}


void setAllFramebuffersDirty(widget::Widget* const w) {
	if (widget::FramebufferWidget* const fbw = dynamic_cast<widget::FramebufferWidget*>(w))
		fbw->setDirty();
	for (widget::Widget* const child : w->children)
		setAllFramebuffersDirty(child);
}


// Flip animation: the rack squashes horizontally, swaps sides at the midpoint, then expands again
static constexpr const double kFlipHalfDuration = 0.09;
static double flipTime = -INFINITY;
static ViewSide flipTarget = VIEW_FRONT;


void setViewSide(ViewSide side) {
	// A cable being dragged must stay on its side: a flip now would let it end on the other side
	if (APP->scene->rack->getIncompleteCable() != nullptr)
		return;
	if (side == viewSide && side == flipTarget)
		return;
	flipTarget = side;
	flipTime = system::getTime();
}


void stepFlip() {
	if (flipTarget != viewSide && timeSinceFlip() >= kFlipHalfDuration)
		viewSide = flipTarget;
}


float flipScaleX() {
	const double t = timeSinceFlip();
	if (t >= kFlipHalfDuration * 2)
		return 1.f;
	return std::max(0.02f, (float) std::fabs(std::cos(M_PI * 0.5 * t / kFlipHalfDuration)));
}


ViewSide targetViewSide() {
	return flipTarget;
}




double timeSinceFlip() {
	return system::getTime() - flipTime;
}


void devStep() {
	static int frame = 0;
	++frame;

	if (frame == 1) {
		if (const char* const path = std::getenv("PATCHCORE_DEV_PATCH")) {
			try {
				APP->patch->load(path);
				APP->patch->path = path;
			}
			catch (Exception& e) {
				WARN("PatchCore: could not load %s: %s", path, e.what());
			}
		}
		// Runtime only, never saved
		if (const char* const themeName = std::getenv("PATCHCORE_DEV_THEME"))
			settings.theme = std::string(themeName) == "classic" ? THEME_CLASSIC : THEME_DARK;
		if (const char* const view = std::getenv("PATCHCORE_DEV_VIEW"))
			viewSide = flipTarget = std::string(view) == "back" ? VIEW_BACK : VIEW_FRONT;
	}

	// Applied a few frames later, after the patch has set its own zoom and offset
	if (frame == 3) {
		if (const char* const zoom = std::getenv("PATCHCORE_DEV_ZOOM"))
			APP->scene->rackScroll->setZoom(std::atof(zoom));
		if (const char* const offset = std::getenv("PATCHCORE_DEV_OFFSET")) {
			float x = 0.f, y = 0.f;
			if (std::sscanf(offset, "%f,%f", &x, &y) == 2)
				APP->scene->rackScroll->setGridOffset(math::Vec(x, y));
		}
		if (const char* const select = std::getenv("PATCHCORE_DEV_SELECT")) {
			const std::vector<app::ModuleWidget*> modules = APP->scene->rack->getModules();
			const size_t index = std::atoi(select);
			if (index < modules.size())
				APP->scene->rack->select(modules[index]);
		}
	}
}


namespace draw {


void bevelBox(NVGcontext* vg, math::Rect r, float radius, NVGcolor top, NVGcolor bottom, NVGcolor border) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(r), radius);
	nvgFillPaint(vg, nvgLinearGradient(vg, r.pos.x, r.pos.y, r.pos.x, r.pos.y + r.size.y, top, bottom));
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.f);
	nvgStrokeColor(vg, border);
	nvgStroke(vg);

	// light top edge
	nvgBeginPath(vg);
	nvgMoveTo(vg, r.pos.x + radius, r.pos.y + 1.f);
	nvgLineTo(vg, r.pos.x + r.size.x - radius, r.pos.y + 1.f);
	nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, 0.12f));
	nvgStroke(vg);
}


void insetBox(NVGcontext* vg, math::Rect r, float radius, NVGcolor bg) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(r), radius);
	nvgFillColor(vg, bg);
	nvgFill(vg);

	// inner shadow from the top
	nvgBeginPath(vg);
	nvgRoundedRect(vg, RECT_ARGS(r), radius);
	nvgFillPaint(vg, nvgBoxGradient(vg, r.pos.x, r.pos.y + 2.f, r.size.x, r.size.y, radius, 5.f,
	                                nvgRGBAf(0, 0, 0, 0), nvgRGBAf(0, 0, 0, 0.6f)));
	nvgFill(vg);

	// lit bottom lip
	nvgBeginPath(vg);
	nvgMoveTo(vg, r.pos.x + radius, r.pos.y + r.size.y + 0.5f);
	nvgLineTo(vg, r.pos.x + r.size.x - radius, r.pos.y + r.size.y + 0.5f);
	nvgStrokeWidth(vg, 1.f);
	nvgStrokeColor(vg, color::fromHexString("#3a3d44"));
	nvgStroke(vg);
}


void screw(NVGcontext* vg, math::Vec c, float radius, float angle) {
	// head
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, radius);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x - radius * 0.35f, c.y - radius * 0.35f, radius * 0.1f, radius * 1.2f,
	                                   nvgRGB(0xe6, 0xe8, 0xeb), nvgRGB(0x6c, 0x70, 0x77)));
	nvgFill(vg);
	nvgStrokeWidth(vg, 0.8f);
	nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 0x90));
	nvgStroke(vg);

	// cross slot
	nvgSave(vg);
	nvgTranslate(vg, c.x, c.y);
	nvgRotate(vg, angle);
	const float s = radius * 0.62f;
	nvgBeginPath(vg);
	nvgMoveTo(vg, -s, 0);
	nvgLineTo(vg, s, 0);
	nvgMoveTo(vg, 0, -s);
	nvgLineTo(vg, 0, s);
	nvgStrokeWidth(vg, radius * 0.32f);
	nvgLineCap(vg, NVG_ROUND);
	nvgStrokeColor(vg, nvgRGBA(0x2a, 0x2c, 0x30, 0xe0));
	nvgStroke(vg);
	nvgRestore(vg);
}


void hole(NVGcontext* vg, math::Vec c, float radius) {
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y + 0.6f, radius);
	nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.18f));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, radius);
	nvgFillColor(vg, nvgRGB(0x0b, 0x0c, 0x0e));
	nvgFill(vg);
}


void text(NVGcontext* vg, std::shared_ptr<window::Font> font, float size, NVGcolor color, math::Vec pos, int align,
          const std::string& s, float letterSpacing) {
	if (!font || font->handle < 0)
		return;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, letterSpacing);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, color);
	nvgText(vg, pos.x, pos.y, s.c_str(), nullptr);
}


void lcdText(NVGcontext* vg, float size, math::Vec pos, int align, const std::string& s, NVGcolor color) {
	std::shared_ptr<window::Font> font = monoFont();
	if (!font || font->handle < 0)
		return;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, 0.f);
	nvgTextAlign(vg, align);
	// glow
	nvgFontBlur(vg, 4.f);
	nvgFillColor(vg, color::alpha(color, 0.45f));
	nvgText(vg, pos.x, pos.y, s.c_str(), nullptr);
	nvgFontBlur(vg, 0.f);
	nvgFillColor(vg, color);
	nvgText(vg, pos.x, pos.y, s.c_str(), nullptr);
}


} // namespace draw
} // namespace patchcore
} // namespace rack
