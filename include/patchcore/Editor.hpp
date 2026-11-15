/*
 * PatchCore frontend for DISTRHO Cardinal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Editor view (standalone only). The core shows the view and the Editor's controls in the transport bar while
 * an editor module is in the rack; the module (JX-EDIT in the JXMP plugin) builds its content.
 */

#pragma once

namespace rack {
namespace widget {
struct Widget;
}

namespace patchcore {

/** Implemented by the widget of an editor module, next to app::ModuleWidget. */
struct EditorProvider;
/** Called by a provider while it is destroyed (its module is still alive then): the view drops the content it built
at once, before the module goes. Defined with the view. */
void editorProviderGone(EditorProvider* provider);

struct EditorProvider {
	virtual ~EditorProvider() {
		editorProviderGone(this);
	}
	/** Builds the content of the Editor view. The core owns it, sets its box every frame and deletes it when the view
	closes or the module goes away. */
	virtual widget::Widget* createEditorView() = 0;
	/** False for a second editor that stays inactive, and in a plugin (the DAW is the editor there). */
	virtual bool editorActive() {
		return true;
	}
};

/** Opens or closes the Editor view (it closes by itself when no editor module is in the rack). Opening it closes
the Mixer view and the other way round. Defined in src/patchcore/EditorView.cpp. */
void showEditor(bool show);
bool editorShown();
/** True when the rack has an active editor module: the Editor view and the transport's REC, LOOP, CLICK,
position and time exist. */
bool editorAvailable();

} // namespace patchcore
} // namespace rack
