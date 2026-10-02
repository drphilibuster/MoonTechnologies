#pragma once
// Undo for Ledger's own edits, through Rack's history (Edit > Undo, Ctrl/Cmd-Z): a
// pattern edited on the roll, a slot made, copied or cleared on SEQ, a row pasted or
// cleared, an effect chosen, moved, muted or set, and the song. The knobs undo the way
// every Rack parameter does.
//
// An edit reaches the audio thread a moment after it is made, so an action keeps only
// the state from before; the state after is read the first time it is undone, once the
// rings have drained, and is what redo puts back.
#include "LedgerModule.hpp"

namespace ledgerUndo {

static inline Ledger* findLedger(int64_t id) {
	return APP && APP->engine ? dynamic_cast<Ledger*>(APP->engine->getModule(id)) : NULL;
}

// Wait (briefly) until the audio thread has applied everything sent so far.
static inline void drain(Ledger* m) {
	for (int i = 0; i < 200; i++) {
		if (!m->editsInFlight() && m->fxHead.load() == m->fxTail.load() && m->songHead.load() == m->songTail.load())
			return;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

static inline void push(history::Action* a) {
	if (APP && APP->history) APP->history->push(a);
	else delete a;
}

// One slot: its pattern only (`whole` false: the roll's edits), or everything it holds.
struct SlotUndo : history::ModuleAction {
	int track, slot;
	bool whole;
	L::Slot* before;
	L::Slot* after = NULL;

	SlotUndo(Ledger* m, int t, int k, bool whole, const std::string& what) : track(t), slot(k), whole(whole) {
		moduleId = m->id;
		name = "Ledger: " + what;
		before = new L::Slot;
		m->readSlotFull(t, k, *before);
	}
	~SlotUndo() { delete before; delete after; }

	void put(Ledger* m, const L::Slot& s) {
		SlotEdit* e = new SlotEdit;
		e->op = whole ? SlotEdit::PUT_SLOT : SlotEdit::PUT_CONTENT;
		e->track = track;
		e->slot = slot;
		e->data = s;
		m->sendEditWait(*e);
		delete e;
	}
	void undo() override {
		Ledger* m = findLedger(moduleId);
		if (!m) return;
		if (!after) {
			drain(m);
			after = new L::Slot;
			m->readSlotFull(track, slot, *after);
		}
		put(m, *before);
	}
	void redo() override {
		Ledger* m = findLedger(moduleId);
		if (m && after) put(m, *after);
	}
};

// One track's effects: the chain, and every slot's own values and mutes for it.
struct FxUndo : history::ModuleAction {
	int track;
	FxState* before;
	FxState* after = NULL;

	FxUndo(Ledger* m, int t, const std::string& what) : track(t) {
		moduleId = m->id;
		name = "Ledger: " + what;
		before = new FxState();
		m->readFxState(t, *before);
	}
	~FxUndo() { delete before; delete after; }

	// Did anything change since it was made? (A drag that came back where it began did not.)
	bool changed(Ledger* m) {
		drain(m);
		FxState* now = new FxState();
		m->readFxState(track, *now);
		bool c = std::memcmp(now, before, sizeof *now) != 0;
		delete now;
		return c;
	}
	void undo() override {
		Ledger* m = findLedger(moduleId);
		if (!m) return;
		if (!after) {
			drain(m);
			after = new FxState();
			m->readFxState(track, *after);
		}
		m->sendRestore(track, *before);
	}
	void redo() override {
		Ledger* m = findLedger(moduleId);
		if (m && after) m->sendRestore(track, *after);
	}
};

struct SongUndo : history::ModuleAction {
	L::SongEntry before[L::kSongMax], after[L::kSongMax];
	int beforeLen, afterLen = -1;

	SongUndo(Ledger* m, const std::string& what) {
		moduleId = m->id;
		name = "Ledger: " + what;
		std::memcpy(before, m->song.e, sizeof before);
		beforeLen = m->song.len;
	}
	void put(Ledger* m, const L::SongEntry* e, int len) {
		Ledger::SongCmd* c = new Ledger::SongCmd;
		c->op = Ledger::SongCmd::ALL;
		c->at = 0;
		c->len = len;
		std::memcpy(c->all, e, sizeof c->all);
		for (int i = 0; i < 200 && !m->sendSong(*c); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		delete c;
	}
	void undo() override {
		Ledger* m = findLedger(moduleId);
		if (!m) return;
		if (afterLen < 0) {
			drain(m);
			std::memcpy(after, m->song.e, sizeof after);
			afterLen = m->song.len;
		}
		put(m, before, beforeLen);
	}
	void redo() override {
		Ledger* m = findLedger(moduleId);
		if (m && afterLen >= 0) put(m, after, afterLen);
	}
};

}
