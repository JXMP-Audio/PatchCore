/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <math.hpp>
#include <nanovg.h>
#include <widget/Widget.hpp>
#include <window/Window.hpp>
#include <patchcore/SidedModuleWidget.hpp>

namespace rack {

namespace app {
struct CableWidget;
struct ModuleWidget;
struct PortWidget;
struct RackWidget;
}

namespace plugin {
struct Model;
}

namespace patchcore {

enum ThemeId {
	THEME_CLASSIC = 0,
	THEME_DARK,
};

enum ViewSide {
	VIEW_FRONT = 0,
	VIEW_BACK,
};

/** Color tokens, mirroring the `:root` block of research_docs/mockups/index.html */
struct Theme {
	NVGcolor chrome0, chrome1, chrome2, chrome3, chrome4;
	NVGcolor line, lineSoft;
	NVGcolor text, textDim, textFaint;
	NVGcolor accent, select, rec, play;
	NVGcolor lcdBg, lcdFg, lcdDim;
	NVGcolor wall;
};

/** Persistent PatchCore options, stored in the `PatchCore.json` sidecar next to Cardinal's own settings. */
struct Settings {
	int theme = THEME_DARK;
	/** Panel gloss/vignette amount, 0..1 */
	float sheen = 0.6f;
	/** Draw mount screws on modules whose panel has no screw widgets */
	bool mountScrews = true;
	bool browserDocked = true;
	bool navigatorVisible = true;
	bool transportVisible = true;
};

extern Settings settings;

/** Current rack side. Runtime only, always starts at the front. */
extern ViewSide viewSide;

void loadSettings();
void saveSettings();

/** True when the PatchCore look is active (not Classic, and not the Mini variant). */
bool enabled();

/** True when the rack shows its back: PatchCore enabled and flipped. */
bool isBackView();

/** Side of a socket: what its module widget says, SIDE_FRONT for basic modules. */
PortSide portSide(app::PortWidget* port);

/** True when the module widget is a SidedModuleWidget with at least one back port. */
bool hasBackPorts(app::ModuleWidget* mw);

/** Side a cable is drawn on: the side of its ports. A cable whose ends disagree (hand-edited patch) is on the front. */
PortSide cableSide(app::CableWidget* cw);

const Theme& theme();

/** Fonts that ship with Rack in res/fonts, available in every plugin format. */
std::shared_ptr<window::Font> uiFont();
std::shared_ptr<window::Font> labelFont();
std::shared_ptr<window::Font> monoFont();

/** Mark every FramebufferWidget below `w` dirty, used after theme switches. */
void setAllFramebuffersDirty(widget::Widget* w);


namespace draw {

/** Vertical gradient box with a 1px light top edge and a dark border, like the mockup's buttons and bars. */
void bevelBox(NVGcontext* vg, math::Rect r, float radius, NVGcolor top, NVGcolor bottom, NVGcolor border);

/** Recessed box with an inner shadow, used for LCDs and inputs. */
void insetBox(NVGcontext* vg, math::Rect r, float radius, NVGcolor bg);

/** Phillips screw head. */
void screw(NVGcontext* vg, math::Vec center, float radius, float angle);

/** Round rack hole (dark with a lit lower lip). */
void hole(NVGcontext* vg, math::Vec center, float radius);

/** Single line of text. `align` uses NVG_ALIGN_* flags. Letter spacing in px. */
void text(NVGcontext* vg, std::shared_ptr<window::Font> font, float size, NVGcolor color, math::Vec pos, int align,
          const std::string& s, float letterSpacing = 0.f);

/** Text with a soft glow, for LCD readouts. */
void lcdText(NVGcontext* vg, float size, math::Vec pos, int align, const std::string& s, NVGcolor color);

} // namespace draw


/** Switch the rack between front and back, starting the flip animation. */
void setViewSide(ViewSide side);

/** Show or hide the docked module browser. */
void toggleBrowser();

/** Side the rack is flipping to (equals viewSide when no flip is running). */
ViewSide targetViewSide();

/** Seconds since the last flip started. */
double timeSinceFlip();

/** Advances the flip animation; swaps viewSide at its midpoint. Called from Scene::step(). */
void stepFlip();

/** Horizontal scale of the rack view during the flip animation, 1 when idle. */
float flipScaleX();


// Chrome helpers (TopBar.cpp)

float textWidth(std::shared_ptr<window::Font> font, float size, const std::string& s, float letterSpacing);
/** Gradient bar background used by the top bar and the transport bar. */
void drawBarBackground(NVGcontext* vg, math::Vec size, bool topEdgeIsBorder);
/** PatchCore look for the File/Edit/View/Engine/Help buttons. */
void drawMenuButton(NVGcontext* vg, math::Vec size, const std::string& text, bool hovered);
float menuButtonWidth(const std::string& text);

widget::Widget* createTopBarLogo();
/** View segment (Rack/Back/Browser/Mixer) and the patch name LCD. */
widget::Widget* createTopBarCenter();
/** Sheen slider and selection status. */
widget::Widget* createTopBarRight();

/** Right-edge minimap of the rack with a draggable viewport (Navigator.cpp). */
widget::Widget* createNavigator();
/** Background color of a module panel, read from its SVG. */
NVGcolor panelColor(app::ModuleWidget* mw);


// Back panel (BackPanel.cpp)

/** Draws the generated back plate of a module: plate, stencilled name, sticker, power header and jack labels. */
void drawBackPanel(app::ModuleWidget* mw, NVGcontext* vg);
/** Draws the whole back of a module (plate and sockets) in module coordinates. */
void drawModuleBack(app::ModuleWidget* mw, const widget::Widget::DrawArgs& args);
/** Color of the topmost solid panel SVG shape under `point` (module coordinates) with an area of at least `minArea`. */
bool panelColorAt(app::ModuleWidget* mw, math::Vec point, float minArea, NVGcolor* color);


// Docked browser (DockedBrowser.cpp, with module creation helpers in custom/Browser.cpp)

widget::Widget* createDockedBrowser();
/** Fuzzy search over brand, plugin, name, description and tags; best matches first. */
std::vector<plugin::Model*> searchModels(const std::string& search);
/** Adds a module at the mouse position, like clicking it in the modal browser. */
app::ModuleWidget* addModelAtMouse(plugin::Model* model);
/** Adds a module at the nearest free rack slot to `pos` (rack coordinates). */
app::ModuleWidget* addModelAt(plugin::Model* model, math::Vec pos);


// Transport (PatchCoreUI.cpp, built per plugin variant)

widget::Widget* createTransportBar();
/** True when the internal transport drives the engine (standalone without a timebase master). */
bool transportIsInternal();
void transportTogglePlay();


/** Development aid, called from Scene::step(). Reads PATCHCORE_DEV_* environment variables once:
PATCHCORE_DEV_PATCH (patch to load), PATCHCORE_DEV_THEME ("classic", not saved), PATCHCORE_DEV_ZOOM, PATCHCORE_DEV_OFFSET ("x,y" grid offset),
PATCHCORE_DEV_VIEW ("back"), PATCHCORE_DEV_SELECT (index of a module to select). */
void devStep();

/** Draws the 19" rack frame (ears, rails, recesses) around the occupied rack rows.
Inserted into RackWidget below the module container. Hides the stock RailWidget while PatchCore is enabled. */
widget::Widget* createRackFrame(app::RackWidget* rack);

} // namespace patchcore

namespace app {
/** Applies the PatchCore view (front without sockets, or back) to a module's children. Defined in override/ModuleWidget.cpp. */
void ModuleWidget_syncPatchCoreView(ModuleWidget* mw);
}

} // namespace rack
