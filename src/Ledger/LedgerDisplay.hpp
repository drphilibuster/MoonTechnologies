#pragma once
// Ledger's display: five pages on one piece of glass, under a strip of controls.
//
//   The strip (LedgerStrip, the top two rows of the glass) is on every page: the five
//   pages and the eight tracks, then the transport and what is done to the selected
//   track. Each is a field (FIELD_* in Panel.hpp) bound to the button param that used to
//   stand on the face; this only draws them. The page is LedgerDisplay, below it.
//
//   TANK  Shoal's view: eight lanes, each the whole of what its track will play, and
//         the selected track's books beside them.
//   ROLL  the selected track's pattern as a piano roll, with a lane strip below it.
//   FX    the selected track's effects chain: eight slots, and the chosen effect's
//         parameters as bars to drag.
//   SEQ   every track's sixteen slots: what each holds, which is playing, which is
//         queued. Launching happens here, and copying, pasting and clearing rows.
//   SONG  the rows in the order the song plays them, each with its number of passes.
//
// Included by Ledger.cpp only, after Panel.hpp (it draws through panel:: and places the
// books in the FIELD_* rectangles the spec cut the glass into).
// Everything it changes goes to the module as an edit (Ledger::sendEdit) or an atomic
// request; it never writes the module's state directly. Each edit leaves an undo step
// in Rack's history (LedgerUndo.hpp).
#include "LedgerModule.hpp"
#include "LedgerUndo.hpp"

namespace U = ledgerUndo;

static const char* const NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static std::string noteName(int n) {
	return string::f("%s%d", NOTE_NAMES[((n % 12) + 12) % 12], n / 12 - 1);
}

// A number typed into a menu, applied on Enter through `apply`.
struct LedgerValueField : ui::TextField {
	std::function<void(int)> apply;
	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			int v = std::atoi(text.c_str());
			if (apply) apply(v);
			ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
			if (overlay) overlay->requestDelete();
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}
};

// A submenu holding one LedgerValueField.
static MenuItem* valueSubmenu(const std::string& label, const std::string& right, const std::string& hint,
                              int current, std::function<void(int)> apply) {
	return createSubmenuItem(label, right, [=](Menu* sub) {
		sub->addChild(createMenuLabel(hint + ", Enter to set"));
		LedgerValueField* f = new LedgerValueField;
		f->apply = apply;
		f->box.size.x = 120.f;
		f->text = string::f("%d", current);
		f->selectAll();
		sub->addChild(f);
	});
}

struct LedgerDisplay : widget::OpaqueWidget {
	Ledger* module = NULL;
	S::Shoal view;					// the UI's copy of the engine, for evalStep
	Ledger::Snapshot snap;
	bool haveSnap = false;

	// each track's active slot, as last copied
	L::Slot cache[S::kNumTracks];
	int cacheSlot[S::kNumTracks];
	uint32_t cacheVer[S::kNumTracks];

	// the roll's working copy of the pattern it edits
	L::Slot roll;
	int rollTrack = -1, rollSlot = -1;
	uint32_t rollVer = 0;
	bool rollDirty = false;			// a local change not yet sent
	int rollLow = 48;				// lowest pitch row on screen
	int rollRows = 25;
	int snapTicks = L::kTicksPerStep / 4;
	int lastLen = L::kTicksPerStep / 4;
	int lastVel = 100;
	int selNote = -1;
	int laneSel = L::kLaneMod1;
	L::GenSettings gen;

	enum Drag { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_VEL, DRAG_LANE, DRAG_FX };
	int fxSel = 0;					// the effect the FX page shows
	int fxParam = -1;				// the parameter being dragged
	float fxDragAcc = 0.f;
	int fxDragStart = 0;
	Drag drag = DRAG_NONE;
	Vec dragPos;
	L::Note dragOrig;
	float dragTickOff = 0.f;

	// the undo steps of a gesture still under way: pushed when it ends
	U::SlotUndo* rollPending = NULL;
	U::FxUndo* fxPending = NULL;

	LedgerDisplay() {
		for (int t = 0; t < S::kNumTracks; t++) { cacheSlot[t] = -1; cacheVer[t] = 0; cache[t].clear(); }
		roll.clear();
	}
	~LedgerDisplay() {
		delete rollPending;
		delete fxPending;
	}

	void flushRollUndo() {
		if (rollPending) U::push(rollPending);
		rollPending = NULL;
	}
	void flushFxUndo() {
		if (fxPending && module && fxPending->changed(module)) U::push(fxPending);
		else delete fxPending;
		fxPending = NULL;
	}
	// An undo step for the selected track's effects, taken before a change to them.
	void fxUndo(const char* what) {
		if (module) U::push(new U::FxUndo(module, fxTrack(), what));
	}

	int page() const { return module ? module->page.load() : kPageTank; }

	// A preview with no module: the default books, so the browser shows the tank alive.
	void loadPreview() {
		for (int p = 0; p < S::kNumParameters; p++) view.v[p] = S::paramRange(p).def;
		S::construct(&view, 48000);
		S::rebuildScale(view.dtc, 0);
		for (int t = 0; t < S::kNumTracks; t++) {
			view.dtc->tracks[t].activeSeed = (uint32_t)view.v[S::TP(t, S::kTSeed)];
			view.v[S::TP(t, S::kTChance)] = (int16_t)(55 + 6 * t);
			view.v[S::TP(t, S::kTLength)] = (int16_t)(t < 4 ? 16 : 24 + 8 * (t - 4));
		}
		std::memset(&snap, 0, sizeof snap);
		std::memcpy(snap.v, view.v, sizeof snap.v);
		snap.dtc = *view.dtc;
		for (int t = 0; t < S::kNumTracks; t++) { snap.queued[t] = -1; snap.patStep[t] = -1; cache[t].kind = L::kSlotGen; }
		haveSnap = true;
	}

	void step() override {
		// as many semitones as the roll has room for at about four pixels a row
		{
			RollRect r = rollRect();
			int rows = clamp((int)((r.y1 - r.y0) / 4.f), 12, 25);
			if (rows != rollRows) {
				rollRows = rows;
				rollLow = clamp(rollLow, 0, 127 - rollRows + 1);
			}
		}
		if (module) {
			if (module->readSnapshot(snap)) {
				std::memcpy(view.v, snap.v, sizeof view.v);
				view.dtcStore = snap.dtc;
				view.dtc = &view.dtcStore;
				std::memcpy(view.solo, snap.solo, sizeof view.solo);
				haveSnap = true;
			}
			for (int t = 0; t < S::kNumTracks; t++) {
				int k = snap.active[t];
				if (k != cacheSlot[t] || module->slotVersion[t][k].load() != cacheVer[t]) {
					cacheVer[t] = module->readSlot(t, k, cache[t]);
					cacheSlot[t] = k;
				}
			}
			syncRoll();
		}
		else if (!haveSnap)
			loadPreview();
		OpaqueWidget::step();
	}

	// ---- the roll's copy ----------------------------------------------------

	// Follow the selected track's active slot; take a fresh copy when the module
	// changed it and nothing of ours is still on its way.
	void syncRoll() {
		int t = snap.sel, k = snap.active[t];
		if (rollDirty)
			sendRoll();
		if (t != rollTrack || k != rollSlot) {
			flushRollUndo();
			rollTrack = t;
			rollSlot = k;
			rollVer = module->readSlot(t, k, roll);
			selNote = -1;
			drag = DRAG_NONE;
			centreRoll();
			return;
		}
		if (drag == DRAG_NONE && !rollDirty && !module->editsInFlight()
			&& module->slotVersion[t][k].load() != rollVer)
			rollVer = module->readSlot(t, k, roll);
	}

	void centreRoll() {
		if (roll.pat.count == 0) { rollLow = 48; return; }
		int lo = 127, hi = 0;
		for (int i = 0; i < roll.pat.count; i++) {
			lo = std::min(lo, (int)roll.pat.notes[i].pitch);
			hi = std::max(hi, (int)roll.pat.notes[i].pitch);
		}
		rollLow = clamp((lo + hi) / 2 - rollRows / 2, 0, 127 - rollRows + 1);
	}

	// Send the working copy, sorted, as the slot's pattern.
	void sendRoll() {
		if (!module || rollTrack < 0) return;
		// the slot as it was before this edit (or before the gesture this edit is part of)
		if (!rollPending)
			rollPending = new U::SlotUndo(module, rollTrack, rollSlot, false, "edit pattern");
		SlotEdit* e = new SlotEdit;
		e->op = SlotEdit::PUT_PATTERN;
		e->track = rollTrack;
		e->slot = rollSlot;
		e->data = roll;
		e->data.pat.sort();
		rollDirty = !module->sendEdit(*e);
		delete e;
		if (roll.kind == L::kSlotEmpty) roll.kind = L::kSlotPat;
		if (drag == DRAG_NONE && !rollDirty)
			flushRollUndo();
	}

	// Replace a whole slot. `undoInto` gathers several into one undo step (a row).
	void sendSlot(int t, int k, const L::Slot& s, const char* what = "change slot", history::ComplexAction* undoInto = NULL) {
		if (!module) return;
		U::SlotUndo* u = new U::SlotUndo(module, t, k, true, what);
		SlotEdit* e = new SlotEdit;
		e->op = SlotEdit::PUT_SLOT;
		e->track = t;
		e->slot = k;
		e->data = s;
		module->sendEditWait(*e);
		delete e;
		if (undoInto) undoInto->push(u);
		else U::push(u);
	}

	// ---- geometry -----------------------------------------------------------

	const float pad = 4.f;

	// A field's rectangle (panel mm, from Panel.hpp) in this widget's pixels. The display
	// sits on the glass below the strip, so this is inGlass() less the strip.
	Rect local(const Rect& f) const {
		Rect r = panel::mmRect(f.pos.x, f.pos.y, f.size.x, f.size.y);
		r.pos = r.pos.minus(box.pos);
		return r;
	}
	// The tank's lanes stop short of the books, whose fields start where the spec put them.
	float laneW() const { return local(panel::FIELD_CHANCE).pos.x - 5.f; }

	// The long help lines in the page headers, cut to the glass with an ellipsis.
	panel::FittedText headFit;
	void headText(NVGcontext* vg, const panel::TextStyle& st, float x, float y, const std::string& str) {
		panel::text(vg, st, x, y, headFit.get(vg, st, str, std::max(0.f, box.size.x - pad - x)));
	}

	struct RollRect { float x0, x1, y0, y1, ly0, ly1, kx; };
	RollRect rollRect() const {
		RollRect r;
		float head = 11.f;
		r.kx = pad;
		r.x0 = pad + 18.f;
		r.x1 = box.size.x - pad;
		r.y0 = pad + head;
		float avail = box.size.y - pad - r.y0;
		r.ly1 = box.size.y - pad;
		r.ly0 = r.ly1 - avail * 0.24f;
		r.y1 = r.ly0 - 3.f;
		return r;
	}
	int rollLen() const { return std::max(1, (int)view.v[S::TP(rollTrack < 0 ? 0 : rollTrack, S::kTLength)]); }
	float tickToX(const RollRect& r, float tick) const { return r.x0 + (r.x1 - r.x0) * tick / (rollLen() * L::kTicksPerStep); }
	float xToTick(const RollRect& r, float x) const { return (x - r.x0) / (r.x1 - r.x0) * rollLen() * L::kTicksPerStep; }
	float rowH(const RollRect& r) const { return (r.y1 - r.y0) / rollRows; }
	float pitchToY(const RollRect& r, int p) const { return r.y1 - (p - rollLow + 1) * rowH(r); }
	int yToPitch(const RollRect& r, float y) const { return rollLow + (int)std::floor((r.y1 - y) / rowH(r)); }

	int noteAt(const RollRect& r, Vec p, bool* edge) const {
		for (int i = roll.pat.count - 1; i >= 0; i--) {
			const L::Note& n = roll.pat.notes[i];
			float x0 = tickToX(r, n.start), x1 = std::max(x0 + 2.f, tickToX(r, n.start + n.len));
			float y0 = pitchToY(r, n.pitch), y1 = y0 + rowH(r);
			if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) {
				if (edge) *edge = p.x >= x1 - 3.f;
				return i;
			}
		}
		return -1;
	}

	bool rollEditable() const { return module && rollTrack >= 0 && roll.kind != L::kSlotGen; }

	// ---- events -------------------------------------------------------------

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS) {
			OpaqueWidget::onButton(e);
			return;
		}
		switch (page()) {
			case kPageRoll: rollButton(e); break;
			case kPageFx: fxButton(e); break;
			case kPageSeq: seqButton(e); break;
			case kPageSong: songButton(e); break;
			default:
				if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.pos.x < laneW()) {
					float lh = (box.size.y - 2 * pad) / S::kNumTracks;
					int t = (int)((e.pos.y - pad) / lh);
					if (t >= 0 && t < S::kNumTracks) module->requestSel = t;
				}
				break;
		}
		e.consume(this);
	}

	void rollButton(const ButtonEvent& e) {
		RollRect r = rollRect();
		if (!rollEditable()) {
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) rollMenu();
			return;
		}
		Vec p = e.pos;
		// the lane strip
		if (p.y >= r.ly0 && p.y <= r.ly1 && p.x >= r.x0) {
			int st = (int)(xToTick(r, p.x) / L::kTicksPerStep);
			if (st < 0 || st >= rollLen()) return;
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				roll.pat.lane[laneSel][st] = L::kLaneUnset;
				sendRoll();
			}
			else if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
				drag = DRAG_LANE;
				dragPos = p;
				setLanePoint(r, p);
			}
			return;
		}
		if (p.y < r.y0 || p.y > r.y1 || p.x < r.x0) {
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) rollMenu();
			return;
		}
		bool edge = false;
		int i = noteAt(r, p, &edge);
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			if (i >= 0) {
				roll.pat.remove(i);
				selNote = -1;
				sendRoll();
			}
			else
				rollMenu();
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		dragPos = p;
		dragStartY = p.y;
		if (i < 0) {
			// a new note where the click was, on the snap grid
			int tick = (int)std::floor(xToTick(r, p.x) / snapTicks) * snapTicks;
			int end = rollLen() * L::kTicksPerStep;
			if (tick < 0 || tick >= end) return;
			L::Note n;
			n.start = (uint16_t)tick;
			n.len = (uint16_t)std::max(1, std::min(lastLen, end - tick));
			n.pitch = (uint8_t)clamp(yToPitch(r, p.y), 0, 127);
			n.vel = (uint8_t)lastVel;
			if (!roll.pat.add(n)) return;
			selNote = findNote(n);
			drag = DRAG_MOVE;
			dragOrig = n;
			dragTickOff = xToTick(r, p.x) - n.start;
			sendRoll();
			return;
		}
		selNote = i;
		dragOrig = roll.pat.notes[i];
		dragTickOff = xToTick(r, p.x) - dragOrig.start;
		drag = (e.mods & RACK_MOD_MASK) == GLFW_MOD_ALT ? DRAG_VEL : edge ? DRAG_RESIZE : DRAG_MOVE;
		lastLen = dragOrig.len;
		lastVel = dragOrig.vel;
	}

	int findNote(const L::Note& n) const {
		for (int i = 0; i < roll.pat.count; i++) {
			const L::Note& m = roll.pat.notes[i];
			if (m.start == n.start && m.pitch == n.pitch && m.len == n.len && m.vel == n.vel) return i;
		}
		return -1;
	}

	void setLanePoint(const RollRect& r, Vec p) {
		int st = (int)(xToTick(r, p.x) / L::kTicksPerStep);
		if (st < 0 || st >= rollLen()) return;
		float f = clamp((r.ly1 - p.y) / (r.ly1 - r.ly0), 0.f, 1.f);
		int v = (int)std::round(L::laneMin(laneSel) + f * (L::laneMax(laneSel) - L::laneMin(laneSel)));
		if (roll.pat.lane[laneSel][st] != v) {
			roll.pat.lane[laneSel][st] = (int16_t)v;
			sendRoll();
		}
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (drag == DRAG_FX && page() == kPageFx) { fxDrag(e.mouseDelta.x / getAbsoluteZoom()); return; }
		if (drag == DRAG_NONE || page() != kPageRoll) return;
		float z = getAbsoluteZoom();
		dragPos = dragPos.plus(e.mouseDelta.div(z));
		RollRect r = rollRect();
		if (drag == DRAG_LANE) { setLanePoint(r, dragPos); return; }
		if (selNote < 0 || selNote >= roll.pat.count) return;
		L::Note& n = roll.pat.notes[selNote];
		int end = rollLen() * L::kTicksPerStep;
		L::Note was = n;
		if (drag == DRAG_MOVE) {
			int tick = (int)std::round((xToTick(r, dragPos.x) - dragTickOff) / snapTicks) * snapTicks;
			n.start = (uint16_t)clamp(tick, 0, end - 1);
			n.pitch = (uint8_t)clamp(yToPitch(r, dragPos.y), 0, 127);
		}
		else if (drag == DRAG_RESIZE) {
			int tick = (int)std::round(xToTick(r, dragPos.x) / snapTicks) * snapTicks;
			n.len = (uint16_t)clamp(tick - (int)n.start, 1, end);
			lastLen = n.len;
		}
		else if (drag == DRAG_VEL) {
			int vel = (int)std::round(dragOrig.vel + (dragPosStartY() - dragPos.y) * 1.0f);
			n.vel = (uint8_t)clamp(vel, 1, 127);
			lastVel = n.vel;
		}
		if (memcmp(&was, &n, sizeof n))
			sendRoll();
	}
	float dragStartY = 0.f;
	float dragPosStartY() const { return dragStartY; }

	void onDragEnd(const DragEndEvent& e) override {
		if ((drag == DRAG_MOVE || drag == DRAG_RESIZE || drag == DRAG_VEL) && selNote >= 0 && selNote < roll.pat.count) {
			L::Note n = roll.pat.notes[selNote];
			roll.pat.sort();
			selNote = findNote(n);
		}
		drag = DRAG_NONE;
		fxParam = -1;
		if (!rollDirty) flushRollUndo();
		flushFxUndo();
		OpaqueWidget::onDragEnd(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (!module || page() != kPageRoll || !rollEditable() || (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)) {
			OpaqueWidget::onHoverKey(e);
			return;
		}
		int mods = e.mods & RACK_MOD_MASK;
		bool have = selNote >= 0 && selNote < roll.pat.count;
		int end = rollLen() * L::kTicksPerStep;
		bool used = true;
		if (have) {
			L::Note& n = roll.pat.notes[selNote];
			L::Note was = n;
			switch (e.key) {
				case GLFW_KEY_DELETE:
				case GLFW_KEY_BACKSPACE:
					roll.pat.remove(selNote);
					selNote = -1;
					sendRoll();
					e.consume(this);
					return;
				case GLFW_KEY_UP: n.pitch = (uint8_t)clamp(n.pitch + (mods == GLFW_MOD_SHIFT ? 12 : 1), 0, 127); break;
				case GLFW_KEY_DOWN: n.pitch = (uint8_t)clamp(n.pitch - (mods == GLFW_MOD_SHIFT ? 12 : 1), 0, 127); break;
				case GLFW_KEY_LEFT:
					if (mods == GLFW_MOD_SHIFT) n.len = (uint16_t)std::max(1, (int)n.len - snapTicks);
					else n.start = (uint16_t)std::max(0, (int)n.start - snapTicks);
					break;
				case GLFW_KEY_RIGHT:
					if (mods == GLFW_MOD_SHIFT) n.len = (uint16_t)std::min(end, (int)n.len + snapTicks);
					else n.start = (uint16_t)std::min(end - 1, (int)n.start + snapTicks);
					break;
				default: used = false; break;
			}
			if (used) {
				if (memcmp(&was, &n, sizeof n)) {
					L::Note keep = n;
					roll.pat.sort();
					selNote = findNote(keep);
					sendRoll();
				}
				e.consume(this);
				return;
			}
		}
		if (e.key == GLFW_KEY_R && mods == 0) {
			generate(L::kGenAll);
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverKey(e);
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		if (module && page() == kPageRoll) {
			int d = e.scrollDelta.y > 0 ? 1 : e.scrollDelta.y < 0 ? -1 : 0;
			rollLow = clamp(rollLow + d * 2, 0, 127 - rollRows + 1);
			e.consume(this);
			return;
		}
		if (module && page() == kPageSong) {
			int i = songCellAt(e.pos);
			int d = e.scrollDelta.y > 0 ? 1 : e.scrollDelta.y < 0 ? -1 : 0;
			if (i >= 0 && i < module->song.len && d) {
				L::SongEntry s = module->song.e[i];
				if (APP->window->getMods() & GLFW_MOD_SHIFT) s.times = (uint8_t)clamp((int)s.times + d, 1, 16);
				else s.row = (int8_t)clamp((int)s.row + d, 0, L::kNumSlots - 1);
				songCmd(Ledger::SongCmd::SET, i, s, "change song entry");
			}
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverScroll(e);
	}

	void generate(int what) {
		if (!rollEditable()) return;
		uint32_t seed = random::u32();
		L::generate(roll.pat, rollLen(), gen, view.v[S::kGRoot], view.v[S::kGScale], seed, what);
		roll.pat.sort();
		selNote = -1;
		if (what == L::kGenAll) centreRoll();
		sendRoll();
	}

	void rollMenu() {
		if (!module) return;
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Track %d, slot %d", rollTrack + 1, rollSlot + 1)));
		if (roll.kind == L::kSlotGen) {
			menu->addChild(createMenuLabel("This slot is a generator."));
			menu->addChild(createMenuItem("Capture it into the next empty slot", "", [=]() { module->requestCapture = true; }));
			return;
		}
		menu->addChild(createMenuItem("Randomize the pattern", "R", [=]() { generate(L::kGenAll); }));
		menu->addChild(createMenuItem("Re-roll pitches", "", [=]() { generate(L::kGenPitch); }));
		menu->addChild(createMenuItem("Re-roll lengths", "", [=]() { generate(L::kGenLength); }));
		menu->addChild(createMenuItem("Re-roll velocities", "", [=]() { generate(L::kGenVelocity); }));
		menu->addChild(createSubmenuItem("Randomize settings", "", [=](Menu* sub) {
			sub->addChild(valueSubmenu("Density (%)", string::f("%d", gen.density), "0-100", gen.density,
				[=](int v) { gen.density = clamp(v, 0, 100); }));
			sub->addChild(valueSubmenu("Degrees below the root", string::f("%d", gen.degreesBelow), "0-24", gen.degreesBelow,
				[=](int v) { gen.degreesBelow = clamp(v, 0, 24); }));
			sub->addChild(valueSubmenu("Degrees above the root", string::f("%d", gen.degreesAbove), "0-24", gen.degreesAbove,
				[=](int v) { gen.degreesAbove = clamp(v, 0, 24); }));
			sub->addChild(valueSubmenu("Shortest note (ticks, 24 a step)", string::f("%d", gen.lenMin), "1-96", gen.lenMin,
				[=](int v) { gen.lenMin = clamp(v, 1, 96); }));
			sub->addChild(valueSubmenu("Longest note (ticks)", string::f("%d", gen.lenMax), "1-96", gen.lenMax,
				[=](int v) { gen.lenMax = clamp(v, 1, 96); }));
			sub->addChild(valueSubmenu("Softest", string::f("%d", gen.velMin), "1-127", gen.velMin,
				[=](int v) { gen.velMin = clamp(v, 1, 127); }));
			sub->addChild(valueSubmenu("Loudest", string::f("%d", gen.velMax), "1-127", gen.velMax,
				[=](int v) { gen.velMax = clamp(v, 1, 127); }));
			sub->addChild(createMenuLabel("Notes land on the snap grid"));
		}));
		menu->addChild(new MenuSeparator);
		{
			const int snaps[] = { 24, 12, 8, 6, 4, 3, 1 };
			const char* names[] = { "1 step", "1/2", "1/3", "1/4", "1/6", "1/8", "Off (1/24)" };
			std::vector<std::string> v(names, names + 7);
			menu->addChild(createIndexSubmenuItem("Snap", v,
				[=]() { for (int i = 0; i < 7; i++) if (snaps[i] == snapTicks) return (size_t)i; return (size_t)3; },
				[=](size_t i) { snapTicks = snaps[i]; gen.grid = snaps[i]; }));
		}
		{
			std::vector<std::string> lanes(L::laneNames, L::laneNames + L::kNumLanes);
			menu->addChild(createIndexSubmenuItem("Lane shown", lanes,
				[=]() { return (size_t)laneSel; }, [=](size_t i) { laneSel = (int)i; }));
			std::vector<std::string> ints(L::interpNames, L::interpNames + L::kNumInterps);
			menu->addChild(createIndexSubmenuItem("Lane curve", ints,
				[=]() { return (size_t)roll.pat.interp[laneSel]; },
				[=](size_t i) { roll.pat.interp[laneSel] = (uint8_t)i; sendRoll(); }));
			menu->addChild(createMenuItem("Clear the lane", "", [=]() {
				for (int s = 0; s < S::kMaxSteps; s++) roll.pat.lane[laneSel][s] = L::kLaneUnset;
				sendRoll();
			}));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear the notes", "", [=]() { roll.pat.count = 0; selNote = -1; sendRoll(); }));
		menu->addChild(createMenuLabel("Click: add · drag: move · drag the end: length"));
		menu->addChild(createMenuLabel("Alt-drag: velocity · right-click: delete"));
		menu->addChild(createMenuLabel("Arrows: move · shift-arrows: octave / length · scroll: pitch"));
	}

	// ---- SEQ ----------------------------------------------------------------

	struct SeqRect { float x0, x1, y0, y1, colW, rowH; };
	SeqRect seqRect() const {
		SeqRect r;
		r.x0 = pad + 16.f;
		r.x1 = box.size.x - pad;
		r.y0 = pad + 21.f;			// a line for the header, a line for the track numbers
		r.y1 = box.size.y - pad;
		r.colW = (r.x1 - r.x0) / S::kNumTracks;
		r.rowH = (r.y1 - r.y0) / L::kNumSlots;
		return r;
	}

	void seqButton(const ButtonEvent& e) {
		SeqRect r = seqRect();
		int k = (int)((e.pos.y - r.y0) / r.rowH);
		if (k < 0 || k >= L::kNumSlots) return;
		if (e.pos.x < r.x0) {
			// a row number: launch the whole row as a sequence; right-click for the row's menu
			if (e.button == GLFW_MOUSE_BUTTON_LEFT) module->queueSequence(k);
			else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) rowMenu(k);
			return;
		}
		int t = (int)((e.pos.x - r.x0) / r.colW);
		if (t < 0 || t >= S::kNumTracks) return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			module->queued[t] = k;
			module->requestSel = t;
		}
		else if (e.button == GLFW_MOUSE_BUTTON_RIGHT)
			slotMenu(t, k);
	}

	// The row clipboard: one slot per track.
	static L::Slot* rowClip() { static L::Slot* c = NULL; if (!c) { c = new L::Slot[S::kNumTracks]; for (int t = 0; t < S::kNumTracks; t++) c[t].clear(); } return c; }
	static bool& rowClipFull() { static bool b = false; return b; }

	void rowMenu(int k) {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Row %d", k + 1)));
		menu->addChild(createMenuItem("Launch the row", "", [=]() { module->queueSequence(k); }));
		menu->addChild(createMenuItem("Add it to the song", "", [=]() {
			L::SongEntry s;
			s.row = (int8_t)k;
			s.times = 1;
			songCmd(Ledger::SongCmd::INSERT, module->song.len, s, "add to song");
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Copy the row", "", [=]() {
			for (int t = 0; t < S::kNumTracks; t++) module->readSlotFull(t, k, rowClip()[t]);
			rowClipFull() = true;
		}));
		if (rowClipFull())
			menu->addChild(createMenuItem("Paste the row here", "", [=]() {
				history::ComplexAction* h = new history::ComplexAction;
				h->name = "Ledger: paste row";
				for (int t = 0; t < S::kNumTracks; t++) sendSlot(t, k, rowClip()[t], "paste row", h);
				U::push(h);
			}));
		menu->addChild(createMenuItem("Clear the row", "", [=]() {
			history::ComplexAction* h = new history::ComplexAction;
			h->name = "Ledger: clear row";
			L::Slot* s = new L::Slot;
			s->clear();
			for (int t = 0; t < S::kNumTracks; t++) sendSlot(t, k, *s, "clear row", h);
			delete s;
			U::push(h);
		}));
	}

	void slotMenu(int t, int k) {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Track %d, slot %d", t + 1, k + 1)));
		menu->addChild(createMenuItem("Launch", "", [=]() { module->queued[t] = k; }));
		menu->addChild(createMenuItem("Launch the whole row", "", [=]() { module->queueSequence(k); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("New generator (a random seed)", "", [=]() {
			L::Slot* s = new L::Slot;
			s->clear();
			s->kind = L::kSlotGen;
			L::storeBlock(s->block, module->base, t);
			s->block[L::kBlockSeed] = (int16_t)(random::u32() % 1000);
			sendSlot(t, k, *s, "new generator");
			delete s;
		}));
		menu->addChild(createMenuItem("New pattern (empty)", "", [=]() {
			L::Slot* s = new L::Slot;
			s->clear();
			s->kind = L::kSlotPat;
			L::storeBlock(s->block, module->base, t);
			L::captureBlock(s->block);
			sendSlot(t, k, *s, "new pattern");
			delete s;
		}));
		menu->addChild(createMenuItem("Copy the playing slot here", "", [=]() {
			int a = snap.active[t];
			L::Slot* s = new L::Slot;
			module->readSlot(t, a, *s);
			L::storeBlock(s->block, module->base, t);
			sendSlot(t, k, *s, "copy slot");
			delete s;
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() {
			L::Slot* s = new L::Slot;
			s->clear();
			sendSlot(t, k, *s, "clear slot");
			delete s;
		}));
	}

	// ---- SONG ---------------------------------------------------------------

	enum { SONG_COLS = 4, SONG_ROWS = 8 };
	int songSel = -1;

	struct SongRect { float x0, y0, cellW, cellH; };
	SongRect songRect() const {
		SongRect r;
		r.x0 = pad;
		r.y0 = pad + 13.f;
		r.cellW = (box.size.x - 2 * pad) / SONG_COLS;
		r.cellH = (box.size.y - pad - r.y0) / SONG_ROWS;
		return r;
	}
	// Entries run down each column, then on to the next.
	int songCellAt(Vec p) const {
		SongRect r = songRect();
		if (p.y < r.y0 || p.x < r.x0) return -1;
		int c = (int)((p.x - r.x0) / r.cellW), row = (int)((p.y - r.y0) / r.cellH);
		if (c < 0 || c >= SONG_COLS || row < 0 || row >= SONG_ROWS) return -1;
		return c * SONG_ROWS + row;
	}

	void songCmd(int op, int at, L::SongEntry e, const char* what) {
		if (!module) return;
		U::push(new U::SongUndo(module, what));
		Ledger::SongCmd* c = new Ledger::SongCmd;
		std::memset(c, 0, sizeof *c);
		c->op = op;
		c->at = at;
		c->e = e;
		for (int i = 0; i < 200 && !module->sendSong(*c); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		delete c;
	}

	void songButton(const ButtonEvent& e) {
		SongRect r = songRect();
		if (e.pos.y < r.y0) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT) module->songOn = !module->songOn.load();
			return;
		}
		int i = songCellAt(e.pos);
		if (i < 0) return;
		int len = module->song.len;
		if (i == len && len < L::kSongMax && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// the + cell: the next row after the last one, or the selected track's playing row
			L::SongEntry s;
			s.row = (int8_t)(len ? (module->song.e[len - 1].row + 1) % L::kNumSlots : snap.active[snap.sel]);
			s.times = 1;
			songCmd(Ledger::SongCmd::INSERT, len, s, "add to song");
			songSel = len;
			return;
		}
		if (i >= len) return;
		songSel = i;
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) songMenu(i);
	}

	void songMenu(int i) {
		ui::Menu* menu = createMenu();
		L::SongEntry cur = module->song.e[i];
		menu->addChild(createMenuLabel(string::f("Song entry %d", i + 1)));
		std::vector<std::string> rows, times;
		for (int k = 1; k <= L::kNumSlots; k++) rows.push_back(string::f("Row %d", k));
		for (int n = 1; n <= 16; n++) times.push_back(string::f("%d", n));
		menu->addChild(createIndexSubmenuItem("Row", rows,
			[=]() { return (size_t)module->song.e[i].row; },
			[=](size_t k) { L::SongEntry s = module->song.e[i]; s.row = (int8_t)k; songCmd(Ledger::SongCmd::SET, i, s, "change song entry"); }));
		menu->addChild(createIndexSubmenuItem("Times through", times,
			[=]() { return (size_t)(module->song.e[i].times - 1); },
			[=](size_t k) { L::SongEntry s = module->song.e[i]; s.times = (uint8_t)(k + 1); songCmd(Ledger::SongCmd::SET, i, s, "change song entry"); }));
		menu->addChild(new MenuSeparator);
		if (module->song.len < L::kSongMax)
			menu->addChild(createMenuItem("Duplicate", "", [=]() { songCmd(Ledger::SongCmd::INSERT, i + 1, cur, "duplicate song entry"); }));
		if (i > 0)
			menu->addChild(createMenuItem("Move earlier", "", [=]() {
				Ledger::SongCmd* c = new Ledger::SongCmd;
				std::memset(c, 0, sizeof *c);
				std::memcpy(c->all, module->song.e, sizeof c->all);
				std::swap(c->all[i], c->all[i - 1]);
				c->op = Ledger::SongCmd::ALL; c->len = module->song.len;
				U::push(new U::SongUndo(module, "move song entry"));
				module->sendSong(*c);
				delete c;
				songSel = i - 1;
			}));
		if (i < module->song.len - 1)
			menu->addChild(createMenuItem("Move later", "", [=]() {
				Ledger::SongCmd* c = new Ledger::SongCmd;
				std::memset(c, 0, sizeof *c);
				std::memcpy(c->all, module->song.e, sizeof c->all);
				std::swap(c->all[i], c->all[i + 1]);
				c->op = Ledger::SongCmd::ALL; c->len = module->song.len;
				U::push(new U::SongUndo(module, "move song entry"));
				module->sendSong(*c);
				delete c;
				songSel = i + 1;
			}));
		menu->addChild(createMenuItem("Delete", "", [=]() { songCmd(Ledger::SongCmd::REMOVE, i, cur, "delete song entry"); songSel = -1; }));
		menu->addChild(createMenuItem("Clear the song", "", [=]() {
			Ledger::SongCmd* c = new Ledger::SongCmd;
			std::memset(c, 0, sizeof *c);
			c->op = Ledger::SongCmd::ALL; c->len = 0;
			U::push(new U::SongUndo(module, "clear song"));
			module->sendSong(*c);
			delete c;
			songSel = -1;
		}));
	}

	void drawSong(NVGcontext* vg) {
		SongRect r = songRect();
		const panel::TextStyle HEAD(panel::Face::Mono, 7.5f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		if (!module) {
			panel::text(vg, HEAD, pad, pad + 8.f, "SONG");
			return;
		}
		int lq = module->launchQ;
		if (lq == kLaunchNow || lq == kLaunchStep || lq == kLaunchLoop) lq = kLaunchModulo;
		float x = panel::text(vg, HEAD.inked(snap.songOn ? panel::LIME : panel::SAGE), pad, pad + 8.f,
			snap.songOn ? "SONG PLAYING  " : "SONG STOPPED  ");
		headText(vg, HEAD, x, pad + 8.f, string::f("a pass: %s  ·  click here to start or stop  ·  + adds a row  ·  scroll: the row (shift: times)  ·  right-click for more",
			launchNames[lq]));
		const L::Song& sg = module->song;		// small ints the UI only reads; a torn read mislabels a frame
		int len = std::min(sg.len, (int)L::kSongMax);
		const panel::TextStyle CELL(panel::Face::Mono, std::min(8.f, r.cellH * 0.6f), panel::PAPER, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i <= len && i < L::kSongMax; i++) {
			float cx = r.x0 + (i / SONG_ROWS) * r.cellW, cy = r.y0 + (i % SONG_ROWS) * r.cellH;
			bool playing = snap.songOn && snap.songPos == i;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, cx + 1.f, cy + 0.6f, r.cellW - 2.f, r.cellH - 1.2f, 1.f);
			nvgFillColor(vg, playing ? panel::alpha(panel::LIME, 0.35f) : panel::alpha(panel::SAGE, i < len ? 0.16f : 0.05f));
			nvgFill(vg);
			if (i == songSel) {
				nvgStrokeColor(vg, panel::alpha(panel::PAPER, 0.6f));
				nvgStrokeWidth(vg, 0.6f);
				nvgStroke(vg);
			}
			float ty = cy + r.cellH * 0.5f;
			if (i == len) {
				panel::text(vg, CELL.inked(panel::SAGE), cx + 5.f, ty, "+");
				continue;
			}
			panel::text(vg, CELL.inked(panel::SAGE), cx + 5.f, ty, string::f("%2d", i + 1));
			panel::text(vg, CELL.inked(playing ? panel::PAPER : panel::PAPER), cx + 22.f, ty, string::f("ROW %d", sg.e[i].row + 1));
			std::string tail = playing ? string::f("%d of %d", sg.e[i].times - snap.songLeft + 1, sg.e[i].times)
				: string::f("x%d", sg.e[i].times);
			panel::text(vg, CELL.inked(playing ? panel::LIME : panel::SAGE).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE),
				cx + r.cellW - 5.f, ty, tail);
		}
	}

	// ---- FX -----------------------------------------------------------------

	struct FxRect { float lx0, lx1, px0, px1, y0, y1, rowH, prowH; };
	FxRect fxRect() const {
		FxRect r;
		r.lx0 = pad;
		r.lx1 = pad + box.size.x * 0.28f;
		r.px0 = r.lx1 + 10.f;
		r.px1 = box.size.x - pad;
		r.y0 = pad + 11.f;
		r.y1 = box.size.y - pad;
		r.rowH = (r.y1 - r.y0) / L::kChainSlots;
		r.prowH = (r.y1 - r.y0) / L::kMaxFxParams;
		return r;
	}

	int fxTrack() const { return snap.sel; }
	L::Fx& fxAt(int i) { return module->chains[fxTrack()].fx[i]; }

	// The value a parameter is set to where an edit lands: this slot's own, if it has one.
	int fxSetValue(int i, int k, bool* own = NULL) {
		int t = fxTrack();
		const L::Slot& sl = module->slots[t][snap.active[t]];
		int o = sl.findOverride(i, k);
		if (own) *own = o >= 0;
		return o >= 0 ? sl.ov[o].value : fxAt(i).p[k];
	}

	void fxWrite(int i, int k, int v) {
		L::Fx& f = fxAt(i);
		const L::ParamDesc& pd = f.desc().p[k];
		v = clamp(v, (int)pd.min, (int)pd.max);
		bool own = false;
		fxSetValue(i, k, &own);
		if (own) {
			FxCmd c = {};
			c.op = FxCmd::OVERRIDE_SET; c.track = fxTrack(); c.fx = i; c.slot = snap.active[fxTrack()]; c.param = k; c.value = v;
			module->sendFx(c);
		}
		else
			f.p[k] = (int16_t)v;		// a whole int16: the audio thread reads it whole
	}

	void fxCmd(int op, int i, int value = 0, int other = -1, int param = 0, int slot = -1) {
		FxCmd c = {};
		c.op = op; c.track = fxTrack(); c.fx = i; c.value = value; c.other = other; c.param = param;
		c.slot = slot < 0 ? snap.active[fxTrack()] : slot;
		module->sendFx(c);
	}

	void fxButton(const ButtonEvent& e) {
		FxRect r = fxRect();
		Vec p = e.pos;
		if (p.x >= r.lx0 && p.x <= r.lx1 && p.y >= r.y0 && p.y < r.y1) {
			int i = clamp((int)((p.y - r.y0) / r.rowH), 0, L::kChainSlots - 1);
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { fxSel = i; fxSlotMenu(i); return; }
			if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
			// the two boxes at the right of the row: M (everywhere), S (this slot)
			if (p.x > r.lx1 - 22.f && p.x <= r.lx1 - 12.f) { fxUndo("mute effect"); fxAt(i).gmute = !fxAt(i).gmute; return; }
			if (p.x > r.lx1 - 11.f) {
				fxUndo("mute effect on slot");
				const L::Slot& sl = module->slots[fxTrack()][snap.active[fxTrack()]];
				fxCmd(FxCmd::SLOT_MUTE, i, !((sl.fxMute >> i) & 1));
				return;
			}
			fxSel = i;
			return;
		}
		if (p.x >= r.px0 && p.x <= r.px1 && p.y >= r.y0 && p.y < r.y1) {
			const L::Fx& f = fxAt(fxSel);
			int k = (int)((p.y - r.y0) / r.prowH);
			if (k < 0 || k >= f.desc().nParams) return;
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { fxParamMenu(fxSel, k); return; }
			if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
			drag = DRAG_FX;
			fxParam = k;
			delete fxPending;
			fxPending = new U::FxUndo(module, fxTrack(), "set effect parameter");
			fxDragAcc = 0.f;
			fxDragStart = fxSetValue(fxSel, k);
		}
	}

	// Dragging across the bar: its whole width is the parameter's whole range.
	void fxDrag(float dx) {
		if (fxParam < 0) return;
		FxRect r = fxRect();
		const L::ParamDesc& pd = fxAt(fxSel).desc().p[fxParam];
		fxDragAcc += dx / std::max(1.f, (r.px1 - r.px0) * 0.55f) * (pd.max - pd.min);
		fxWrite(fxSel, fxParam, fxDragStart + (int)std::round(fxDragAcc));
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (module && page() == kPageFx && fxParam >= 0) {
			const L::ParamDesc& pd = fxAt(fxSel).desc().p[fxParam];
			fxUndo("reset effect parameter");
			fxWrite(fxSel, fxParam, pd.def);
			e.consume(this);
			return;
		}
		OpaqueWidget::onDoubleClick(e);
	}

	static std::string fxValueText(const L::ParamDesc& pd, int v) {
		if (pd.labels) return pd.labels[clamp(v, (int)pd.min, (int)pd.max) - pd.min];
		return string::f("%d%s", v, pd.unit);
	}

	void fxSlotMenu(int i) {
		ui::Menu* menu = createMenu();
		int t = fxTrack();
		menu->addChild(createMenuLabel(string::f("Track %d, effect %d", t + 1, i + 1)));
		menu->addChild(createSubmenuItem("Effect", fxAt(i).desc().name, [=](Menu* sub) {
			for (int ty = 0; ty < L::kNumFxTypes; ty++)
				sub->addChild(createCheckMenuItem(L::fxDescs[ty].name, "",
					[=]() { return fxAt(i).type == ty; }, [=]() { fxUndo("choose effect"); fxCmd(FxCmd::SET_TYPE, i, ty); }));
		}));
		if (i > 0) menu->addChild(createMenuItem("Move up", "", [=]() { fxUndo("move effect"); fxCmd(FxCmd::SWAP, i, 0, i - 1); fxSel = i - 1; }));
		if (i < L::kChainSlots - 1) menu->addChild(createMenuItem("Move down", "", [=]() { fxUndo("move effect"); fxCmd(FxCmd::SWAP, i, 0, i + 1); fxSel = i + 1; }));
		menu->addChild(createMenuItem("Copy", "", [=]() {
			clipType() = fxAt(i).type;
			for (int k = 0; k < L::kMaxFxParams; k++) clipParams()[k] = fxAt(i).p[k];
		}));
		if (clipType() >= 0)
			menu->addChild(createMenuItem(string::f("Paste (%s)", L::fxDescs[clipType()].name), "", [=]() {
				fxUndo("paste effect");
				FxCmd c = {};
				c.op = FxCmd::PASTE; c.track = t; c.fx = i; c.value = clipType(); c.slot = snap.active[t];
				for (int k = 0; k < L::kMaxFxParams; k++) c.params[k] = clipParams()[k];
				module->sendFx(c);
			}));
		menu->addChild(createMenuItem("Clear", "", [=]() { fxUndo("clear effect"); fxCmd(FxCmd::SET_TYPE, i, L::kFxNone); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolMenuItem("Muted on every slot", "", [=]() { return fxAt(i).gmute; },
			[=](bool b) { fxUndo("mute effect"); fxAt(i).gmute = b; }));
		menu->addChild(createBoolMenuItem(string::f("Muted on slot %d", snap.active[t] + 1), "",
			[=]() { return (module->slots[t][snap.active[t]].fxMute >> i) & 1; },
			[=](bool b) { fxUndo("mute effect on slot"); fxCmd(FxCmd::SLOT_MUTE, i, b); }));
	}
	static int& clipType() { static int c = -1; return c; }
	static int16_t* clipParams() { static int16_t c[L::kMaxFxParams] = {}; return c; }

	void fxParamMenu(int i, int k) {
		ui::Menu* menu = createMenu();
		int t = fxTrack();
		const L::ParamDesc pd = fxAt(i).desc().p[k];
		bool own = false;
		int v = fxSetValue(i, k, &own);
		menu->addChild(createMenuLabel(string::f("%s: %s", pd.name, fxValueText(pd, v).c_str())));
		menu->addChild(valueSubmenu("Type a value", fxValueText(pd, v), string::f("%d to %d", pd.min, pd.max), v,
			[=](int nv) { fxUndo("set effect parameter"); fxWrite(i, k, nv); }));
		int slot = snap.active[t];
		if (own)
			menu->addChild(createMenuItem(string::f("Back to the track's value (slot %d stops having its own)", slot + 1), "",
				[=]() { fxUndo("slot value"); fxCmd(FxCmd::OVERRIDE_CLEAR, i, 0, -1, k); }));
		else
			menu->addChild(createMenuItem(string::f("Give slot %d its own value", slot + 1), "",
				[=]() { fxUndo("slot value"); fxCmd(FxCmd::OVERRIDE_SET, i, v, -1, k); }));
		menu->addChild(createMenuItem("Reset to the default", "", [=]() { fxUndo("reset effect parameter"); fxWrite(i, k, pd.def); }));
		menu->addChild(createSubmenuItem("Modulate with", "", [=](Menu* sub) {
			for (int src = 1; src <= L::kNumMatrixSources; src++)
				sub->addChild(createMenuItem(src == L::kSrcCC ? "MIDI CC (learn: move a controller)" : L::sourceNames[src], "", [=]() {
					// the track's first free matrix slot, full depth
					for (int m = 0; m < L::kNumMatrixSlots; m++) {
						L::MatrixSlot& ms = module->matrix[t][m];
						if (ms.live()) continue;
						L::MatrixSlot n;
						n.source = (int8_t)src;
						n.dest = (int8_t)L::fxDest(i, k);
						ms = n;
						// a CC: the next one the track receives is the one
						if (src == L::kSrcCC) module->learn = t * L::kNumMatrixSlots + m;
						return;
					}
				}));
		}));
	}

	void drawFx(NVGcontext* vg) {
		FxRect r = fxRect();
		int t = fxTrack();
		const panel::TextStyle HEAD(panel::Face::Mono, 7.5f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		if (!module) {
			panel::text(vg, HEAD, pad, pad + 8.f, "EFFECTS");
			return;
		}
		int slot = snap.active[t];
		const L::Slot& sl = module->slots[t][slot];
		bool live = module->routed[t];
		float hx = panel::text(vg, HEAD.inked(panel::LIME), pad, pad + 8.f, string::f("T%d EFFECTS  ", t + 1));
		headText(vg, HEAD, hx, pad + 8.f, string::f("slot %d  ·  M mutes everywhere, S on this slot  ·  drag a bar; double-click resets; right-click for more%s",
			slot + 1, live ? "" : "  ·  (a generator with no live effect plays as Shoal made it)"));
		const panel::TextStyle ROW(panel::Face::Mono, std::min(8.f, r.rowH * 0.62f), panel::PAPER, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < L::kChainSlots; i++) {
			const L::Fx& f = fxAt(i);
			float y = r.y0 + i * r.rowH, yc = y + r.rowH * 0.5f;
			bool selF = i == fxSel;
			bool gm = f.gmute, sm = (sl.fxMute >> i) & 1;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.lx0, y + 0.5f, r.lx1 - r.lx0, r.rowH - 1.f, 1.f);
			nvgFillColor(vg, selF ? panel::alpha(panel::LIME, 0.22f) : panel::alpha(panel::SAGE, f.type ? 0.12f : 0.05f));
			nvgFill(vg);
			NVGcolor ink = f.type == L::kFxNone ? panel::alpha(panel::SAGE, 0.6f) : (gm || sm) ? panel::SAGE : panel::PAPER;
			panel::text(vg, ROW.inked(ink), r.lx0 + 3.f, yc, string::f("%d %s", i + 1, f.desc().name));
			for (int b = 0; b < 2; b++) {
				float bx = r.lx1 - 22.f + b * 11.f;
				bool on = b == 0 ? gm : sm;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, bx, yc - 4.f, 9.f, 8.f, 1.f);
				nvgFillColor(vg, on ? panel::LIME : panel::alpha(panel::SAGE, 0.18f));
				nvgFill(vg);
				panel::text(vg, ROW.sized(6.f).inked(on ? panel::GLASS : panel::SAGE).aligned(NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE),
					bx + 4.5f, yc + 0.3f, b == 0 ? "M" : "S");
			}
		}
		// the chosen effect's parameters
		const L::Fx& f = fxAt(fxSel);
		const panel::TextStyle PRM(panel::Face::Mono, std::min(7.5f, r.prowH * 0.62f), panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		if (f.type == L::kFxNone) {
			panel::text(vg, PRM, r.px0, r.y0 + r.prowH * 0.5f, "Empty: right-click the slot to choose an effect");
			return;
		}
		float nameW = (r.px1 - r.px0) * 0.36f;
		float bx0 = r.px0 + nameW, bx1 = r.px1 - 70.f;
		for (int k = 0; k < f.desc().nParams; k++) {
			const L::ParamDesc& pd = f.desc().p[k];
			float y = r.y0 + k * r.prowH, yc = y + r.prowH * 0.5f;
			bool own = false;
			int v = fxSetValue(fxSel, k, &own);
			int pv = f.pe[k];
			panel::text(vg, PRM.inked(fxParam == k && drag == DRAG_FX ? panel::LIME : panel::SAGE), r.px0, yc, pd.name);
			float frac = pd.max > pd.min ? (float)(v - pd.min) / (pd.max - pd.min) : 0.f;
			nvgBeginPath(vg);
			nvgRect(vg, bx0, yc - 2.5f, bx1 - bx0, 5.f);
			nvgFillColor(vg, panel::alpha(panel::SAGE, 0.15f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, bx0, yc - 2.5f, (bx1 - bx0) * frac, 5.f);
			nvgFillColor(vg, own ? panel::LIME : panel::alpha(panel::PAPER, 0.75f));
			nvgFill(vg);
			if (pv != v) {
				// what is playing, under CV: a tick where it is
				float pf = pd.max > pd.min ? (float)(pv - pd.min) / (pd.max - pd.min) : 0.f;
				nvgBeginPath(vg);
				nvgRect(vg, bx0 + (bx1 - bx0) * pf - 0.6f, yc - 4.f, 1.2f, 8.f);
				nvgFillColor(vg, panel::LIME);
				nvgFill(vg);
			}
			std::string val = fxValueText(pd, v);
			if (pv != v) val += " > " + fxValueText(pd, pv);
			panel::text(vg, PRM.inked(own ? panel::LIME : panel::PAPER), bx1 + 4.f, yc, val);
			if (own) panel::text(vg, PRM.sized(5.5f).inked(panel::LIME).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE), bx0 - 3.f, yc, "slot");
		}
	}

	// ---- drawing ------------------------------------------------------------

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !haveSnap) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		switch (page()) {
			case kPageRoll: drawRoll(args.vg); break;
			case kPageFx: drawFx(args.vg); break;
			case kPageSeq: drawSeq(args.vg); break;
			case kPageSong: drawSong(args.vg); break;
			default: drawTank(args.vg); break;
		}
	}

	bool isPat(int t) const { return cache[t].kind != L::kSlotGen; }

	void drawTank(NVGcontext* vg) {
		const int16_t* v = view.v;
		const S::Dtc* dtc = view.dtc;
		bool anySolo = S::anySoloOf(&view);
		const float lh = (box.size.y - 2 * pad) / S::kNumTracks;
		const float numW = 9.f, tailW = 34.f;
		const float lx0 = pad + numW, lx1 = laneW() - tailW;

		const panel::TextStyle NUM(panel::Face::Mono, std::min(9.f, lh * 0.8f), panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const panel::TextStyle TAIL = NUM.sized(std::min(7.5f, lh * 0.7f));

		for (int t = 0; t < S::kNumTracks; t++) {
			const S::TrackState& tr = dtc->tracks[t];
			float y0 = pad + t * lh, yc = y0 + lh * 0.5f;
			bool selT = t == snap.sel;
			bool modT = snap.modMask[t] != 0;
			bool muted = v[S::TP(t, S::kTMute)] || (anySolo && !view.solo[t]);
			int length = v[S::TP(t, S::kTLength)];
			NVGcolor base = muted ? panel::alpha(panel::SAGE, 0.35f)
				: tr.resting ? panel::alpha(panel::SAGE, 0.6f)
				: selT ? panel::PAPER : panel::alpha(panel::PAPER, 0.62f);

			if (selT) {
				nvgBeginPath(vg);
				nvgRect(vg, 1.f, y0 + 1.f, 1.6f, lh - 2.f);
				nvgFillColor(vg, panel::LIME);
				nvgFill(vg);
			}
			NVGcolor numInk = modT ? panel::LIME : selT ? panel::PAPER
				: muted ? panel::alpha(panel::SAGE, 0.5f) : panel::SAGE;
			panel::text(vg, NUM.inked(numInk), pad, yc, string::f("%d", t + 1));

			// the whole pattern, always: never scrolled, cells narrow as it grows
			int cells = std::max(16, length);
			float cw = (lx1 - lx0) / cells;
			float bw = std::max(1.f, cw - (cw > 4.f ? 1.5f : 0.5f));
			float barTop = y0 + 1.5f, barBot = y0 + lh - 1.5f, barH = barBot - barTop;
			bool pat = isPat(t);
			int shift = v[S::XP(t, S::kXShift)];
			int pLo = 127, pHi = 0;
			if (pat)
				for (int i = 0; i < cache[t].pat.count; i++) {
					pLo = std::min(pLo, (int)cache[t].pat.notes[i].pitch);
					pHi = std::max(pHi, (int)cache[t].pat.notes[i].pitch);
				}
			for (int s = 0; s < cells; s++) {
				float x = lx0 + s * cw;
				if (s >= length) {
					nvgBeginPath(vg);
					nvgCircle(vg, x + bw * 0.5f, yc, 0.5f);
					nvgFillColor(vg, panel::alpha(panel::SAGE, 0.3f));
					nvgFill(vg);
					continue;
				}
				bool isPos = s == tr.pos;
				if (isPos) {
					nvgBeginPath(vg);
					nvgRect(vg, x, barTop, bw, barH);
					nvgFillColor(vg, panel::alpha(panel::SAGE, 0.28f));
					nvgFill(vg);
				}
				float hFrac = 0.f;
				int chord = 0;
				if (pat) {
					// a pattern's step: its highest note, and a tick per extra note in it
					int st = ((s + shift) % length + length) % length;
					int a = st * L::kTicksPerStep, b = a + L::kTicksPerStep, top = -1;
					for (int i = 0; i < cache[t].pat.count; i++) {
						const L::Note& n = cache[t].pat.notes[i];
						if (n.start < a) continue;
						if (n.start >= b) break;
						top = std::max(top, (int)n.pitch);
						chord++;
					}
					if (top >= 0)
						hFrac = pHi > pLo ? 0.2f + 0.8f * (top - pLo) / (float)(pHi - pLo) : 0.6f;
				}
				else {
					S::StepEval ev;
					S::evalStep(&view, t, s, ev);
					if (ev.fires) hFrac = ev.barH / 5.f;
				}
				nvgBeginPath(vg);
				if (hFrac <= 0.f) {
					nvgRect(vg, x, barBot - 0.7f, bw, 0.7f);
					nvgFillColor(vg, panel::alpha(panel::SAGE, muted ? 0.25f : 0.45f));
				}
				else {
					float h = barH * hFrac;
					nvgRect(vg, x, barBot - h, bw, h);
					nvgFillColor(vg, isPos ? panel::LIME : base);
				}
				nvgFill(vg);
				for (int c = 1; c < chord && c < 4; c++) {
					nvgBeginPath(vg);
					nvgRect(vg, x, barTop - 0.2f + c * 1.6f - 1.6f, bw, 0.6f);
					nvgFillColor(vg, panel::alpha(panel::LIME, 0.8f));
					nvgFill(vg);
				}
			}

			// rate, slot, follow, solo/mute
			int src = v[S::TP(t, S::kTSource)];
			std::string tail = string::f("%s %c%d", S::rateNames[v[S::TP(t, S::kTRate)]],
				pat ? 'P' : 'G', snap.active[t] + 1);
			if (src > 0 && src - 1 != t && !pat) tail += string::f(" <%d", src);
			bool rateMod = snap.modMask[t] & (1u << L::kDRate);
			panel::text(vg, TAIL.inked(rateMod ? panel::LIME : selT ? panel::PAPER : panel::SAGE), lx1 + 3.f, yc, tail);
			const char* flag = view.solo[t] ? "S" : muted ? "M" : snap.queued[t] >= 0 ? ">" : "";
			panel::text(vg, TAIL.inked(view.solo[t] || snap.queued[t] >= 0 ? panel::LIME : panel::SAGE)
				.aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE), laneW() - 2.f, yc, flag);
		}

		nvgBeginPath(vg);
		nvgMoveTo(vg, laneW() + 2.f, pad);
		nvgLineTo(vg, laneW() + 2.f, box.size.y - pad);
		nvgStrokeColor(vg, panel::alpha(panel::RULE, 0.6f));
		nvgStrokeWidth(vg, 0.5f);
		nvgStroke(vg);

		drawBooks(vg);
	}

	// The books: the selected track's settings, each printed in the field that sets it
	// (FIELD_* in Panel.hpp), so the number you read is the number you drag. A header row
	// above the fields, two columns of five, the key, and a status line under it.
	void drawBooks(NVGcontext* vg) {
		const int16_t* v = view.v;
		int t = snap.sel;
		const S::TrackState& tr = view.dtc->tracks[t];
		uint32_t mm = snap.modMask[t];
		bool pat = isPat(t);
		const Rect first = local(panel::FIELD_CHANCE), last = local(panel::FIELD_TRNS);
		const float step = local(panel::FIELD_NOTE).pos.y - first.pos.y;		// one grid row
		const float x0 = first.pos.x, x1 = last.pos.x + last.size.x, rh = first.size.y;
		const float fs = std::min(8.5f, rh * 0.68f);
		const panel::TextStyle TAG(panel::Face::Mono, fs, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL = TAG.inked(panel::PAPER).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		auto base = [&](const Rect& r) { return r.pos.y + r.size.y * 0.72f; };

		// the header: the track, and the seed it is playing or what its slot holds
		Rect head = first;
		head.pos.y -= step;
		float hb = base(head);
		float end = panel::text(vg, panel::TextStyle(panel::Face::Mono, std::min(12.f, rh * 0.95f), panel::LIME,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE), x0 + 2.f, hb, string::f("T%d", t + 1));
		float sx = std::max(end + 6.f, x0 + (x1 - x0) * 0.3f);
		if (pat) {
			sx = panel::text(vg, TAG, sx, hb, string::f("SLOT %d ", snap.active[t] + 1));
			panel::text(vg, TAG.inked(panel::PAPER), sx, hb, cache[t].kind == L::kSlotEmpty ? "EMPTY"
				: string::f("%d NOTES", cache[t].pat.count));
		}
		else {
			sx = panel::text(vg, TAG, sx, hb, "SEED ");
			panel::segValue(vg, sx, hb, rh * 0.75f, string::f("%03d", (int)tr.activeSeed), "", panel::PAPER);
		}

		struct Pair { const Rect* f; const char* tag; std::string val; int dest; bool used; };
		const Pair pairs[] = {
			{ &panel::FIELD_CHANCE, "CH", string::f("%d", v[S::TP(t, S::kTChance)]), L::kDChance, true },
			{ &panel::FIELD_NOTE, "NT", string::f("%+d", v[S::TP(t, S::kTNote)]), L::kDNote, !pat },
			{ &panel::FIELD_OCTAVE, "OC", string::f("%+d", v[S::TP(t, S::kTOct)]), L::kDOct, !pat },
			{ &panel::FIELD_RATE, "RATE", S::rateNames[v[S::TP(t, S::kTRate)]], L::kDRate, true },
			{ &panel::FIELD_LENG, "LEN", string::f("%d", v[S::TP(t, S::kTLength)]), L::kDLength, true },
			{ &panel::FIELD_DIRN, "DIR", S::directionShort[v[S::TP(t, S::kTDirection)]], L::kDDirection, true },
			{ &panel::FIELD_EVOLVE, "EV", string::f("%d", v[S::TP(t, S::kTEvolve)]), L::kDEvolve, !pat },
			{ &panel::FIELD_BREATHE, "BR", string::f("%d", v[S::TP(t, S::kTBreathe)]), L::kDBreathe, true },
			{ &panel::FIELD_OCTA, "OCT", string::f("%+d", v[S::TP(t, S::kTOctave)]), L::kDOctave, true },
			{ &panel::FIELD_TRNS, "TR", string::f(pat ? "%+dst" : "%+d", v[S::TP(t, S::kTTrans)]), L::kDTrans, true },
		};
		for (const Pair& p : pairs) {
			Rect r = local(*p.f);
			float y = base(r);
			NVGcolor ink = !p.used ? panel::alpha(panel::SAGE, 0.4f)
				: (mm & (1u << p.dest)) ? panel::LIME : panel::PAPER;
			panel::text(vg, TAG, r.pos.x + 3.f, y, p.tag);
			panel::text(vg, VAL.inked(ink), r.pos.x + r.size.x - 3.f, y, p.used ? p.val : std::string("-"));
		}

		// the key: the root and the scale are fields of their own; who it follows and the
		// transpose leader's offset after them
		{
			const Rect rr = local(panel::FIELD_ROOT), sr = local(panel::FIELD_SCALE);
			float y = base(rr);
			NVGcolor rootInk = (snap.gMod & Ledger::GMOD_ROOT) ? panel::LIME : panel::PAPER;
			NVGcolor scaleInk = (snap.gMod & Ledger::GMOD_SCALE) ? panel::LIME : panel::PAPER;
			panel::text(vg, TAG.inked(rootInk), rr.pos.x + 3.f, y, NOTE_NAMES[v[S::kGRoot] % 12]);
			panel::text(vg, TAG.inked(scaleInk), sr.pos.x + 3.f, y, S::scaleShort[v[S::kGScale]]);
			int src = v[S::TP(t, S::kTSource)];
			std::string tail;
			if (src > 0 && src - 1 != t && !pat) tail += string::f("<%d", src);
			if (snap.trans != 0 && module && module->followTrans[t]) tail += string::f(" %+d", snap.trans);
			if (!tail.empty())
				panel::text(vg, TAG.aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), x1 - 3.f, y, tail);
		}

		// the status line under the key
		Rect st = local(panel::FIELD_ROOT);
		st.pos.y += step;
		float y = base(st);
		if (tr.pendingSeed >= 0 && !pat) {
			if ((int)(system::getTime() * 4.0) & 1)
				panel::text(vg, TAG.inked(panel::LIME), x0 + 3.f, y, "RESEED ARM");
		}
		if (snap.rec) {
			// recording; flashing while it waits for a first note or the clock
			if (snap.rec == 2 || ((int)(system::getTime() * 4.0) & 1))
				panel::text(vg, TAG.inked(panel::LIME).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), x1 - 3.f, y, "REC");
		}
		else if (v[S::kGFreeze])
			panel::text(vg, TAG.inked(panel::LIME).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), x1 - 3.f, y, "FRZ");
		else if (!v[S::kGRun])
			panel::text(vg, TAG.aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), x1 - 3.f, y, "STOP");
		else if (snap.queued[t] >= 0)
			panel::text(vg, TAG.inked(panel::LIME).aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), x1 - 3.f, y,
				string::f("> %d", snap.queued[t] + 1));
	}

	void drawRoll(NVGcontext* vg) {
		RollRect r = rollRect();
		int t = rollTrack < 0 ? snap.sel : rollTrack;
		int len = rollLen();
		const panel::TextStyle HEAD(panel::Face::Mono, 7.5f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		std::string head = string::f("T%d  SLOT %d  ", t + 1, rollSlot + 1);
		float x = panel::text(vg, HEAD.inked(panel::LIME), pad, pad + 8.f, head);
		if (roll.kind == L::kSlotGen) {
			headText(vg, HEAD, x, pad + 8.f, "A GENERATOR -- CAPTURE writes it down; right-click for more");
			return;
		}
		const char* snaps[] = { "1", "1/2", "1/3", "1/4", "1/6", "1/8", "OFF" };
		const int snapT[] = { 24, 12, 8, 6, 4, 3, 1 };
		const char* sn = "1/4";
		for (int i = 0; i < 7; i++) if (snapT[i] == snapTicks) sn = snaps[i];
		headText(vg, HEAD, x, pad + 8.f, string::f("%s  LEN %d  %s  SNAP %s  %d NOTES  %s %s",
			roll.kind == L::kSlotEmpty ? "EMPTY" : "PATTERN", len, S::rateNames[view.v[S::TP(t, S::kTRate)]],
			sn, roll.pat.count, L::laneNames[laneSel], L::interpNames[roll.pat.interp[laneSel]]));

		// rows: black keys shaded, C labelled
		float rh = rowH(r);
		const panel::TextStyle KEY(panel::Face::Mono, std::min(6.5f, rh * 1.6f), panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < rollRows; i++) {
			int p = rollLow + i;
			float y = pitchToY(r, p);
			int pc = p % 12;
			bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
			nvgBeginPath(vg);
			nvgRect(vg, r.x0, y, r.x1 - r.x0, rh);
			nvgFillColor(vg, panel::alpha(panel::SAGE, black ? 0.10f : 0.04f));
			nvgFill(vg);
			if (pc == 0)
				panel::text(vg, KEY, r.kx, y + rh * 0.5f, noteName(p));
		}
		// step lines, beats every 4 steps stronger
		for (int s = 0; s <= len; s++) {
			float xs = tickToX(r, s * L::kTicksPerStep);
			nvgBeginPath(vg);
			nvgMoveTo(vg, xs, r.y0);
			nvgLineTo(vg, xs, r.ly1);
			nvgStrokeColor(vg, panel::alpha(panel::RULE, s % 4 == 0 ? 0.55f : 0.22f));
			nvgStrokeWidth(vg, s % 4 == 0 ? 0.6f : 0.4f);
			nvgStroke(vg);
		}
		// notes
		nvgSave(vg);
		nvgScissor(vg, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
		for (int i = 0; i < roll.pat.count; i++) {
			const L::Note& n = roll.pat.notes[i];
			float x0 = tickToX(r, n.start), x1 = std::max(x0 + 1.5f, tickToX(r, n.start + n.len));
			float y0 = pitchToY(r, n.pitch);
			float a = 0.35f + 0.65f * n.vel / 127.f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x0 + 0.3f, y0 + 0.3f, x1 - x0 - 0.6f, rh - 0.6f, 0.8f);
			nvgFillColor(vg, i == selNote ? panel::LIME : panel::alpha(panel::PAPER, a));
			nvgFill(vg);
		}
		nvgRestore(vg);
		// below / above the window
		int below = 0, above = 0;
		for (int i = 0; i < roll.pat.count; i++) {
			below += roll.pat.notes[i].pitch < rollLow;
			above += roll.pat.notes[i].pitch >= rollLow + rollRows;
		}
		const panel::TextStyle EDGE(panel::Face::Mono, 6.f, panel::LIME, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		if (above) panel::text(vg, EDGE, r.x1, r.y0 + 6.f, string::f("%d above", above));
		if (below) panel::text(vg, EDGE, r.x1, r.y1 - 1.f, string::f("%d below", below));
		// playhead
		int ps = snap.patStep[t];
		if (ps >= 0 && snap.active[t] == rollSlot) {
			float xs = tickToX(r, ps * L::kTicksPerStep);
			nvgBeginPath(vg);
			nvgRect(vg, xs, r.y0, std::max(1.f, tickToX(r, (ps + 1) * L::kTicksPerStep) - xs), r.ly1 - r.y0);
			nvgFillColor(vg, panel::alpha(panel::LIME, 0.12f));
			nvgFill(vg);
		}
		// the lane strip: points as bars, the curve between them as a line
		nvgBeginPath(vg);
		nvgRect(vg, r.x0, r.ly0, r.x1 - r.x0, r.ly1 - r.ly0);
		nvgFillColor(vg, panel::alpha(panel::SAGE, 0.06f));
		nvgFill(vg);
		float lmin = (float)L::laneMin(laneSel), lspan = (float)(L::laneMax(laneSel) - L::laneMin(laneSel));
		for (int s = 0; s < len; s++) {
			int16_t lv = roll.pat.lane[laneSel][s];
			if (lv == L::kLaneUnset) continue;
			float f = (lv - lmin) / lspan;
			float xs = tickToX(r, s * L::kTicksPerStep);
			nvgBeginPath(vg);
			nvgRect(vg, xs + 0.5f, r.ly1 - f * (r.ly1 - r.ly0), 1.6f, f * (r.ly1 - r.ly0));
			nvgFillColor(vg, panel::LIME);
			nvgFill(vg);
		}
		nvgBeginPath(vg);
		bool started = false;
		for (int i = 0; i <= 200; i++) {
			float xs = len * i / 200.f;
			float lv = L::laneValueAt(roll.pat, laneSel, xs, len);
			if (lv == (float)L::kLaneUnset) break;
			float px = tickToX(r, xs * L::kTicksPerStep);
			float py = r.ly1 - (lv - lmin) / lspan * (r.ly1 - r.ly0);
			if (!started) { nvgMoveTo(vg, px, py); started = true; }
			else nvgLineTo(vg, px, py);
		}
		nvgStrokeColor(vg, panel::alpha(panel::PAPER, 0.7f));
		nvgStrokeWidth(vg, 0.7f);
		nvgStroke(vg);
		panel::text(vg, KEY.aligned(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE), r.kx, (r.ly0 + r.ly1) * 0.5f,
			string::f("%s", L::laneNames[laneSel]));
	}

	void drawSeq(NVGcontext* vg) {
		SeqRect r = seqRect();
		const panel::TextStyle HEAD(panel::Face::Mono, 7.5f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		int lq = module ? module->launchQ : kLaunchLoop;
		headText(vg, HEAD, pad, pad + 8.f, string::f("SLOTS  launch: %s  ·  click a slot to launch it, a row number for the row", launchNames[lq]));
		const panel::TextStyle CELL(panel::Face::Mono, std::min(7.f, r.rowH * 0.9f), panel::PAPER,
			NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		bool blink = (int)(system::getTime() * 4.0) & 1;
		for (int t = 0; t < S::kNumTracks; t++) {
			float cx = r.x0 + t * r.colW;
			panel::text(vg, CELL.inked(t == snap.sel ? panel::LIME : panel::SAGE), cx + r.colW * 0.5f,
				r.y0 - 5.f, string::f("T%d", t + 1));
		}
		for (int k = 0; k < L::kNumSlots; k++) {
			float y = r.y0 + k * r.rowH;
			panel::text(vg, CELL.inked(panel::SAGE).aligned(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE), pad, y + r.rowH * 0.5f,
				string::f("%d", k + 1));
			for (int t = 0; t < S::kNumTracks; t++) {
				float cx = r.x0 + t * r.colW;
				if (!module) continue;
				const L::Slot& s = module->slots[t][k];		// a glance at small ints; a torn read only mislabels a frame
				bool act = snap.active[t] == k, q = snap.queued[t] == k;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, cx + 1.f, y + 0.6f, r.colW - 2.f, r.rowH - 1.2f, 1.f);
				nvgFillColor(vg, act ? panel::alpha(panel::LIME, 0.35f)
					: s.kind == L::kSlotEmpty ? panel::alpha(panel::SAGE, 0.05f) : panel::alpha(panel::SAGE, 0.16f));
				nvgFill(vg);
				if (q && blink) {
					nvgStrokeColor(vg, panel::LIME);
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
				}
				std::string label = s.kind == L::kSlotGen ? string::f("GEN %03d", act ? (int)view.dtc->tracks[t].activeSeed : s.block[L::kBlockSeed])
					: s.kind == L::kSlotPat ? string::f("PAT %d", s.pat.count) : std::string("·");
				panel::text(vg, CELL.inked(act ? panel::PAPER : s.kind == L::kSlotEmpty ? panel::alpha(panel::SAGE, 0.6f) : panel::SAGE),
					cx + r.colW * 0.5f, y + r.rowH * 0.5f, label);
			}
		}
	}
};

// The glass, and the strip across its top two rows. Each tab is a field the module places
// (a ScreenButton on the param the panel's button used to be: a page, a track, the transport,
// what is done to the selected track); this paints the glass under the page, and each tab's
// name and state, lit the way its LED was.
struct LedgerStrip : widget::TransparentWidget {
	Ledger* module = NULL;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, panel::GLASS);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, panel::RULE);
		nvgStrokeWidth(args.vg, 0.6f);
		nvgStroke(args.vg);
	}

	void tab(NVGcontext* vg, const Rect& f, const char* name, float lit) {
		Rect r = panel::mmRect(f.pos.x, f.pos.y, f.size.x, f.size.y);
		r.pos = r.pos.minus(box.pos);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, r.pos.x + 0.4f, r.pos.y + 0.4f, r.size.x - 0.8f, r.size.y - 0.8f, 1.2f);
		nvgFillColor(vg, lit > 0.02f ? panel::alpha(panel::LIME, 0.10f + 0.32f * std::min(lit, 1.f))
			: panel::alpha(panel::SAGE, 0.10f));
		nvgFill(vg);
		const panel::TextStyle NAME(panel::Face::Mono, std::min(7.f, r.size.y * 0.6f),
			lit > 0.5f ? panel::PAPER : panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		panel::text(vg, NAME, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f + 0.3f, name);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		TransparentWidget::drawLayer(args, layer);
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		auto light = [&](int id) { return module ? module->lights[id].getBrightness() : 0.f; };

		static const Rect* const pages[kNumPages] = { &panel::FIELD_PG_TANK, &panel::FIELD_PG_ROLL,
			&panel::FIELD_PG_FX, &panel::FIELD_PG_SEQ, &panel::FIELD_PG_SONG };
		static const char* const pageNames[kNumPages] = { "TANK", "ROLL", "FX", "SEQ", "SONG" };
		int page = module ? module->page.load() : kPageTank;
		for (int p = 0; p < kNumPages; p++)
			tab(vg, *pages[p], pageNames[p], p == page ? 1.f : 0.f);

		// a track's tab is its old bezel: full on the selected track, a glow while it plays
		static const Rect* const trks[S::kNumTracks] = { &panel::FIELD_TRK1, &panel::FIELD_TRK2,
			&panel::FIELD_TRK3, &panel::FIELD_TRK4, &panel::FIELD_TRK5, &panel::FIELD_TRK6,
			&panel::FIELD_TRK7, &panel::FIELD_TRK8 };
		static const char* const trkNames[S::kNumTracks] = { "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8" };
		for (int t = 0; t < S::kNumTracks; t++)
			tab(vg, *trks[t], trkNames[t], module ? light(Ledger::TRK_LIGHT + t) : (t == 0 ? 1.f : 0.f));

		tab(vg, panel::FIELD_RUN, "RUN", module ? light(Ledger::RUN_LIGHT) : 1.f);
		tab(vg, panel::FIELD_RSET, "RESET", module ? module->params[Ledger::RSET_PARAM].getValue() : 0.f);
		tab(vg, panel::FIELD_FRZE, "FREEZE", light(Ledger::FRZE_LIGHT));
		tab(vg, panel::FIELD_REC, "REC", light(Ledger::REC_LIGHT));
		tab(vg, panel::FIELD_MUTE, "MUTE", light(Ledger::MUTE_LIGHT));
		tab(vg, panel::FIELD_SOLO, "SOLO", light(Ledger::SOLO_LIGHT));
		tab(vg, panel::FIELD_RSED, "RESEED", light(Ledger::RSED_LIGHT));
		tab(vg, panel::FIELD_CAPT, "CAPTURE", light(Ledger::CAPT_LIGHT));
	}
};
