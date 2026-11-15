/*
 * JXMP Audio modules for PatchCore
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * JX-EDIT: the Editor's content. Track list on the left; on the right the ruler (START / STOP of the loop), an
 * overview of all 8 tracks over the whole song, the piano roll of the selected track and its velocity lane.
 */

#include "Edit.hpp"
#include <patchcore/Editor.hpp>
#include <ui/MenuOverlay.hpp>
#include <ui/TextField.hpp>
#include <widget/ZoomWidget.hpp>

namespace jxedit {

namespace st = jxmp::style;

namespace {

const float kListW = 200.f, kToolH = 30.f, kRulerH = 22.f, kOverRulerH = 18.f, kOverRowH = 20.f, kGap = 6.f, kVelH = 80.f,
            kKeysW = 44.f;
const float kRowH = 11.f;
const float kTrackRowH = 26.f;

struct GridChoice {
	const char* name;
	int64_t ticks;
};
const GridChoice kGrids[] = {
    {"1/4", sg::kPpq}, {"1/8", sg::kPpq / 2}, {"1/16", sg::kPpq / 4}, {"1/32", sg::kPpq / 8},
    {"1/8 T", sg::kPpq / 3}, {"1/16 T", sg::kPpq / 6}, {"OFF", 1},
};

const char* gridName(int64_t g) {
	for (const GridChoice& c : kGrids)
		if (c.ticks == g)
			return c.name;
	return "?";
}

bool blackKey(int pitch) {
	const int n = ((pitch % 12) + 12) % 12;
	return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

/** Renames a track in place (a double click on its name): Enter or a click elsewhere keeps the name, Esc restores it */
struct NameField : ui::TextField {
	Edit* module = nullptr;
	int track = 0;
	std::string before;
	std::function<void()> done;
	bool finished = false;

	void finish(bool keep) {
		if (finished)
			return;
		finished = true;
		if (module) {
			std::string name = keep ? utf8Cut(text, 24) : before;
			if (name.empty())
				name = string::f("TRACK %d", track + 1);
			module->song.tracks[track].name = name;
			module->publish();
			// Esc: nothing changed, so no undo step either
			module->commit(true);
			if (!keep && !module->undo.empty() && module->undo.back() == module->song)
				module->undo.pop_back();
		}
		if (done)
			done();
		requestDelete();
	}

	void onChange(const ChangeEvent& e) override {
		ui::TextField::onChange(e);
		if (module && !finished)
			module->song.tracks[track].name = utf8Cut(text, 24);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER || e.key == GLFW_KEY_ESCAPE)) {
			finish(e.key != GLFW_KEY_ESCAPE);
			APP->event->setSelectedWidget(nullptr);
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
		// While typing, PgUp / PgDn do not switch the track under the field
		if (e.key == GLFW_KEY_PAGE_UP || e.key == GLFW_KEY_PAGE_DOWN)
			e.consume(this);
	}

	// Drawn like the list (dark, its font, one line), not as Rack's light text box
	static constexpr float kSize = 10.f, kPad = 4.f;

	bool font(NVGcontext* vg) {
		std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL);
		if (!f || f->handle < 0)
			return false;
		nvgFontFaceId(vg, f->handle);
		nvgFontSize(vg, kSize);
		nvgTextLetterSpacing(vg, 0.5f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		return true;
	}

	/** x of each byte position of the text (from 0), with the text scrolled so the cursor shows */
	std::vector<float> xs;
	float shift = 0.f;
	void measure(NVGcontext* vg) {
		xs.assign(text.size() + 1, 0.f);
		std::vector<NVGglyphPosition> g(text.size() + 1);
		const int n = nvgTextGlyphPositions(vg, 0, 0, text.c_str(), nullptr, g.data(), (int) g.size());
		float end = 0.f;
		for (int k = 0; k < n; ++k)
			end = g[k].maxx;
		size_t gi = 0;
		for (size_t b = 0; b <= text.size(); ++b) {
			while (gi < (size_t) n && (size_t) (g[gi].str - text.c_str()) < b)
				++gi;
			xs[b] = gi < (size_t) n ? g[gi].x : end;
		}
		const float room = box.size.x - 2 * kPad;
		const float cx = xs[math::clamp(cursor, 0, (int) text.size())];
		if (cx + shift > room)
			shift = room - cx;
		if (cx + shift < 0.f)
			shift = -cx;
		if (end + shift < room)
			shift = std::min(0.f, room - end);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* const vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(vg, nvgRGB(0x12, 0x13, 0x16));
		nvgFill(vg);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, kEditColour);
		nvgStroke(vg);
		nvgSave(vg);
		nvgIntersectScissor(vg, kPad - 1.f, 0, box.size.x - 2 * kPad + 2.f, box.size.y);
		if (font(vg)) {
			measure(vg);
			const float x0 = kPad + shift, cy = box.size.y / 2;
			const int a = math::clamp(std::min(cursor, selection), 0, (int) text.size());
			const int b = math::clamp(std::max(cursor, selection), 0, (int) text.size());
			if (a != b) {
				nvgBeginPath(vg);
				nvgRect(vg, x0 + xs[a], cy - 7.f, xs[b] - xs[a], 14.f);
				nvgFillColor(vg, color::alpha(kEditColour, 0.45f));
				nvgFill(vg);
			}
			nvgFillColor(vg, st::legend());
			nvgText(vg, x0, cy, text.c_str(), nullptr);
			if (APP->event->selectedWidget == this) {
				nvgBeginPath(vg);
				nvgRect(vg, x0 + xs[math::clamp(cursor, 0, (int) text.size())], cy - 6.f, 1.f, 12.f);
				nvgFillColor(vg, color::WHITE);
				nvgFill(vg);
			}
		}
		nvgRestore(vg);
	}

	int getTextPosition(math::Vec mousePos) override {
		NVGcontext* const vg = APP->window->vg;
		nvgSave(vg);
		const bool ok = font(vg);
		if (ok)
			measure(vg);
		nvgRestore(vg);
		if (!ok || xs.empty())
			return 0;
		const float x = mousePos.x - kPad - shift;
		int best = 0;
		for (size_t b = 0; b < xs.size(); ++b) {
			// Only on letter boundaries (not inside a UTF-8 letter)
			if (b < text.size() && (text[b] & 0xC0) == 0x80)
				continue;
			if (std::fabs(xs[b] - x) < std::fabs(xs[best] - x))
				best = (int) b;
		}
		return best;
	}

	void onDeselect(const DeselectEvent& e) override {
		ui::TextField::onDeselect(e);
		finish(true);
	}
};

struct Button {
	math::Rect rect;
	std::string label;
	std::function<void()> action;
	bool enabled;  // dimmed and inactive when it would do nothing
};

} // namespace


struct EditPane : widget::OpaqueWidget {
	Edit* m = nullptr;
	std::vector<char> sel;  // selection of the selected track's notes, parallel to its notes
	int selTrack = -1;
	std::vector<sg::Note> clip;
	int64_t lastPaste = -1, lastPasteFrom = -1;  // where the last paste went and where it was asked for
	std::vector<Button> buttons;

	enum DragType { DRAG_NONE, DRAG_MOVE, DRAG_LENGTH, DRAG_BOX, DRAG_VEL, DRAG_LOOP_START, DRAG_LOOP_END, DRAG_SEEK, DRAG_SEEK_OVER, DRAG_OVERVIEW };
	DragType drag = DRAG_NONE;
	math::Vec dragFrom, dragNow;
	struct Orig {
		size_t index;
		int64_t start, length;
		int pitch, vel;
	};
	std::vector<Orig> orig;

	// ---- geometry
	float rightX() const { return kListW; }
	// The overview (whole song) with its own ruler on top; the piano roll with its own ruler below it
	float overRulerY() const { return kToolH; }
	float overY() const { return kToolH + kOverRulerH; }
	float overBottom() const { return overY() + sg::kTracks * overRowH(); }
	float rulerY() const { return overBottom() + kGap; }
	float rollY() const { return rulerY() + kRulerH; }
	/** Overview scale: the whole song across the width */
	double overScale() const { return (box.size.x - noteX0()) / (double) songTicks(); }
	float overX(double tick) const { return noteX0() + tick * overScale(); }
	double overTick(float x) const { return (x - noteX0()) / overScale(); }
	/** Every n-th bar gets a number, so the numbers are at least `minPx` apart */
	int barStep(double pxPerBar, float minPx) const {
		const int steps[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
		for (int st : steps)
			if (pxPerBar * st >= minPx)
				return st;
		return 512;
	}
	float velY() const { return box.size.y - velH(); }
	/** In a low window the overview rows and then the velocity lane give up height, so the piano roll keeps about
	 * 16 rows; the minimum window still shows everything */
	float fixedH() const { return kToolH + kOverRulerH + kGap + kRulerH + kGap; }
	float overRowH() const {
		return math::clamp((box.size.y - fixedH() - kVelH - 16 * kRowH) / sg::kTracks, 9.f, kOverRowH);
	}
	float velH() const {
		return math::clamp(box.size.y - fixedH() - sg::kTracks * overRowH() - 16 * kRowH, 40.f, kVelH);
	}
	float rollH() const { return velY() - kGap - rollY(); }
	float noteX0() const { return rightX() + kKeysW; }

	double tickAt(float x) const { return (x - noteX0()) / m->zoom + m->scroll; }
	float xOf(double tick) const { return noteX0() + (tick - m->scroll) * m->zoom; }
	float yOf(int pitch) const { return rollY() + (m->topPitch - pitch) * kRowH; }
	int pitchAt(float y) const { return m->topPitch - (int) std::floor((y - rollY()) / kRowH); }
	/** The highest row can go down only until the lowest note (C-1, 0) is at the bottom: nothing below it */
	int minTop() const { return math::clamp((int) (rollH() / kRowH) - 1, 0, 127); }
	int clampTop(int top) const { return math::clamp(top, minTop(), 127); }

	/** The shortest loop: a grid step, at least a 1/16 (as a patch is loaded) */
	double minLoop() const { return (double) std::max<int64_t>(m->grid, sg::kPpq / 4); }
	int64_t snapDown(double t) const { return (int64_t) std::floor(t / m->grid) * m->grid; }
	int64_t snapNear(double t) const { return (int64_t) std::llround(t / m->grid) * m->grid; }

	sg::Track& track() { return m->song.tracks[m->selected]; }

	// The track settings under the list
	float panelY() const { return kToolH + sg::kTracks * kTrackRowH + 18.f; }
	float chanY() const { return panelY() + 24.f; }
	float polyY() const { return panelY() + 48.f; }
	float modeY() const { return panelY() + 72.f; }
	float infoY() const { return panelY() + 104.f; }  // the information under the controls
	math::Rect chanMinusRect() const { return math::Rect(96.f, chanY() - 9.f, 20.f, 18.f); }
	math::Rect chanPlusRect() const { return math::Rect(166.f, chanY() - 9.f, 20.f, 18.f); }
	math::Rect polyMinusRect() const { return math::Rect(96.f, polyY() - 9.f, 20.f, 18.f); }
	math::Rect polyPlusRect() const { return math::Rect(166.f, polyY() - 9.f, 20.f, 18.f); }
	math::Rect modeRect() const { return math::Rect(96.f, modeY() - 9.f, 90.f, 18.f); }

	int channel() { return track().channel; }

	/** Polyphony and mode belong to the MIDI channel (its Host MIDI): every track on it shares them */
	void setPoly(int poly, int mode) {
		m->remember();
		const int c = channel();
		m->song.poly[c] = math::clamp(poly, 1, 16);
		m->song.polyMode[c] = ((mode % 3) + 3) % 3;
		m->editorWins[c] = true;
		m->syncHostMidi(true);
		m->publish();
	}

	void setChannel(int c) {
		m->remember();
		track().channel = math::clamp(c, 0, 15);
		m->syncHostMidi(true);
		m->publish();
	}

	/** The overview's length: its own zoom (at least `overviewBars`), always longer than the song and than what the
	 * piano roll shows */
	int64_t overLen = 0;  // the overview's length while the mouse drags in it (else it would grow under the mouse)
	int64_t songTicks() const {
		if (overLen > 0)
			return overLen;
		const int64_t bar = sg::kPpq * barBeats();
		const double viewEnd = m->scroll + (box.size.x - noteX0()) / m->zoom;
		int64_t end = std::max<int64_t>(m->song.lastTick() + 2 * bar, (int64_t) m->overviewBars * bar);
		end = std::max<int64_t>(end, (int64_t) std::ceil(viewEnd));
		return (end + bar - 1) / bar * bar;
	}

	int barBeats() const {
		CardinalPluginContext* const c = m->context();
		return c ? std::max(1, (int) c->internalTransportBeatsPerBar) : 4;
	}

	double framesPerTick() const {
		CardinalPluginContext* const c = m->context();
		const double bpm = c ? std::max(1.0, (double) c->internalTransportBeatsPerMinute) : 120.0;
		return m->sampleRate() * 60.0 / bpm / sg::kPpq;
	}

	/** The mouse in this pane's own (unzoomed) coordinates */
	math::Vec mouse() {
		return APP->scene->getMousePos().minus(getAbsoluteOffset(math::Vec())).div(getAbsoluteZoom());
	}

	// ---- selection
	void syncSel() {
		if (selTrack != m->selected || sel.size() != track().notes.size()) {
			sel.assign(track().notes.size(), 0);
			selTrack = m->selected;
		}
	}

	void clearSel() {
		std::fill(sel.begin(), sel.end(), 0);
	}

	/** Sorts the notes by start and keeps the selection with them */
	void sortNotes() {
		std::vector<sg::Note>& notes = track().notes;
		std::vector<size_t> idx(notes.size());
		for (size_t k = 0; k < idx.size(); ++k)
			idx[k] = k;
		std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
		std::vector<sg::Note> n2;
		std::vector<char> s2;
		n2.reserve(notes.size());
		s2.reserve(notes.size());
		for (size_t k : idx) {
			n2.push_back(notes[k]);
			s2.push_back(k < sel.size() ? sel[k] : 0);
		}
		notes.swap(n2);
		sel.swap(s2);
	}

	void changed() {
		sortNotes();
		m->publish();
	}

	int hitNote(math::Vec p, bool* edge) {
		const std::vector<sg::Note>& notes = track().notes;
		for (int k = (int) notes.size() - 1; k >= 0; --k) {
			const sg::Note& n = notes[k];
			const float x = xOf((double) n.start), w = std::max(3.f, (float) (n.length * m->zoom)), y = yOf(n.pitch);
			if (p.x >= x && p.x < x + w && p.y >= y && p.y < y + kRowH) {
				if (edge)
					*edge = p.x > x + w - std::min(6.f, w / 3.f);
				return k;
			}
		}
		return -1;
	}

	int hitVelocity(float x) {
		const std::vector<sg::Note>& notes = track().notes;
		int best = -1;
		float bestD = 5.f;
		for (int k = 0; k < (int) notes.size(); ++k) {
			const float d = std::fabs(xOf((double) notes[k].start) - x);
			if (d < bestD || (d <= bestD && best >= 0 && sel[k])) {
				bestD = d;
				best = k;
			}
		}
		return best;
	}

	void startOrig() {
		orig.clear();
		const std::vector<sg::Note>& notes = track().notes;
		for (size_t k = 0; k < notes.size(); ++k)
			if (sel[k])
				orig.push_back(Orig{k, notes[k].start, notes[k].length, notes[k].pitch, notes[k].vel});
	}

	// ---- the note heard while one is drawn or dragged
	int previewBase = -1;  // the pitch of the note under the mouse when the drag began, -1 = none
	int previewVel = 100;
	double previewUntil = 0.0;  // a click alone still sounds this long

	/** Only while the song does not play (paused or stopped): during playback the song itself is heard */
	void hear(int pitch) {
		CardinalPluginContext* const c = m->context();
		if (!c || !m->song.audible(m->selected) || c->internalTransportPlaying)
			return;
		c->editorPreview = sg::NotePreview::pack(track().channel, pitch, previewVel);
		previewUntil = system::getTime() + 0.25;
	}

	void stopHearing() {
		previewBase = -1;
		previewUntil = 0.0;
		if (CardinalPluginContext* const c = m->context())
			c->editorPreview = 0;
	}

	/** The whole song in the roll: from bar 1 to its last note (at least 4 bars), its notes in the middle */
	void fitSong() {
		const int64_t bar = sg::kPpq * barBeats();
		const int64_t end = (std::max<int64_t>(m->song.lastTick(), 4 * bar) + bar - 1) / bar * bar;
		m->zoom = std::min(2.0, std::max(0.005, (box.size.x - noteX0() - 8.f) / (double) end));
		m->scroll = 0.0;
		int lo = 128, hi = -1;
		for (const sg::Track& t : m->song.tracks)
			for (const sg::Note& n : t.notes) {
				lo = std::min(lo, n.pitch);
				hi = std::max(hi, n.pitch);
			}
		if (hi >= 0) {
			const int rows = std::max(1, (int) (rollH() / kRowH));
			m->topPitch = clampTop((lo + hi + 1) / 2 + rows / 2);
		}
	}

	// ---- actions
	void quantize() {
		syncSel();
		m->remember();
		bool any = false;
		for (char s : sel)
			any = any || s;
		std::vector<sg::Note>& notes = track().notes;
		for (size_t k = 0; k < notes.size(); ++k)
			if (!any || sel[k])
				notes[k].start = std::max<int64_t>(0, snapNear((double) notes[k].start));
		changed();
	}

	void copy() {
		syncSel();
		if (!anySelected())
			return;
		clip.clear();
		lastPaste = -1;
		const std::vector<sg::Note>& notes = track().notes;
		int64_t first = INT64_MAX;
		for (size_t k = 0; k < notes.size(); ++k)
			if (sel[k]) {
				clip.push_back(notes[k]);
				first = std::min(first, notes[k].start);
			}
		for (sg::Note& n : clip)
			n.start -= first;
	}

	void paste() {
		if (clip.empty())
			return;
		syncSel();
		m->remember();
		clearSel();
		const double viewEnd = m->scroll + (box.size.x - noteX0()) / m->zoom;
		const double play = m->playTick();
		int64_t at = std::max<int64_t>(0, snapDown(play >= m->scroll && play < viewEnd ? play : m->scroll + m->grid - 1));
		if (lastPaste >= 0 && at == lastPasteFrom) {
			// Ctrl+V again: after the previous one (never the same notes twice in one place)
			int64_t span = 0;
			for (const sg::Note& c : clip)
				span = std::max(span, c.end());
			at = lastPaste + (span + m->grid - 1) / m->grid * m->grid;
		}
		else
			lastPasteFrom = at;
		lastPaste = at;
		for (const sg::Note& c : clip) {
			sg::Note n = c;
			n.start += at;
			track().notes.push_back(n);
			sel.push_back(1);
		}
		changed();
	}

	bool anySelected() const {
		for (char c : sel)
			if (c)
				return true;
		return false;
	}

	void deleteSelected() {
		syncSel();
		std::vector<sg::Note>& notes = track().notes;
		bool any = false;
		for (char s : sel)
			any = any || s;
		if (!any)
			return;
		m->remember();
		std::vector<sg::Note> keep;
		for (size_t k = 0; k < notes.size(); ++k)
			if (!sel[k])
				keep.push_back(notes[k]);
		notes.swap(keep);
		sel.assign(notes.size(), 0);
		m->publish();
	}

	void seek(double tick) {
		CardinalPluginContext* const c = m->context();
		if (!c)
			return;
		c->internalTransportSeekFrame = (int64_t) std::llround(std::max(0.0, tick) * framesPerTick());
	}

	/** The roll shows the song around overview position `x` */
	void centreOn(float x) {
		const double view = (box.size.x - noteX0()) / m->zoom;
		m->scroll = std::max(0.0, overTick(std::max(x, noteX0())) - view / 2);
	}

	void selectTrack(int k) {
		m->selected = k;
		syncSel();
		showNotes();
		m->publish();
	}

	/** None of the selected track's notes is in the rows shown: the rows move to the middle of its notes (the time
	 * stays where it is) */
	void showNotes() {
		const std::vector<sg::Note>& notes = track().notes;
		if (notes.empty())
			return;
		const int rows = std::max(1, (int) (rollH() / kRowH));
		const int bottom = m->topPitch - rows + 1;
		int lo = 128, hi = -1;
		for (const sg::Note& n : notes) {
			if (n.pitch <= m->topPitch && n.pitch >= bottom)
				return;
			lo = std::min(lo, n.pitch);
			hi = std::max(hi, n.pitch);
		}
		m->topPitch = clampTop((lo + hi + 1) / 2 + rows / 2);
	}

	NameField* naming = nullptr;  // the name being typed, a child of this pane
	/** The left column (tracks, the track's settings) scrolls with the wheel when the window is too low for it */
	float leftScroll = 0.f, leftContentH = 0.f;
	float maxLeftScroll() const { return std::max(0.f, leftContentH - box.size.y); }

	~EditPane() {
		// Going away (the Editor closed, the window closing): the field is deselected while its children are deleted;
		// the name typed so far is already in the song, and the module may be gone by now, so it does nothing then
		if (naming) {
			naming->finished = true;
			naming->done = nullptr;
			naming->module = nullptr;
		}
	}

	void rename(int k) {
		if (naming)
			naming->finish(true);
		m->remember();
		NameField* const f = new NameField;
		f->module = m;
		f->track = k;
		f->before = m->song.tracks[k].name;
		f->box = math::Rect(36.f, kToolH + k * kTrackRowH + 3.f - leftScroll, kListW - 58.f - 36.f, kTrackRowH - 6.f);
		f->setText(f->before);
		f->done = [this, f]() {
			if (naming == f)
				naming = nullptr;
		};
		addChild(f);
		naming = f;
		APP->event->setSelectedWidget(f);
		f->selectAll();
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!m)
			return;
		// A double click on a track's name in the list: type a new one
		const math::Vec p = mouse().plus(math::Vec(0.f, leftScroll));
		const int k = (int) std::floor((p.y - kToolH) / kTrackRowH);
		if (p.x >= 30.f && p.x < kListW - 56.f && k >= 0 && k < sg::kTracks) {
			rename(k);
			e.consume(this);
		}
	}

	void gridMenu() {
		ui::Menu* const menu = createMenu();
		menu->addChild(createMenuLabel("Grid"));
		for (const GridChoice& c : kGrids) {
			const int64_t ticks = c.ticks;
			Edit* const mm = m;
			menu->addChild(createCheckMenuItem(c.name, "", [=]() { return mm->grid == ticks; }, [=]() { mm->grid = ticks; }));
		}
	}

	float labelWidth(const std::string& s) {
		std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL);
		if (!f || f->handle < 0)
			return 6.f * s.size();
		NVGcontext* const vg = APP->window->vg;
		nvgSave(vg);
		nvgFontFaceId(vg, f->handle);
		nvgFontSize(vg, 8.5f);
		nvgTextLetterSpacing(vg, 0.5f);
		const float w = nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, nullptr);
		nvgRestore(vg);
		return w;
	}

	void layoutButtons() {
		buttons.clear();
		float x = rightX() + 8.f;
		// As wide as the label: they all fit next to each other in a narrow window too
		auto add = [&](const std::string& label, std::function<void()> action, bool enabled = true) {
			const float w = std::max(36.f, labelWidth(label) + 14.f);
			buttons.push_back(Button{math::Rect(x, 5.f, w, kToolH - 10.f), label, action, enabled});
			x += w + 5.f;
		};
		const bool any = anySelected();
		add(string::f("GRID %s", gridName(m->grid)), [this]() { gridMenu(); });
		add("FIT SONG", [this]() { fitSong(); });
		add("QUANTIZE", [this]() { quantize(); }, !track().notes.empty());
		add("COPY", [this]() { copy(); }, any);
		add("PASTE", [this]() { paste(); }, !clip.empty());
		add("DELETE", [this]() { deleteSelected(); }, any);
		add("UNDO", [this]() {
			m->undoStep();
			selTrack = -1;
			syncSel();
		}, m->canUndo());
	}

	// ---- events
	void step() override {
		OpaqueWidget::step();
		if (!m)
			return;
		syncSel();
		layoutButtons();
		// A note heard on a click stops a moment later, one being dragged when the mouse is let go
		CardinalPluginContext* const ctx = m->context();
		if (previewUntil > 0.0 && ((drag == DRAG_NONE && system::getTime() >= previewUntil) || (ctx && ctx->internalTransportPlaying)))
			stopHearing();
		// The view never starts before the song (a negative scroll put bar 1 under the keys), nor below C-1
		m->scroll = std::max(0.0, m->scroll);
		m->topPitch = clampTop(m->topPitch);
		leftScroll = math::clamp(leftScroll, 0.f, maxLeftScroll());
		if (naming)
			naming->box.pos.y = kToolH + naming->track * kTrackRowH + 3.f - leftScroll;
		// Held arrows move the view smoothly (Shift: faster)
		const double now = system::getTime();
		const double dt = lastStep < 0.0 ? 0.0 : std::min(0.1, now - lastStep);
		lastStep = now;
		if (held[0] || held[1] || held[2] || held[3]) {
			const double fast = (APP->window->getMods() & GLFW_MOD_SHIFT) ? 3.0 : 1.0;
			const double px = 700.0 * fast * dt;
			m->scroll = std::max(0.0, m->scroll + ((held[1] ? 1 : 0) - (held[0] ? 1 : 0)) * px / m->zoom);
			rows += ((held[2] ? 1 : 0) - (held[3] ? 1 : 0)) * 30.0 * fast * dt;
			const int whole = (int) rows;
			if (whole != 0) {
				m->topPitch = clampTop(m->topPitch + whole);
				rows -= whole;
			}
		}
		else
			rows = 0.0;
		// The view does not follow the playhead: it stays where the user put it (arrows, overview, wheel)
	}

	void onButton(const ButtonEvent& e) override {
		// Not OpaqueWidget::onButton: it marks every left click as handled before we see it
		Widget::onButton(e);
		e.stopPropagating();
		if (!m || e.isConsumed() || e.action != GLFW_PRESS)
			return;
		syncSel();
		const math::Vec p = e.pos;
		const bool left = e.button == GLFW_MOUSE_BUTTON_LEFT, right = e.button == GLFW_MOUSE_BUTTON_RIGHT;
		drag = DRAG_NONE;
		dragFrom = dragNow = p;
		e.consume(this);

		// Track list (in its own scrolled coordinates)
		if (p.x < kListW) {
			const math::Vec p = math::Vec(e.pos.x, e.pos.y + leftScroll);
			if (left && chanMinusRect().contains(p)) {
				setChannel(channel() - 1);
				return;
			}
			if (left && chanPlusRect().contains(p)) {
				setChannel(channel() + 1);
				return;
			}
			if (left && polyMinusRect().contains(p)) {
				setPoly(m->song.poly[channel()] - 1, m->song.polyMode[channel()]);
				return;
			}
			if (left && polyPlusRect().contains(p)) {
				setPoly(m->song.poly[channel()] + 1, m->song.polyMode[channel()]);
				return;
			}
			if (modeRect().contains(p) && (left || right)) {
				setPoly(m->song.poly[channel()], m->song.polyMode[channel()] + (left ? 1 : -1));
				return;
			}
			const int k = (int) std::floor((p.y - kToolH) / kTrackRowH);
			if (k < 0 || k >= sg::kTracks)
				return;
			const float mx = kListW - 52.f, sx = kListW - 28.f;
			if (left && p.x >= mx && p.x < mx + 22.f)
				m->toggleMute(k);
			else if (left && p.x >= sx && p.x < sx + 22.f)
				m->toggleSolo(k);
			else if (left)
				selectTrack(k);
			return;
		}
		// Toolbar
		if (p.y < kToolH) {
			for (Button& b : buttons)
				if (left && b.rect.contains(p) && b.action && b.enabled)
					b.action();
			return;
		}
		// Overview ruler: move the position (song scale)
		if (p.y < overY()) {
			if (left && p.x >= noteX0()) {
				overLen = songTicks();
				drag = DRAG_SEEK_OVER;
				seek(snapDown(overTick(p.x)));
			}
			return;
		}
		// Overview: choose the track and the place
		if (p.y < overBottom()) {
			// A click picks that row's track and centres the roll there; dragging moves the view left / right
			const int k = (int) std::floor((p.y - overY()) / overRowH());
			if (left && k >= 0 && k < sg::kTracks && k != m->selected)
				selectTrack(k);
			if (left && p.x >= noteX0()) {
				overLen = songTicks();
				centreOn(p.x);
				drag = DRAG_OVERVIEW;
			}
			return;
		}
		if (p.y < rulerY())
			return;
		// Roll ruler: the loop markers, or move the position
		if (p.y < rollY()) {
			CardinalPluginContext* const c = m->context();
			if (!c || !left)
				return;
			// The START / STOP flags are in the lower half (START reaches right of its line, STOP left); the nearer
			// one is taken. The upper half (the bar numbers) always moves the position.
			const float xa = xOf(c->internalTransportLoopStart * sg::kPpq), xb = xOf(c->internalTransportLoopEnd * sg::kPpq);
			const bool lower = p.y >= rulerY() + kRulerH / 2;
			const bool onA = lower && p.x >= xa - 4.f && p.x <= xa + 34.f, onB = lower && p.x >= xb - 30.f && p.x <= xb + 4.f;
			if (onA && (!onB || std::fabs(p.x - xa) <= std::fabs(p.x - xb)))
				drag = DRAG_LOOP_START;
			else if (onB)
				drag = DRAG_LOOP_END;
			else {
				drag = DRAG_SEEK;
				seek(snapDown(tickAt(p.x)));
			}
			return;
		}
		// Velocity lane
		if (p.y >= velY()) {
			const int k = hitVelocity(p.x);
			if (k >= 0 && left) {
				m->remember();
				if (!sel[k]) {
					clearSel();
					sel[k] = 1;
				}
				startOrig();
				drag = DRAG_VEL;
			}
			return;
		}
		// Piano roll
		if (p.x < noteX0() || p.y < rollY() || p.y >= rollY() + rollH())
			return;
		bool edge = false;
		const int k = hitNote(p, &edge);
		if (right) {
			if (k >= 0) {
				m->remember();
				track().notes.erase(track().notes.begin() + k);
				sel.erase(sel.begin() + k);
				m->publish();
			}
			return;
		}
		if (!left)
			return;
		const int mods = e.mods & RACK_MOD_MASK;
		if (k >= 0) {
			m->remember();
			if (mods & GLFW_MOD_SHIFT)
				sel[k] = !sel[k];
			else if (!sel[k]) {
				clearSel();
				sel[k] = 1;
			}
			startOrig();
			drag = edge ? DRAG_LENGTH : DRAG_MOVE;
			// The next drawn note gets this one's length; it sounds
			m->drawLength = track().notes[k].length;
			previewBase = track().notes[k].pitch;
			previewVel = track().notes[k].vel;
			hear(previewBase);
		}
		else if (mods & (GLFW_MOD_SHIFT | RACK_MOD_CTRL)) {
			drag = DRAG_BOX;
		}
		else {
			const int pitch = pitchAt(p.y);
			if (pitch < 0 || pitch > 127)
				return;
			m->remember();
			clearSel();
			const int64_t length = std::max<int64_t>(m->drawLength > 0 ? m->drawLength : m->grid, 1);
			track().notes.push_back(sg::Note{std::max<int64_t>(0, snapDown(tickAt(p.x))), length, pitch, 100});
			previewBase = pitch;
			previewVel = 100;
			hear(pitch);
			sel.push_back(1);
			sortNotes();
			startOrig();
			drag = DRAG_LENGTH;
			m->publish();
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!m || drag == DRAG_NONE)
			return;
		const math::Vec p = mouse();
		dragNow = p;
		std::vector<sg::Note>& notes = track().notes;
		// The note list may have changed under the drag (undo, track switch): end the gesture
		for (const Orig& o : orig)
			if (o.index >= notes.size()) {
				orig.clear();
				drag = DRAG_NONE;
				return;
			}
		CardinalPluginContext* const c = m->context();
		switch (drag) {
			case DRAG_LOOP_START:
				if (c)
					c->internalTransportLoopStart = std::max(0.0, std::min((double) snapNear(tickAt(p.x)), c->internalTransportLoopEnd * sg::kPpq - minLoop()) / sg::kPpq);
				break;
			case DRAG_LOOP_END:
				if (c)
					c->internalTransportLoopEnd = std::max((double) snapNear(tickAt(p.x)), c->internalTransportLoopStart * sg::kPpq + minLoop()) / sg::kPpq;
				break;
			case DRAG_SEEK:
				seek(snapDown(tickAt(std::max(p.x, noteX0()))));
				break;
			case DRAG_OVERVIEW:
				centreOn(p.x);
				break;
			case DRAG_SEEK_OVER:
				seek(snapDown(overTick(std::max(p.x, noteX0()))));
				break;
			case DRAG_VEL: {
				const int v = math::clamp((int) std::lround((box.size.y - p.y) / (velH() - 8.f) * 127.f), 1, 127);
				for (const Orig& o : orig)
					notes[o.index].vel = orig.size() == 1 ? v : math::clamp(o.vel + (int) std::lround((dragFrom.y - p.y) / (velH() - 8.f) * 127.f), 1, 127);
				m->publish();
				break;
			}
			case DRAG_MOVE: {
				const int64_t dt = (int64_t) std::llround((p.x - dragFrom.x) / m->zoom / m->grid) * m->grid;
				int dp = (int) std::lround((dragFrom.y - p.y) / kRowH);
				int64_t minStart = INT64_MAX;
				int lowest = 127, highest = 0;
				for (const Orig& o : orig) {
					minStart = std::min(minStart, o.start);
					lowest = std::min(lowest, o.pitch);
					highest = std::max(highest, o.pitch);
				}
				// The whole chord stops at C-1 / G9: it keeps its shape
				dp = math::clamp(dp, -lowest, 127 - highest);
				const int64_t shift = std::max(dt, -minStart);
				for (const Orig& o : orig) {
					notes[o.index].start = o.start + shift;
					notes[o.index].pitch = math::clamp(o.pitch + dp, 0, 127);
				}
				if (previewBase >= 0)
					hear(math::clamp(previewBase + dp, 0, 127));
				m->publish();
				break;
			}
			case DRAG_LENGTH: {
				const double dl = (p.x - dragFrom.x) / m->zoom;
				for (const Orig& o : orig) {
					const int64_t end = std::max<int64_t>(o.start + m->grid, snapNear((double) (o.start + o.length) + dl));
					notes[o.index].length = std::max<int64_t>(1, end - o.start);
				}
				m->publish();
				break;
			}
			case DRAG_BOX: {
				const float x0 = std::min(dragFrom.x, p.x), x1 = std::max(dragFrom.x, p.x);
				const float y0 = std::min(dragFrom.y, p.y), y1 = std::max(dragFrom.y, p.y);
				for (size_t k = 0; k < notes.size(); ++k) {
					const float nx = xOf((double) notes[k].start), nw = notes[k].length * m->zoom;
					const float ny = yOf(notes[k].pitch) + kRowH / 2;
					sel[k] = nx + nw > x0 && nx < x1 && ny > y0 && ny < y1;
				}
				break;
			}
			default:
				break;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		// A note drawn or stretched: the next one is as long
		if (m && drag == DRAG_LENGTH && !orig.empty() && orig[0].index < track().notes.size())
			m->drawLength = track().notes[orig[0].index].length;
		if (m && (drag == DRAG_MOVE || drag == DRAG_LENGTH))
			changed();
		// The gesture is over: its undo step now (none if it changed nothing)
		if (m)
			m->commit(true);
		drag = DRAG_NONE;
		overLen = 0;
		orig.clear();
	}

	void onLeave(const LeaveEvent& e) override {
		// The key release goes where the mouse is: stop here, so a held arrow never keeps the view running
		for (bool& h : held)
			h = false;
		OpaqueWidget::onLeave(e);
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!m)
			return;
		const int mods = APP->window->getMods() & RACK_MOD_MASK;
		const math::Vec p = e.pos;
		// Over the left column: it scrolls (only when the window is too low for it)
		if (p.x < kListW) {
			leftScroll = math::clamp(leftScroll - e.scrollDelta.y, 0.f, maxLeftScroll());
			e.consume(this);
			return;
		}
		// Over the overview: Ctrl+wheel shows more or less of the song there
		if ((mods & RACK_MOD_CTRL) && p.y >= overRulerY() && p.y < overBottom()) {
			const int bars = e.scrollDelta.y > 0 ? m->overviewBars * 2 / 3 : m->overviewBars * 3 / 2;
			m->overviewBars = math::clamp(bars, 8, 512);
		}
		else if (mods & RACK_MOD_CTRL) {
			const double t = tickAt(p.x);
			m->zoom = std::min(2.0, std::max(0.005, m->zoom * (e.scrollDelta.y > 0 ? 1.2 : 1 / 1.2)));
			m->scroll = std::max(0.0, t - (p.x - noteX0()) / m->zoom);
		}
		else if ((mods & GLFW_MOD_SHIFT) || e.scrollDelta.x != 0.f) {
			const float d = e.scrollDelta.x != 0.f ? e.scrollDelta.x : e.scrollDelta.y;
			m->scroll = std::max(0.0, m->scroll - d / m->zoom);
		}
		else {
			m->topPitch = clampTop(m->topPitch + (e.scrollDelta.y > 0 ? 2 : -2));
		}
		e.consume(this);
	}

	/** Arrows held move the view: left, right, up, down */
	bool held[4] = {};
	double lastStep = -1.0;
	double rows = 0.0;  // fractional rows of the up / down scroll

	void onHoverKey(const HoverKeyEvent& e) override {
		OpaqueWidget::onHoverKey(e);
		if (!m || e.isConsumed())
			return;
		const int arrow = e.key == GLFW_KEY_LEFT ? 0 : e.key == GLFW_KEY_RIGHT ? 1 : e.key == GLFW_KEY_UP ? 2 : e.key == GLFW_KEY_DOWN ? 3 : -1;
		if (arrow >= 0) {
			if (e.action != GLFW_REPEAT)
				held[arrow] = e.action == GLFW_PRESS;
			e.consume(this);
			return;
		}
		if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)
			return;
		// No edits, undo or track changes while the mouse is dragging (the drag holds note indices)
		if (drag != DRAG_NONE) {
			e.consume(this);
			return;
		}
		const int mods = e.mods & RACK_MOD_MASK;
		if (mods == RACK_MOD_CTRL && e.keyName == "c")
			copy();
		else if (mods == RACK_MOD_CTRL && e.keyName == "v")
			paste();
		else if (mods == RACK_MOD_CTRL && e.keyName == "z") {
			m->undoStep();
			selTrack = -1;
			syncSel();
		}
		else if ((mods == (RACK_MOD_CTRL | GLFW_MOD_SHIFT) && e.keyName == "z") || (mods == RACK_MOD_CTRL && e.keyName == "y")) {
			m->redoStep();
			selTrack = -1;
			syncSel();
		}
		else if (mods == RACK_MOD_CTRL && e.keyName == "a")
			std::fill(sel.begin(), sel.end(), 1);
		else if (mods == 0 && (e.key == GLFW_KEY_DELETE || e.key == GLFW_KEY_BACKSPACE))
			deleteSelected();
		else if (mods == 0 && e.key == GLFW_KEY_Q)
			quantize();
		else if (mods == 0 && e.key == GLFW_KEY_PAGE_UP)
			selectTrack(std::max(0, m->selected - 1));
		else if (mods == 0 && e.key == GLFW_KEY_PAGE_DOWN)
			selectTrack(std::min(sg::kTracks - 1, m->selected + 1));
		else
			return;
		e.consume(this);
	}

	// ---- drawing
	void text(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor c, int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE) {
		st::text(vg, math::Vec(x, y), s, size, c, st::FONT_LABEL, align, 0.5f);
	}

	float textWidth(NVGcontext* vg, const std::string& s, float size) {
		std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL);
		if (!f || f->handle < 0)
			return 6.f * s.size();
		nvgSave(vg);
		nvgFontFaceId(vg, f->handle);
		nvgFontSize(vg, size);
		nvgTextLetterSpacing(vg, 0.5f);
		const float w = nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, nullptr);
		nvgRestore(vg);
		return w;
	}

	/** `s` cut (whole letters) with "…" so it is at most `room` wide */
	std::string fit(NVGcontext* vg, const std::string& s, float size, float room) {
		if (textWidth(vg, s, size) <= room)
			return s;
		size_t n = 0;
		for (size_t i = 0; i < s.size(); ++i)
			n += (s[i] & 0xC0) != 0x80;
		while (n > 0) {
			const std::string cut = utf8Cut(s, --n) + "…";
			if (textWidth(vg, cut, size) <= room)
				return cut;
		}
		return "";
	}

	void rect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}

	void drawList(NVGcontext* vg) {
		rect(vg, 0, 0, kListW, box.size.y, nvgRGB(0x26, 0x28, 0x2d));
		rect(vg, kListW - 1.f, 0, 1.f, box.size.y, nvgRGB(0, 0, 0));
		nvgSave(vg);
		nvgIntersectScissor(vg, 0, 0, kListW - 1.f, box.size.y);
		nvgTranslate(vg, 0, -leftScroll);
		drawListContent(vg);
		nvgRestore(vg);
		// A low window: a scroll bar shows there is more (the wheel scrolls)
		if (maxLeftScroll() > 0.f) {
			const float h = box.size.y * box.size.y / leftContentH;
			const float y = (box.size.y - h) * leftScroll / maxLeftScroll();
			nvgBeginPath(vg);
			nvgRoundedRect(vg, kListW - 6.f, y + 2.f, 3.f, h - 4.f, 1.5f);
			nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x40));
			nvgFill(vg);
		}
	}

	void drawListContent(NVGcontext* vg) {
		text(vg, 12.f, kToolH / 2, "TRACKS", 9.f, kEditColour);
		text(vg, kListW - 58.f, kToolH / 2, "CH", 8.f, st::legendDim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		const double tick = m->playTick();
		for (int k = 0; k < sg::kTracks; ++k) {
			const sg::Track& t = m->song.tracks[k];
			const float y = kToolH + k * kTrackRowH;
			const NVGcolor col = kTrackColours[k];
			if (k == m->selected)
				rect(vg, 0, y, kListW - 1.f, kTrackRowH, nvgRGB(0x34, 0x37, 0x3e));
			rect(vg, 0, y, 5.f, kTrackRowH - 1.f, col);
			rect(vg, 0, y + kTrackRowH - 1.f, kListW, 1.f, nvgRGB(0x1d, 0x1e, 0x22));
			// activity LED
			bool on = false;
			CardinalPluginContext* const c = m->context();
			if (c && c->internalTransportPlaying && m->song.audible(k))
				for (const sg::Note& n : t.notes) {
					if (n.start > tick)
						break;
					if (n.end() > tick) {
						on = true;
						break;
					}
				}
			nvgBeginPath(vg);
			nvgCircle(vg, 15.f, y + kTrackRowH / 2, 3.5f);
			nvgFillColor(vg, on ? col : color::mult(col, 0.3f));
			nvgFill(vg);
			text(vg, 26.f, y + kTrackRowH / 2, string::f("%d", k + 1), 9.f, st::legendDim());
			// Its MIDI channel (and the polyphony of that channel); the name gets the room left of it
			const int poly = m->song.poly[t.channel];
			const std::string chText = poly > 1 ? string::f("%d ×%d", t.channel + 1, poly) : string::f("%d", t.channel + 1);
			// (the name field, while open, takes the room up to M)
			if (!naming || naming->track != k) {
				text(vg, kListW - 58.f, y + kTrackRowH / 2, chText, 8.f, st::legendDim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
				const float room = kListW - 58.f - textWidth(vg, chText, 8.f) - 8.f - 40.f;
				text(vg, 40.f, y + kTrackRowH / 2, fit(vg, t.name, 10.f, room), 10.f, m->song.audible(k) ? st::legend() : st::legendDim());
			}
			auto msBtn = [&](float x, const char* l, float b, NVGcolor lc) {
				const bool lit = b > 0.4f;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y + 5.f, 20.f, kTrackRowH - 10.f, 3.f);
				nvgFillColor(vg, color::lerp(nvgRGB(0x3a, 0x3c, 0x42), lc, b));
				nvgFill(vg);
				text(vg, x + 10.f, y + kTrackRowH / 2, l, 8.f, lit ? nvgRGB(0x1a, 0x12, 0x10) : st::legendDim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			};
			// As on the mixer: MUTE lit when set, blinking while another track's solo silences this one
			msBtn(kListW - 52.f, "M", m->muteLight(k), nvgRGB(0xe8, 0x3a, 0x30));
			msBtn(kListW - 28.f, "S", t.solo ? 1.f : 0.f, nvgRGB(0xff, 0x8a, 0x1c));
		}
		// The selected track's settings: its MIDI channel, and that channel's polyphony (written into its Host MIDI)
		const int c = channel(), ch = c + 1;
		const int shared = m->tracksOn(c);
		text(vg, 12.f, panelY(), fit(vg, string::f("TRACK %d · %s", m->selected + 1, track().name.c_str()), 9.f, kListW - 24.f), 9.f, kEditColour);
		auto box2 = [&](math::Rect r, const std::string& l, bool on) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, RECT_ARGS(r), 3.f);
			nvgFillColor(vg, on ? nvgRGB(0x3a, 0x3d, 0x44) : nvgRGB(0x2b, 0x2d, 0x33));
			nvgFill(vg);
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, nvgRGB(0x45, 0x48, 0x4f));
			nvgStroke(vg);
			text(vg, r.getCenter().x, r.getCenter().y, l, 8.5f, st::legend(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		};
		auto value = [&](math::Rect a, math::Rect b, float y, const std::string& v) {
			text(vg, (a.getRight() + b.pos.x) / 2, y, v, 9.f, st::legend(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		};
		text(vg, 12.f, chanY(), "MIDI CH", 8.f, st::legendDim());
		box2(chanMinusRect(), "-", c > 0);
		value(chanMinusRect(), chanPlusRect(), chanY(), string::f("%d", ch));
		box2(chanPlusRect(), "+", c < 15);
		text(vg, 12.f, polyY(), "POLYPHONY", 8.f, st::legendDim());
		box2(polyMinusRect(), "-", m->song.poly[c] > 1);
		value(polyMinusRect(), polyPlusRect(), polyY(), m->song.poly[c] == 1 ? "1 MONO" : string::f("%d", m->song.poly[c]));
		box2(polyPlusRect(), "+", m->song.poly[c] < 16);
		text(vg, 12.f, modeY(), "MODE", 8.f, st::legendDim());
		static const char* const modes[] = {"ROTATE", "REUSE", "RESET"};
		// MPE is chosen in the Host MIDI itself (left alone from here until MODE is clicked)
		box2(modeRect(), m->hostMpe[c] ? "MPE (HOST MIDI)" : modes[math::clamp(m->song.polyMode[c], 0, 2)], true);
		// Below the controls, under a line: what is true now (one line each, nothing overlaps), then the hints
		rect(vg, 12.f, infoY() - 12.f, kListW - 24.f, 1.f, nvgRGB(0x33, 0x35, 0x3b));
		float y = infoY();
		const float step = 15.f;
		const NVGcolor warn = nvgRGB(0xe0, 0x8a, 0x2c);
		// One line of the information (the column scrolls when the window is too low for all of it)
		auto line = [&](float x, const std::string& t, float size, NVGcolor col) {
			text(vg, x, y, t, size, col);
		};
		const bool has = m->hostMidi[c], viaAll = !has && m->hostMidiAll > 0;
		const NVGcolor allColour = nvgRGB(0xe8, 0xc5, 0x3a);
		{
			nvgBeginPath(vg);
			nvgCircle(vg, 16.f, y, 3.5f);
			nvgFillColor(vg, has ? nvgRGB(0x5d, 0xd1, 0x6a) : viaAll ? allColour : warn);
			nvgFill(vg);
		}
		if (has)
			line(26.f, string::f("HOST MIDI CH %d IN THE RACK", ch), 8.f, st::legend());
		else if (viaAll)
			line(26.f, "A HOST MIDI ON ALL PLAYS IT", 8.f, allColour);
		else
			line(26.f, string::f("NO HOST MIDI ON CH %d", ch), 8.f, warn);
		y += step;
		if (viaAll) {
			line(26.f, string::f("FOR POLYPHONY: ONE ON CH %d", ch), 7.5f, st::legendDim());
			y += step;
		}
		else if (!has) {
			line(26.f, "ADD A HOST MIDI, IN ITS MENU SET", 7.5f, st::legendDim());
			y += 12.f;
			line(26.f, string::f("PLAYS MIDI CHANNEL TO %d", ch), 7.5f, st::legendDim());
			y += step;
		}
		if (shared > 1) {
			line(26.f, string::f("SHARED BY %d TRACKS ON CH %d", shared, ch), 7.5f, st::legendDim());
			y += step;
		}
		// A Host MIDI on "All" plays every track (and gets no polyphony from here)
		if (m->hostMidiAll > 0) {
			y += 4.f;
			line(26.f, m->hostMidiAll == 1 ? "1 HOST MIDI ON ALL CHANNELS:" : string::f("%d HOST MIDI ON ALL CHANNELS:", m->hostMidiAll), 7.5f, warn);
			y += 12.f;
			line(26.f, "IT PLAYS EVERY TRACK", 7.5f, warn);
			y += step;
		}
		y += 8.f;
		line(12.f, "KEYBOARD: THIS TRACK IN THE EDITOR", 7.5f, st::legendDim());
		y += step;
		line(12.f, "DOUBLE-CLICK A NAME: RENAME", 7.5f, st::legendDim());
		y += step;
		line(12.f, "RIGHT-CLICK MODE: BACK", 7.5f, st::legendDim());
		leftContentH = y + 14.f;
	}

	void drawToolbar(NVGcontext* vg) {
		rect(vg, kListW, 0, box.size.x - kListW, kToolH, nvgRGB(0x1d, 0x1f, 0x23));
		rect(vg, kListW, kToolH - 1.f, box.size.x - kListW, 1.f, nvgRGB(0, 0, 0));
		const math::Vec mp = mouse();
		const bool here = APP->event->hoveredWidget == this;
		for (const Button& b : buttons) {
			const bool hot = here && b.enabled && b.rect.contains(mp);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, RECT_ARGS(b.rect), 4.f);
			nvgFillColor(vg, hot ? nvgRGB(0x3c, 0x3f, 0x47) : nvgRGB(0x2b, 0x2d, 0x33));
			nvgFill(vg);
			nvgStrokeWidth(vg, 1.f);
			nvgStrokeColor(vg, hot ? color::alpha(kEditColour, 0.8f) : nvgRGB(0x45, 0x48, 0x4f));
			nvgStroke(vg);
			text(vg, b.rect.getCenter().x, b.rect.getCenter().y, b.label, 8.5f, b.enabled ? st::legend() : color::alpha(st::legendDim(), 0.45f),
			     NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		}
		const float hx = buttons.empty() ? kListW : buttons.back().rect.getRight() + 16.f;
		const char* const hints[] = {"CLICK DRAWS · DRAG MOVES · EDGE = LENGTH · RIGHT DELETES · SHIFT+DRAG SELECTS · CTRL+WHEEL ZOOM",
		                             "CLICK DRAWS · RIGHT DELETES · CTRL+WHEEL ZOOM", ""};
		for (const char* h : hints) {
			float bounds[4];
			nvgFontSize(vg, 7.5f);
			nvgTextLetterSpacing(vg, 0.5f);
			if (std::shared_ptr<window::Font> f = st::font(st::FONT_LABEL))
				nvgFontFaceId(vg, f->handle);
			nvgTextBounds(vg, 0, 0, h, nullptr, bounds);
			if (hx + bounds[2] - bounds[0] < box.size.x - 10.f) {
				text(vg, hx, kToolH / 2, h, 7.5f, nvgRGB(0x6c, 0x70, 0x79));
				break;
			}
		}
	}

	/** Loop shading and START / STOP markers between x `a` and `b` in a ruler at `y`, `h` high */
	void drawLoop(NVGcontext* vg, float a, float b, float y, float h, bool labels) {
		CardinalPluginContext* const c = m->context();
		const bool on = c && c->internalTransportLoop;
		rect(vg, a, y, b - a, h, on ? nvgRGBA(0x2f, 0x6f, 0x8c, 0x99) : nvgRGBA(0x2f, 0x6f, 0x8c, 0x38));
		const NVGcolor mk = on ? nvgRGB(0x5f, 0xc2, 0xee) : nvgRGB(0x6b, 0x87, 0x96);
		if (!labels)
			return;
		// Flags at the bottom of the ruler; the bar numbers stay in its top half
		nvgBeginPath(vg);
		nvgMoveTo(vg, a, y + h);
		nvgLineTo(vg, a + 9.f, y + h);
		nvgLineTo(vg, a, y + h - 9.f);
		nvgClosePath(vg);
		nvgMoveTo(vg, b, y + h);
		nvgLineTo(vg, b - 9.f, y + h);
		nvgLineTo(vg, b, y + h - 9.f);
		nvgClosePath(vg);
		nvgFillColor(vg, mk);
		nvgFill(vg);
		// A marker out of view: its label at that edge, pointing to it (never cut in half)
		const float W = box.size.x, x0 = noteX0();
		const float la = a < x0 ? x0 + 4.f : a + 11.f, lb = b > W ? W - 4.f : b - 11.f;
		if (lb - la > 70.f && a < W && b > x0) {
			text(vg, la, y + h - 5.f, a < x0 ? "‹ START" : "START", 7.f, mk);
			text(vg, lb, y + h - 5.f, b > W ? "STOP ›" : "STOP", 7.f, mk, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		}
	}

	/** Bar numbers from the left edge of the notes; `x(tick)` places them */
	template <typename X>
	void drawBars(NVGcontext* vg, float y, float h, double pxPerTick, double fromTick, X x) {
		const float W = box.size.x;
		const int64_t bar = sg::kPpq * barBeats();
		const int step = barStep(pxPerTick * bar, 26.f);
		const int64_t first = std::max<int64_t>(0, (int64_t) (fromTick / bar / step)) * step;
		for (int64_t n = first;; n += step) {
			const float bx = x((double) (n * bar));
			if (bx >= W)
				break;
			if (bx < noteX0())
				continue;
			rect(vg, bx, y + h * 0.45f, 1.f, h * 0.55f, nvgRGB(0x60, 0x63, 0x6a));
			text(vg, bx + 3.f, y + 7.f, string::f("%lld", (long long) n + 1), 8.f, nvgRGB(0x9a, 0xa0, 0xa8));
		}
	}

	void drawRuler(NVGcontext* vg) {
		const float W = box.size.x, y = rulerY();
		rect(vg, rightX(), y, W - rightX(), kRulerH, nvgRGB(0x20, 0x22, 0x26));
		text(vg, rightX() + 6.f, y + kRulerH / 2, "BAR", 7.f, st::legendDim());
		nvgSave(vg);
		nvgScissor(vg, noteX0(), y, W - noteX0(), kRulerH);
		if (CardinalPluginContext* const c = m->context())
			drawLoop(vg, xOf(c->internalTransportLoopStart * sg::kPpq), xOf(c->internalTransportLoopEnd * sg::kPpq), y, kRulerH, true);
		drawBars(vg, y, kRulerH, m->zoom, tickAt(noteX0()), [this](double t) { return xOf(t); });
		nvgRestore(vg);
		rect(vg, rightX(), y + kRulerH - 1.f, W - rightX(), 1.f, nvgRGB(0, 0, 0));
	}

	void drawOverview(NVGcontext* vg) {
		const float W = box.size.x, y0 = overY(), h = sg::kTracks * overRowH();
		// Its own ruler: the whole song
		rect(vg, rightX(), overRulerY(), W - rightX(), kOverRulerH, nvgRGB(0x20, 0x22, 0x26));
		text(vg, rightX() + 6.f, overRulerY() + kOverRulerH / 2, "SONG", 7.f, st::legendDim());
		nvgSave(vg);
		nvgScissor(vg, noteX0(), overRulerY(), W - noteX0(), kOverRulerH);
		if (CardinalPluginContext* const c = m->context())
			drawLoop(vg, overX(c->internalTransportLoopStart * sg::kPpq), overX(c->internalTransportLoopEnd * sg::kPpq), overRulerY(), kOverRulerH, false);
		drawBars(vg, overRulerY(), kOverRulerH, overScale(), 0.0, [this](double t) { return overX(t); });
		nvgRestore(vg);

		rect(vg, rightX(), y0, W - rightX(), h, nvgRGB(0x12, 0x13, 0x15));
		const double sx = overScale();
		for (int k = 0; k < sg::kTracks; ++k) {
			const sg::Track& t = m->song.tracks[k];
			const float y = y0 + k * overRowH();
			if (k == m->selected)
				rect(vg, rightX(), y, W - rightX(), overRowH(), nvgRGB(0x2a, 0x2d, 0x33));
			rect(vg, rightX(), y + overRowH() - 1.f, W - rightX(), 1.f, nvgRGB(0x1c, 0x1d, 0x20));
			const NVGcolor col = color::alpha(kTrackColours[k], m->song.audible(k) ? 1.f : 0.3f);
			// The gutter: the track number on its colour (the names are in the list on the left)
			nvgBeginPath(vg);
			nvgRoundedRect(vg, rightX() + 8.f, y + 3.f, kKeysW - 16.f, overRowH() - 6.f, 3.f);
			nvgFillColor(vg, col);
			nvgFill(vg);
			text(vg, rightX() + kKeysW / 2, y + overRowH() / 2, string::f("%d", k + 1), 8.f, nvgRGB(0x14, 0x14, 0x16), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			int lo = 127, hi = 0;
			for (const sg::Note& n : t.notes) {
				lo = std::min(lo, n.pitch);
				hi = std::max(hi, n.pitch);
			}
			nvgBeginPath(vg);
			for (const sg::Note& n : t.notes) {
				const float yy = y + overRowH() - 4.f - (hi > lo ? (float) (n.pitch - lo) / (hi - lo) : 0.5f) * (overRowH() - 8.f);
				nvgRect(vg, overX((double) n.start), yy - 1.f, std::max(1.f, (float) (n.length * sx)), 2.f);
			}
			nvgFillColor(vg, col);
			nvgFill(vg);
		}
		// What the piano roll shows
		const float vx = overX(m->scroll), vw = (W - noteX0()) / m->zoom * sx;
		nvgBeginPath(vg);
		nvgRect(vg, vx + 0.5f, overRulerY() + 0.5f, std::max(2.f, std::min(vw, W - vx) - 1.f), kOverRulerH + h - 1.f);
		nvgStrokeWidth(vg, 1.f);
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x66));
		nvgStroke(vg);
		// Its own playhead
		CardinalPluginContext* const c = m->context();
		const bool rec = c && c->internalTransportRecord;
		rect(vg, overX(m->playTick()), overRulerY(), 1.5f, kOverRulerH + h, rec ? nvgRGB(0xe8, 0x48, 0x3c) : color::WHITE);
		rect(vg, rightX(), overBottom(), W - rightX(), kGap, nvgRGB(0, 0, 0));
	}

	void drawRoll(NVGcontext* vg) {
		const float W = box.size.x, y0 = rollY(), h = rollH();
		nvgSave(vg);
		nvgScissor(vg, rightX(), y0, W - rightX(), h);
		for (int p = m->topPitch; yOf(p) < y0 + h && p >= 0; --p) {
			const float y = yOf(p);
			const bool blk = blackKey(p);
			// The grid row (black-key rows darker), a thin line under each row, a brighter one under each C
			rect(vg, noteX0(), y, W - noteX0(), kRowH, blk ? nvgRGB(0x19, 0x1a, 0x1d) : nvgRGB(0x21, 0x22, 0x26));
			rect(vg, noteX0(), y + kRowH - 1.f, W - noteX0(), 1.f, p % 12 == 0 ? nvgRGB(0x3a, 0x3d, 0x44) : nvgRGB(0x1c, 0x1d, 0x20));
			// The key: a white row the whole width, a black key over the left part of its row, as on a piano roll
			const float kx = rightX(), kw = kKeysW - 1.f;
			rect(vg, kx, y, kw, kRowH, nvgRGB(0xe4, 0xe4, 0xe2));
			if (blk)
				rect(vg, kx, y + 1.f, kw * 0.62f, kRowH - 2.f, nvgRGB(0x1c, 0x1c, 0x1e));
			// The line between keys: under every key, stronger where two white keys meet (B-C, E-F)
			const bool whiteBelow = !blk && !blackKey(p - 1);
			rect(vg, blk ? kx + kw * 0.62f : kx, y + kRowH - 1.f, blk ? kw * 0.38f : kw, 1.f,
			     whiteBelow ? nvgRGB(0x8a, 0x8a, 0x88) : nvgRGB(0xc4, 0xc4, 0xc2));
			if (p % 12 == 0)
				text(vg, kx + kw - 3.f, y + kRowH / 2, string::f("C%d", p / 12 - 1), 7.f, nvgRGB(0x55, 0x55, 0x58),
				     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		}
		nvgResetScissor(vg);
		nvgScissor(vg, noteX0(), y0, W - noteX0(), h);
		// Grid: bars, beats, the grid itself when it is wide enough
		const int64_t bar = sg::kPpq * barBeats();
		const int64_t g = m->grid * m->zoom >= 6.f ? m->grid : (sg::kPpq * m->zoom >= 6.f ? sg::kPpq : bar);
		for (int64_t t = std::max<int64_t>(0, (int64_t) (tickAt(noteX0()) / g)) * g; xOf((double) t) < W; t += g) {
			const NVGcolor c = t % bar == 0 ? nvgRGB(0x4a, 0x4d, 0x55) : t % sg::kPpq == 0 ? nvgRGB(0x30, 0x33, 0x3a) : nvgRGB(0x25, 0x27, 0x2c);
			rect(vg, xOf((double) t), y0, 1.f, h, c);
		}
		// Notes
		const sg::Track& t = track();
		const NVGcolor col = kTrackColours[m->selected];
		for (size_t k = 0; k < t.notes.size(); ++k) {
			const sg::Note& n = t.notes[k];
			const float x = xOf((double) n.start), w = std::max(3.f, (float) (n.length * m->zoom)), y = yOf(n.pitch);
			if (x > W || x + w < noteX0())
				continue;
			const bool s = k < sel.size() && sel[k];
			nvgBeginPath(vg);
			nvgRect(vg, x, y + 1.f, w - 1.f, kRowH - 2.f);
			nvgFillColor(vg, s ? color::WHITE : color::alpha(col, 0.55f + 0.45f * n.vel / 127.f));
			nvgFill(vg);
			rect(vg, x + w - 4.f, y + 1.f, 3.f, kRowH - 2.f, nvgRGBA(0, 0, 0, 0x60));
		}
		if (drag == DRAG_BOX) {
			nvgBeginPath(vg);
			nvgRect(vg, std::min(dragFrom.x, dragNow.x), std::min(dragFrom.y, dragNow.y), std::fabs(dragNow.x - dragFrom.x), std::fabs(dragNow.y - dragFrom.y));
			nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x99));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
		nvgRestore(vg);
	}

	void drawVelocity(NVGcontext* vg) {
		const float W = box.size.x, y0 = velY();
		rect(vg, rightX(), y0 - kGap, W - rightX(), kGap, nvgRGB(0, 0, 0));
		rect(vg, rightX(), y0, W - rightX(), velH(), nvgRGB(0x14, 0x15, 0x18));
		text(vg, rightX() + 6.f, y0 + 10.f, "VEL", 8.f, st::legendDim());
		nvgSave(vg);
		nvgScissor(vg, noteX0(), y0, W - noteX0(), velH());
		const sg::Track& t = track();
		const NVGcolor col = kTrackColours[m->selected];
		for (size_t k = 0; k < t.notes.size(); ++k) {
			const sg::Note& n = t.notes[k];
			const float x = xOf((double) n.start);
			if (x > W || x < noteX0() - 4.f)
				continue;
			const float hh = (velH() - 8.f) * n.vel / 127.f;
			const NVGcolor c = k < sel.size() && sel[k] ? color::WHITE : col;
			rect(vg, x, box.size.y - hh - 2.f, 2.f, hh, c);
			rect(vg, x - 2.f, box.size.y - hh - 3.f, 6.f, 3.f, c);
		}
		nvgRestore(vg);
	}

	void draw(const DrawArgs& args) override {
		if (!m)
			return;
		NVGcontext* const vg = args.vg;
		rect(vg, 0, 0, box.size.x, box.size.y, nvgRGB(0x17, 0x18, 0x1a));
		drawList(vg);
		drawToolbar(vg);
		drawRuler(vg);
		drawOverview(vg);
		drawRoll(vg);
		drawVelocity(vg);
		// Playhead
		CardinalPluginContext* const c = m->context();
		const float px = xOf(m->playTick());
		if (px >= noteX0() && px < box.size.x) {
			const bool rec = c && c->internalTransportRecord;
			rect(vg, px, rulerY(), 1.5f, kRulerH, rec ? nvgRGB(0xe8, 0x48, 0x3c) : color::WHITE);
			rect(vg, px, rollY(), 1.5f, box.size.y - rollY(), rec ? nvgRGB(0xe8, 0x48, 0x3c) : color::WHITE);
		}
		OpaqueWidget::draw(args);
	}
};

/** The pane drawn larger: everything (text, rows, buttons) scales together, events are mapped back by the zoom */
const float kEditScale = 1.5f;

struct EditZoom : widget::ZoomWidget {
	EditPane* pane = nullptr;

	void step() override {
		setZoom(kEditScale);
		pane->box = math::Rect(math::Vec(), box.size.div(kEditScale));
		widget::ZoomWidget::step();
	}
};

widget::Widget* createEditPane(Edit* const module) {
	EditPane* const p = new EditPane;
	p->m = module;
	EditZoom* const z = new EditZoom;
	z->pane = p;
	z->addChild(p);
	return z;
}

} // namespace jxedit

// Local UI tests (src/patchcore/tests, not in git): built in only where that file exists
#if __has_include("../../../src/patchcore/tests/edit_uitest.inc")
#include <map>
#include "../../../src/patchcore/tests/edit_uitest.inc"
#endif
