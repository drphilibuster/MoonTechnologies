#pragma once
// Ledger's Module: the books, the slots, and the engine they drive. The panel's
// widgets and the display are in Ledger.cpp and LedgerDisplay.hpp.
//
// Ownership.
//  - `base[]` holds every track's settings as set (a knob, the menu, a reseed, a slot
//    launch) and is what the knobs show. The engine's `eng.v[]` holds the same values
//    with CV and the mod matrix added, and is what plays.
//  - Each track has kNumSlots slots: empty, a generator, or a written pattern. The
//    active slot's settings live in base[] while it plays; launching another slot
//    stores them back into the slot and loads the new one's.
//  - Each track's notes run source -> effects chain -> voices (Player.hpp, Effects.hpp,
//    Voices.hpp). A generator with no live effect skips all three: its gate and pitch
//    go to the jacks exactly as Shoal made them, so the golden test's claim holds for it.
//  - MIDI (Midi.hpp): one input, played into the selected track or into tracks by
//    channel and recorded into their patterns; two outputs, Out A and Out B, that each
//    track's notes, the MIDI output effect and Bernoulli's B path can sound on, with
//    clock and transport. Their channels are set per message (Rack's port channel is
//    -1), so one port carries every track.
//  - Rows and the song (Song.hpp): slot k of every track is row k; a row launches at a
//    sync (a track's loop, every loop together, a grid of clocks), and the song plays
//    rows in order, each its number of times.
//  - Slots are written on the audio thread only. The UI edits a copy and sends it
//    through `edits`, a single-producer ring; `slotVersion` tells it when the audio
//    side changed one (a capture, a load) so it can take a fresh copy.
#include "../plugin.hpp"
#include "Shoal.hpp"
#include "Modulation.hpp"
#include "Pattern.hpp"
#include "Player.hpp"
#include "Voices.hpp"
#include "Effects.hpp"
#include "Midi.hpp"
#include "Song.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace S = ledger::shoal;
namespace L = ledger;

// A change to one slot, from the UI.
// PUT_CONTENT replaces what a slot holds (its kind and pattern) and leaves its settings:
// it is how undo puts a pattern back.
struct SlotEdit {
	enum Op { PUT_PATTERN, PUT_SLOT, PUT_CONTENT };
	int op;
	int track, slot;
	L::Slot data;
};

// Launch quantization: when a queued slot takes over.
// (New ones go at the end: a patch saves the index.)
enum LaunchQ { kLaunchNow, kLaunchStep, kLaunchLoop, kLaunchGrid4, kLaunchGrid8, kLaunchGrid16,
			   kLaunchGrid32, kLaunchGrid64, kLaunchModulo, kLaunchShortest, kLaunchLongest, kLaunchTrack,
			   kNumLaunchQ };
static const char* const launchNames[kNumLaunchQ] = {
	"Immediately", "On the track's next step", "At the end of the track's loop",
	"Every 4 clocks", "Every 8 clocks", "Every 16 clocks", "Every 32 clocks", "Every 64 clocks",
	"When every loop ends together (modulo)", "At the end of the shortest loop", "At the end of the longest loop",
	"At the end of one track's loop",
};

enum Page { kPageTank, kPageRoll, kPageFx, kPageSeq, kPageSong, kNumPages };

// A change to a track's effects, from the UI. Types, order and the slots' own values
// change on the audio thread, because changing them can end notes.
struct FxCmd {
	enum Op { SET_TYPE, SWAP, PASTE, SLOT_MUTE, OVERRIDE_SET, OVERRIDE_CLEAR, RESTORE };
	int op, track, fx, other, slot, param, value;
	int16_t params[L::kMaxFxParams];
};

// A track's whole chain, and what each of its slots says about it: what undo puts back.
struct FxState {
	uint8_t type[L::kChainSlots];
	int16_t p[L::kChainSlots][L::kMaxFxParams];
	bool gmute[L::kChainSlots];
	uint8_t fxMute[L::kNumSlots];
	uint8_t nOv[L::kNumSlots];
	L::FxOverride ov[L::kNumSlots][L::kMaxOverrides];
};

struct Ledger : Module {
	// The knobs that address the selected track, in panel order (= ledger::ModDest).
	enum { NUM_POTS = L::kNumDests };
	enum ParamIds {
		POT_PARAM,
		SCALE_PARAM = POT_PARAM + NUM_POTS,
		ROOT_PARAM,
		WEIGHT_PARAM,
		BPM_PARAM,
		TRK_PARAM,
		MUTE_PARAM = TRK_PARAM + S::kNumTracks,
		SOLO_PARAM,
		RSED_PARAM,
		RUN_PARAM,
		FRZE_PARAM,
		RSET_PARAM,
		PAGE_PARAM,
		CAPT_PARAM = PAGE_PARAM + kNumPages,
		REC_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLK_INPUT, RST_INPUT, RUN_INPUT, RESEED_INPUT, FREEZE_INPUT, SEED_INPUT,
		POT_CV_INPUT,							// one per pot, polyphonic by track
		SCALE_CV_INPUT = POT_CV_INPUT + NUM_POTS,
		ROOT_CV_INPUT,
		WEIGHT_CV_INPUT,
		CV_INPUT,								// CV A-D, for the matrix
		NUM_INPUTS = CV_INPUT + L::kNumCvSources
	};
	enum OutputIds {
		PITCH_OUTPUT,
		GATE_OUTPUT = PITCH_OUTPUT + S::kNumTracks,
		VEL_OUTPUT = GATE_OUTPUT + S::kNumTracks,
		MOD_OUTPUT,
		CURRENT_OUTPUT,
		EOS_OUTPUT,
		CLK_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		TRK_LIGHT,
		MUTE_LIGHT = TRK_LIGHT + S::kNumTracks,
		SOLO_LIGHT,
		RSED_LIGHT,
		RUN_LIGHT,
		FRZE_LIGHT,
		PAGE_LIGHT,
		CAPT_LIGHT = PAGE_LIGHT + kNumPages,
		REC_LIGHT,
		NUM_LIGHTS
	};
	// Which of the books' globals are under CV, for the display.
	enum { GMOD_SCALE = 1, GMOD_ROOT = 2, GMOD_WEIGHT = 4 };
	enum { EDIT_RING = 32, RESTORE_RING = 4, SONG_RING = 16 };

	// A change to the song, from the UI: the audio thread owns where the song is.
	struct SongCmd {
		enum Op { INSERT, REMOVE, SET, ALL };
		int op, at;
		L::SongEntry e;
		L::SongEntry all[L::kSongMax];
		int len;
	};

	// The engine's host. A write from the engine (reseed-all scattering seeds) is a
	// write to the books; an external track's step goes to its pattern player.
	struct Host : S::Host {
		Ledger* m = NULL;
		void setParameterFromAudio(S::Shoal*, int p, int16_t value) override { m->setBase(p, value); }
		void sendMidi3(uint32_t, uint8_t, uint8_t, uint8_t) override {}
		void externalStep(S::Shoal*, int t, int pos, uint32_t stepPeriod, bool silent) override {
			m->patternStep(t, pos, stepPeriod, silent);
		}
	};

	// What the display reads: a copy of the engine, published by a seqlock so the
	// UI thread never evaluates a step against half-written state.
	struct Snapshot {
		int16_t v[S::kNumParameters];
		S::Dtc dtc;
		bool solo[S::kNumTracks];
		int sel;
		float gate[S::kNumTracks];
		uint32_t modMask[S::kNumTracks];		// bit d: destination d is under CV
		uint32_t gMod;							// GMOD_*
		int active[S::kNumTracks];
		int queued[S::kNumTracks];
		int patStep[S::kNumTracks];				// the pattern step last played, -1 none
		int rec;								// 0 off, 1 armed (waiting for a note), 2 recording
		int songPos, songLeft;					// the entry playing (-1 none) and its passes left
		bool songOn;
		int trans;								// the transpose leader's semitones
	};

	S::Shoal eng;
	Host host;
	int16_t base[S::kNumParameters];
	float delta[S::kNumParameters] = {};
	uint32_t modMask[S::kNumTracks] = {};
	uint32_t gMod = 0;
	L::MatrixSlot matrix[S::kNumTracks][L::kNumMatrixSlots];
	int outStd[S::kNumTracks] = {};
	int hubMode = L::kHubOff;
	int sel = 0;
	bool freezeLatch = false;
	bool currentsBipolar = false;

	// slots
	L::Slot slots[S::kNumTracks][L::kNumSlots];
	int active[S::kNumTracks] = {};
	std::atomic<int> queued[S::kNumTracks];
	std::atomic<uint32_t> slotVersion[S::kNumTracks][L::kNumSlots];
	int launchQ = kLaunchLoop;
	int launchTrack = 0;						// kLaunchTrack: whose loop
	bool launchRestart = true;					// a launched slot starts at its first step
	uint32_t anchorTick = 0;					// where the loop syncs count from: the last restart or reset
	// the song
	L::Song song;
	std::atomic<bool> songOn{false};
	bool songWas = false;
	bool songPrimed = false;
	SongCmd songCmds[SONG_RING];
	std::atomic<uint32_t> songHead{0}, songTail{0};
	// undo's restores of a chain, in flight
	FxState restores[RESTORE_RING];
	std::atomic<uint32_t> restoreHead{0};
	// each track's event path
	L::PatternSource psrc[S::kNumTracks];
	L::GenSource gsrc[S::kNumTracks];
	L::Chain chains[S::kNumTracks];
	L::Voices voices[S::kNumTracks];
	L::IdSource ids[S::kNumTracks];
	struct SinkCtx { Ledger* m; int t; } sinkCtx[S::kNumTracks];
	bool routed[S::kNumTracks] = {};
	bool extWas[S::kNumTracks] = {};
	bool handover[S::kNumTracks] = {};		// a generator's last note, finishing under the pattern that took over
	FxCmd fxCmds[32];
	std::atomic<uint32_t> fxHead{0}, fxTail{0};
	uint32_t sampleNow = 0;
	uint32_t lastTickAt = 0;
	uint32_t tickSeen = 0;
	int patStep[S::kNumTracks];
	uint32_t patElapsed[S::kNumTracks] = {}, patPeriod[S::kNumTracks] = {};
	uint32_t laneDiv = 0;
	uint32_t lastAdvances[S::kNumTracks] = {};
	uint32_t lastTick = 0;
	bool wasMuted[S::kNumTracks] = {};

	// the UI's edits, in order
	SlotEdit edits[EDIT_RING];
	std::atomic<uint32_t> editHead{0}, editTail{0};

	// display state the panel's page buttons set
	std::atomic<int> page{kPageTank};

	// knob pickup: the value each pot last wrote or was moved to
	int potLast[NUM_POTS] = {};
	bool resyncPots = true;
	int bookLast[4] = { -1, -1, -1, -1 };

	dsp::BooleanTrigger trkTrig[S::kNumTracks], muteTrig, soloTrig, rsedTrig, runTrig, frzeTrig, rsetTrig,
		pageTrig[kNumPages], captTrig;
	dsp::SchmittTrigger runIn, freezeIn;
	int seedInLast = -1;
	std::atomic<int> requestSel{-1};
	std::atomic<bool> requestReseedAll{false};
	std::atomic<bool> requestCapture{false};
	dsp::ClockDivider modDiv, lightDiv, snapDiv;
	float gateNow[S::kNumTracks] = {};
	float captFlash = 0.f;

	Snapshot snap[1];
	std::atomic<uint32_t> snapSeq{0};

	// ---- MIDI ----
	enum { IN_SELECTED, IN_BY_CHANNEL };
	enum { CH_OFF = 0, CH_ANY = 17 };			// a track's input channel: off, 1-16, any
	midi::InputQueue midiIn;
	midi::Output midiOut[L::kMidiPorts];
	midi::Message outMsg, inMsg;				// reused: MIDI on the audio thread allocates nothing
	L::MidiPort mports[L::kMidiPorts];
	std::atomic<bool> outPanic[L::kMidiPorts];
	int inMode = IN_SELECTED;
	bool clockFromMidi = false;
	bool pcLaunches = true;
	int clkOut[L::kMidiPorts] = {};
	L::ClockOut clockOut;
	uint32_t clkTickSeen = 0;
	bool wasRunning = false;
	// per track
	int inCh[S::kNumTracks];
	int outPort[S::kNumTracks];				// -1 none, else 0..kMidiPorts-1
	int outCh[S::kNumTracks];					// 1..16
	L::MidiMap mmap[S::kNumTracks];
	bool followTrans[S::kNumTracks];
	L::MidiTap trackTap[S::kNumTracks];
	int tapCfg[S::kNumTracks];
	L::GenSource gmid[S::kNumTracks];			// a generator's notes for MIDI, when it skips the chain
	L::LiveIn live[S::kNumTracks];
	bool takes[S::kNumTracks] = {};
	uint8_t ccVal[S::kNumTracks][128] = {};
	bool ccSeen[S::kNumTracks][128] = {};
	std::atomic<int> learn{-1};				// track * kNumMatrixSlots + slot waiting for a CC
	// transpose leader
	int transCh = 0;							// 0 off, 1-16: notes on it transpose, not play
	int transMode = 0;							// 0 semitones from C4 (note 60), 1 the books' root
	int transSemis = 0;
	// recording
	std::atomic<bool> recOn{false};
	int recMode = L::kRecOverdub;
	bool punch = false;
	bool punched = false;
	bool recWas = false;
	struct RecNote { uint16_t id; uint8_t note, vel; int tick; uint32_t at; int slot; };
	enum { REC_HELD = 16 };
	RecNote recHeld[S::kNumTracks][REC_HELD];
	int recN[S::kNumTracks] = {};
	bool looping[S::kNumTracks] = {};
	bool loopDone[S::kNumTracks] = {};		// this take's loop is made; another waits for REC again
	int loopSteps[S::kNumTracks] = {};
	int loopAlign[S::kNumTracks];
	int patPos[S::kNumTracks] = {};
	dsp::BooleanTrigger recTrig;

	Ledger() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		auto pct = [&](int id, const char* name, float lo, float def) {
			configParam(id, lo, 100.f, def, name, "%")->snapEnabled = true;
		};
		pct(POT_PARAM + L::kDChance, "Chance", 0.f, 100.f);
		pct(POT_PARAM + L::kDNote, "Note (how often and how far, by sign)", -100.f, 0.f);
		pct(POT_PARAM + L::kDOct, "Octave leaps (how often and how far, by sign)", -100.f, 0.f);
		configParam(POT_PARAM + L::kDLength, 1.f, S::kMaxSteps, 16.f, "Length", " steps")->snapEnabled = true;
		{
			std::vector<std::string> rates(S::rateNames, S::rateNames + S::kNumRates);
			configSwitch(POT_PARAM + L::kDRate, 0.f, S::kNumRates - 1, S::kRateX1, "Rate", rates);
			std::vector<std::string> dirs(S::directionNames, S::directionNames + S::kNumDirections);
			configSwitch(POT_PARAM + L::kDDirection, 0.f, S::kNumDirections - 1, 0.f, "Direction", dirs);
		}
		configParam(POT_PARAM + L::kDTrans, -7.f, 7.f, 0.f, "Transpose", " scale degrees (semitones on a pattern)")->snapEnabled = true;
		configParam(POT_PARAM + L::kDShift, -(S::kMaxSteps - 1), S::kMaxSteps - 1, 0.f, "Shift", " steps")->snapEnabled = true;
		configSwitch(POT_PARAM + L::kDOctave, -3.f, 3.f, 0.f, "Octave", {"-3", "-2", "-1", "0", "+1", "+2", "+3"});
		pct(POT_PARAM + L::kDEvolve, "Evolve", 0.f, 0.f);
		pct(POT_PARAM + L::kDBreathe, "Breathe", 0.f, 0.f);
		pct(POT_PARAM + L::kDGate, "Gate length", 5.f, 50.f);
		pct(POT_PARAM + L::kDTie, "Tie", 0.f, 0.f);
		pct(POT_PARAM + L::kDSlop, "Slop", 0.f, 0.f);
		{
			std::vector<std::string> scales(S::scaleNames, S::scaleNames + S::kNumScales);
			configSwitch(SCALE_PARAM, 0.f, S::kNumScales - 1, 0.f, "Scale", scales);
		}
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root",
			{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		pct(WEIGHT_PARAM, "Weight", 0.f, 0.f);
		configParam(BPM_PARAM, 20.f, 300.f, 120.f, "Tempo", " BPM")->snapEnabled = true;
		for (int t = 0; t < S::kNumTracks; t++)
			configButton(TRK_PARAM + t, string::f("Select track %d", t + 1));
		configButton(MUTE_PARAM, "Mute the selected track");
		configButton(SOLO_PARAM, "Solo the selected track");
		configButton(RSED_PARAM, "Reseed the selected track (lands at its loop origin)");
		configButton(RUN_PARAM, "Run");
		configButton(FRZE_PARAM, "Freeze");
		configButton(RSET_PARAM, "Reset all tracks to step 1");
		configButton(PAGE_PARAM + kPageTank, "Display: the tank (all eight tracks)");
		configButton(PAGE_PARAM + kPageRoll, "Display: the piano roll (the selected track's pattern)");
		configButton(PAGE_PARAM + kPageFx, "Display: the selected track's effects");
		configButton(PAGE_PARAM + kPageSeq, "Display: slots and sequences");
		configButton(PAGE_PARAM + kPageSong, "Display: the song");
		configButton(CAPT_PARAM, "Capture: write the selected generator's loop into the next empty slot, and launch it");
		configButton(REC_PARAM, "Record MIDI in into the playing patterns");

		configInput(CLK_INPUT, "Clock (one pulse = one x1 step; patching it replaces the internal tempo)");
		configInput(RST_INPUT, "Reset");
		configInput(RUN_INPUT, "Run gate (high = running)");
		configInput(RESEED_INPUT, "Reseed all tracks (trigger)");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(SEED_INPUT, "Seed for the selected track (0-10 V = 0-999)");
		for (int d = 0; d < NUM_POTS; d++)
			configInput(POT_CV_INPUT + d, string::f("%s CV (adds; channel n = track n, mono = every track)",
				L::destNames[d]));
		configInput(SCALE_CV_INPUT, "Scale CV (adds, 0-10 V across the scales)");
		configInput(ROOT_CV_INPUT, "Root (1 V/oct, 0 V = C; replaces the knob)");
		configInput(WEIGHT_CV_INPUT, "Weight CV (adds, 0-10 V)");
		for (int i = 0; i < L::kNumCvSources; i++)
			configInput(CV_INPUT + i, string::f("CV %c (a mod matrix source)", 'A' + i));
		for (int t = 0; t < S::kNumTracks; t++) {
			configOutput(PITCH_OUTPUT + t, string::f("Track %d pitch (1 V/oct, 0 V = C3; a channel per voice)", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate (a channel per voice)", t + 1));
		}
		configOutput(VEL_OUTPUT, "Velocity, 0-10 V (channel n = track n)");
		configOutput(MOD_OUTPUT, "MOD 1 lane, 0-10 V (channel n = track n)");
		configOutput(CURRENT_OUTPUT, "Currents (channel n = track n)");
		configOutput(EOS_OUTPUT, "End of sequence (channel n = track n)");
		configOutput(CLK_OUTPUT, "Clock");

		for (int p = 0; p < L::kMidiPorts; p++) {
			midiOut[p].channel = -1;	// each message carries its own channel
			outPanic[p] = false;
		}
		outMsg.setSize(3);
		modDiv.setDivision(4);
		lightDiv.setDivision(64);
		snapDiv.setDivision(512);
		host.m = this;
		eng.host = &host;
		for (int t = 0; t < S::kNumTracks; t++) {
			sinkCtx[t].m = this;
			sinkCtx[t].t = t;
			chains[t].ids = &ids[t];
			chains[t].sink = &Ledger::sinkFn;
			chains[t].sinkCtx = &sinkCtx[t];
			chains[t].ctx.midi = mports;
			chains[t].ctx.midiMap = &mmap[t];
		}
		eng.pulseVolts = 10.f;
		for (int t = 0; t < S::kNumTracks; t++) {
			queued[t] = -1;
			for (int k = 0; k < L::kNumSlots; k++) slotVersion[t][k] = 1;
		}
		initEngine(true);
	}

	~Ledger() {
		panicNow();
	}

	void onRemove(const RemoveEvent& e) override {
		panicNow();
	}

	// ---- the books ---------------------------------------------------------

	// Write the engine's copy of p, telling it as the NT host would.
	void engineSet(int p, int value) {
		if (eng.v[p] == value)
			return;
		eng.v[p] = (int16_t)value;
		S::parameterChanged(&eng, p);
	}

	// Set a value in the books; the engine gets it with its modulation added.
	void setBase(int p, int value) {
		S::ParamRange r = S::paramRange(p);
		base[p] = (int16_t)clamp(value, (int)r.min, (int)r.max);
		engineSet(p, L::applyDelta(p, base[p], delta[p]));
	}

	// The end of track t's chain: its voices.
	static void sinkFn(void* ctx, const L::Ev& e) {
		SinkCtx* c = (SinkCtx*)ctx;
		c->m->voices[c->t].event(e);
		c->m->midiTrack(c->t, e);
	}
	// Into the head of track t's chain.
	void toChain(int t, const L::Ev& e) { chains[t].push(0, e); }

	void bumpSlot(int t, int k) { slotVersion[t][k].fetch_add(1, std::memory_order_release); }

	// Restart the engine. `defaults` resets every value and slot; otherwise base[]
	// and the slots (just loaded) are kept and the engine is rebuilt around them.
	void initEngine(bool defaults) {
		uint32_t sr = (uint32_t)std::max(1.f, APP->engine->getSampleRate());
		if (defaults) {
			for (int p = 0; p < S::kNumParameters; p++)
				base[p] = S::paramRange(p).def;
			for (int t = 0; t < S::kNumTracks; t++) {
				outStd[t] = L::kStdVOct;
				for (int i = 0; i < L::kNumMatrixSlots; i++)
					matrix[t][i] = L::MatrixSlot();
				for (int k = 0; k < L::kNumSlots; k++) {
					slots[t][k].clear();
					bumpSlot(t, k);
				}
				slots[t][0].kind = L::kSlotGen;
				L::storeBlock(slots[t][0].block, base, t);
				active[t] = 0;
				voices[t].init(sr);
				for (int i = 0; i < L::kChainSlots; i++) {
					chains[t].fx[i].setType(L::kFxNone);
					chains[t].fx[i].gmute = false;
					chains[t].muted[i] = false;
				}
			}
			hubMode = L::kHubOff;
			launchQ = kLaunchLoop;
			launchTrack = 0;
			launchRestart = true;
			song.clear();
			songOn = false;
			midiDefaults();
		}
		base[S::kGClockSource] = 1;		// internal until CLK IN is patched
		for (int p = 0; p < S::kNumParameters; p++) {
			delta[p] = 0.f;
			eng.v[p] = base[p];
		}
		for (int t = 0; t < S::kNumTracks; t++) {
			modMask[t] = 0;
			queued[t] = -1;
			patStep[t] = -1;
			patElapsed[t] = patPeriod[t] = 0;
			lastAdvances[t] = 0;
			wasMuted[t] = false;
			voices[t].setSampleRate(sr);
			voices[t].reset();
			psrc[t].reset();
			gsrc[t].reset();
			routed[t] = false;
			extWas[t] = handover[t] = false;
			for (int i = 0; i < L::kChainSlots; i++) chains[t].fx[i].resetState();
		}
		for (int t = 0; t < S::kNumTracks; t++) {
			trackTap[t].flush(mports);
			trackTap[t].reset();
			tapCfg[t] = -1;
			gmid[t].reset();
			int modIn = live[t].modCC;
			live[t] = L::LiveIn();
			live[t].modCC = modIn;
			takes[t] = false;
			recN[t] = 0;
			looping[t] = loopDone[t] = false;
			loopAlign[t] = -1;
			std::memset(ccSeen[t], 0, sizeof ccSeen[t]);
		}
		for (int p = 0; p < L::kMidiPorts; p++) { mports[p].allOff(); }
		clockOut.stop();
		clkTickSeen = 0;
		wasRunning = false;
		transSemis = 0;
		punched = false;
		song.stop();
		songWas = songPrimed = false;
		anchorTick = 0;
		songTail.store(songHead.load());
		sampleNow = lastTickAt = tickSeen = 0;
		fxTail.store(fxHead.load());
		gMod = 0;
		lastTick = 0;
		S::construct(&eng, sr);
		for (int t = 0; t < S::kNumTracks; t++)
			eng.external[t] = slots[t][active[t]].kind != L::kSlotGen;
		for (int p = 0; p < S::kNumParameters; p++)
			S::parameterChanged(&eng, p);
		resyncPots = true;
		for (int i = 0; i < 4; i++) bookLast[i] = -1;
		seedInLast = -1;
	}

	// The MIDI settings a new Ledger starts with. The devices are left as they are.
	void midiDefaults() {
		inMode = IN_SELECTED;
		clockFromMidi = false;
		pcLaunches = true;
		for (int p = 0; p < L::kMidiPorts; p++) clkOut[p] = L::kClkOutOff;
		transCh = 0;
		transMode = 0;
		recMode = L::kRecOverdub;
		punch = false;
		recOn = false;
		for (int t = 0; t < S::kNumTracks; t++) {
			inCh[t] = t + 1;
			outPort[t] = -1;
			outCh[t] = t + 1;
			mmap[t] = L::MidiMap();
			live[t].modCC = 1;
			followTrans[t] = true;
		}
	}

	void onReset() override {
		sel = 0;
		freezeLatch = false;
		currentsBipolar = false;
		page = kPageTank;
		initEngine(true);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		S::setSampleRate(&eng, (uint32_t)std::max(1.f, e.sampleRate));
		for (int t = 0; t < S::kNumTracks; t++)
			voices[t].setSampleRate((uint32_t)std::max(1.f, e.sampleRate));
	}

	// Knobs that address the selected track: a moved knob writes the track; a new
	// selection (or a load, or a launch) moves the knobs. They show the set value,
	// never the modulated one -- that is the display's job.
	void syncPots() {
		if (resyncPots) {
			for (int i = 0; i < NUM_POTS; i++) {
				int v = base[L::destParam(i, sel)];
				params[POT_PARAM + i].setValue((float)v);
				potLast[i] = v;
			}
			resyncPots = false;
			return;
		}
		for (int i = 0; i < NUM_POTS; i++) {
			int v = (int)std::round(params[POT_PARAM + i].getValue());
			if (v != potLast[i]) {
				potLast[i] = v;
				setBase(L::destParam(i, sel), v);
			}
		}
	}

	void syncBooks() {
		int scale = (int)std::round(params[SCALE_PARAM].getValue());
		int root = (int)std::round(params[ROOT_PARAM].getValue());
		int weight = (int)std::round(params[WEIGHT_PARAM].getValue());
		int bpm = (int)std::round(params[BPM_PARAM].getValue());
		if (scale != bookLast[0]) { bookLast[0] = scale; setBase(S::kGScale, scale); }
		if (root != bookLast[1]) { bookLast[1] = root; setBase(S::kGRoot, 48 + root); }	// 0 V = C3
		if (weight != bookLast[2]) { bookLast[2] = weight; setBase(S::kGWeight, weight); }
		if (bpm != bookLast[3]) { bookLast[3] = bpm; setBase(S::kGBPM, bpm); }
	}

	// Recompute every CV's contribution and hand the engine the results.
	void modulate() {
		float cvIn[L::kNumCvSources];
		bool cvInOn[L::kNumCvSources];
		for (int i = 0; i < L::kNumCvSources; i++) {
			cvInOn[i] = inputs[CV_INPUT + i].isConnected();
			cvIn[i] = cvInOn[i] ? inputs[CV_INPUT + i].getVoltage() : 0.f;
		}
		for (int t = 0; t < S::kNumTracks; t++) {
			// the sources: CV A-D, this track's MOD 1-4 lanes and its CCs, as 0..10 V
			float cv[L::kNumMatrixSources];
			bool cvOn[L::kNumMatrixSources];
			cv[L::kSrcCC - 1] = 0.f;
			cvOn[L::kSrcCC - 1] = false;
			for (int i = 0; i < L::kNumCvSources; i++) { cv[i] = cvIn[i]; cvOn[i] = cvInOn[i]; }
			for (int l = 0; l < 4; l++) {
				cvOn[4 + l] = voices[t].laneSet[l];
				cv[4 + l] = 0.f;			// read per slot below: it depends on the slot's polarity
			}
			float d[L::kNumDests] = {};
			float fxd[L::kChainSlots][L::kMaxFxParams] = {};
			uint32_t mask = 0;
			for (int k = 0; k < L::kNumDests; k++) {
				Input& in = inputs[POT_CV_INPUT + k];
				int ch = in.getChannels();
				if (ch == 0 || (ch > 1 && t >= ch))
					continue;
				d[k] += in.getVoltage(ch == 1 ? 0 : t) * L::cvSpan(L::destParam(k, t));
				mask |= 1u << k;
			}
			for (int i = 0; i < L::kNumMatrixSlots; i++) {
				const L::MatrixSlot& s = matrix[t][i];
				if (!s.live() || s.dest < 0)
					continue;
				float volts = cv[s.source - 1];
				bool on = cvOn[s.source - 1];
				if (s.source == L::kSrcCC) {
					// a CC: 64 until it has been heard
					int c = clamp((int)s.cc, 0, 127);
					on = ccSeen[t][c];
					volts = L::midiVolts(on ? ccVal[t][c] : (s.unipolar ? 0 : 64), s.unipolar);
				}
				else if (s.source > L::kNumCvSources) {
					// a MOD lane: middle until it has a value
					int l = s.source - L::kNumCvSources - 1;
					volts = L::midiVolts(on ? voices[t].lane[l] : (s.unipolar ? 0 : 64), s.unipolar);
				}
				if (L::destIsFx(s.dest)) {
					int fi = L::destFxSlot(s.dest), pi = L::destFxParam(s.dest);
					if (fi >= L::kChainSlots) continue;
					const L::Fx& f = chains[t].fx[fi];
					if (pi >= f.desc().nParams) continue;
					const L::ParamDesc& pd = f.desc().p[pi];
					fxd[fi][pi] += L::slotDeltaSpan(s, (float)(pd.max - pd.min), volts);
					continue;
				}
				if (s.dest >= L::kNumDests) continue;
				d[s.dest] += L::slotDelta(s, t, volts);
				if (on || s.offset != 0)
					mask |= 1u << s.dest;
			}
			// the effects as they play: the track's values, this slot's own, CV on top
			const L::Slot& sl = slots[t][active[t]];
			for (int i = 0; i < L::kChainSlots; i++) {
				L::Fx& f = chains[t].fx[i];
				for (int k = 0; k < f.desc().nParams; k++) {
					int o = sl.findOverride(i, k);
					int v = o >= 0 ? sl.ov[o].value : f.p[k];
					const L::ParamDesc& pd = f.desc().p[k];
					v += (int)lroundf(fxd[i][k]);
					f.pe[k] = (int16_t)clamp(v, (int)pd.min, (int)pd.max);
				}
				chains[t].setMuted(i, f.gmute || ((sl.fxMute >> i) & 1));
			}
			modMask[t] = mask;
			for (int k = 0; k < L::kNumDests; k++) {
				int p = L::destParam(k, t);
				delta[p] = d[k];
				engineSet(p, L::applyDelta(p, base[p], d[k]));
			}
		}

		uint32_t g = 0;
		float ds = 0.f, dw = 0.f;
		if (inputs[SCALE_CV_INPUT].isConnected()) { ds = inputs[SCALE_CV_INPUT].getVoltage() * L::cvSpan(S::kGScale); g |= GMOD_SCALE; }
		if (inputs[WEIGHT_CV_INPUT].isConnected()) { dw = inputs[WEIGHT_CV_INPUT].getVoltage() * L::cvSpan(S::kGWeight); g |= GMOD_WEIGHT; }
		delta[S::kGScale] = ds;
		delta[S::kGWeight] = dw;
		engineSet(S::kGScale, L::applyDelta(S::kGScale, base[S::kGScale], ds));
		engineSet(S::kGWeight, L::applyDelta(S::kGWeight, base[S::kGWeight], dw));
		if (inputs[ROOT_CV_INPUT].isConnected()) {
			g |= GMOD_ROOT;
			engineSet(S::kGRoot, 48 + L::rootFromVolts(inputs[ROOT_CV_INPUT].getVoltage()));
		}
		else
			engineSet(S::kGRoot, base[S::kGRoot]);
		gMod = g;
	}

	void select(int t) {
		if (t == sel || t < 0 || t >= S::kNumTracks)
			return;
		sel = t;
		resyncPots = true;
		seedInLast = -1;
	}

	void reseedTrack(int t) {
		setBase(S::TP(t, S::kTSeed), (int)(random::u32() % 1000));
	}

	void reseedAll() {
		int b = (int)(random::u32() % 1000);
		if (b == eng.dtc->lastReseedAll)
			b = (b + 1) % 1000;			// an unchanged value would be ignored
		setBase(S::kGReseedAll, b);
	}

	// ---- slots -------------------------------------------------------------

	// Make slot k track t's active slot now: the outgoing slot keeps the settings it
	// was playing with, the incoming one's are loaded.
	void launch(int t, int k) {
		if (k < 0 || k >= L::kNumSlots)
			return;
		// Restart: unless the track has just played its loop's last step and the new slot
		// is as long (then it goes on to step 1 by itself, walk and all), it starts over.
		S::TrackState& tr = eng.dtc->tracks[t];
		int outLen = eng.v[S::TP(t, S::kTLength)];
		bool atEnd = outLen <= 1 || tr.eosCount == 0;		// eosCount: steps played of this loop
		int inLen = slots[t][k].kind != L::kSlotEmpty ? slots[t][k].block[L::kBlockLength] : base[S::TP(t, S::kTLength)];
		bool restart = launchRestart && !(atEnd && inLen == base[S::TP(t, S::kTLength)]);
		L::Slot& out = slots[t][active[t]];
		if (out.kind != L::kSlotEmpty)
			L::storeBlock(out.block, base, t);
		active[t] = k;
		L::Slot& in = slots[t][k];
		if (in.kind != L::kSlotEmpty)
			for (int i = 0; i < L::kNumBlock; i++)
				setBase(L::blockParam(i, t), in.block[i]);
		eng.external[t] = in.kind != L::kSlotGen;
		psrc[t].releaseAll();
		patStep[t] = -1;
		if (restart) {
			// what Shoal's reset does, for this track alone: its next step is its walk's first
			tr.pos = -1;
			tr.extraPending = 0;
			tr.slopCountdown = 0;
			tr.tieHeld = false;
			tr.eosCount = 0;
			anchorTick = eng.dtc->tickCount;
		}
		if (t == sel)
			resyncPots = true;
	}

	// Track t's loop, in master ticks, as the loop syncs count it.
	uint64_t loopTicksOf(int t) const {
		int rate = clamp((int)eng.v[S::TP(t, S::kTRate)], 0, S::kNumRates - 1);
		return L::loopTicks(eng.v[S::TP(t, S::kTLength)], S::rateMult[rate], S::rateDiv[rate]);
	}

	// The period, in ticks, a row launch waits on under `mode`. The song treats the
	// per-track modes (now, step, loop) as modulo.
	uint64_t syncPeriod(int mode) const {
		switch (mode) {
			case kLaunchGrid4: return 4;
			case kLaunchGrid8: return 8;
			case kLaunchGrid16: return 16;
			case kLaunchGrid32: return 32;
			case kLaunchGrid64: return 64;
			default: break;
		}
		uint64_t ticks[S::kNumTracks];
		bool used[S::kNumTracks];
		for (int t = 0; t < S::kNumTracks; t++) {
			ticks[t] = loopTicksOf(t);
			used[t] = slots[t][active[t]].kind != L::kSlotEmpty;
		}
		int sync = mode == kLaunchShortest ? L::kRowShortest : mode == kLaunchLongest ? L::kRowLongest
			: mode == kLaunchTrack ? L::kRowTrack : L::kRowModulo;
		return L::rowPeriod(sync, ticks, used, S::kNumTracks, launchTrack);
	}

	// Whether `tick` (the next tick to play) starts a period of `mode`. Grids count from
	// the clock's own start; the loop syncs from where the playing row started.
	bool syncAt(int mode, uint32_t tick) const {
		uint64_t p = syncPeriod(mode);
		if (!p) return false;
		bool grid = mode >= kLaunchGrid4 && mode <= kLaunchGrid64;
		uint32_t from = grid ? 0u : anchorTick;
		return (uint64_t)(uint32_t)(tick - from) % p == 0;
	}

	// Launch row k now, on every track.
	void launchRow(int k) {
		for (int t = 0; t < S::kNumTracks; t++) {
			launch(t, k);
			queued[t] = -1;
		}
	}

	// UI thread: send a change to the song. False if the ring is full.
	bool sendSong(const SongCmd& c) {
		uint32_t head = songHead.load(std::memory_order_relaxed);
		if (head - songTail.load(std::memory_order_acquire) >= SONG_RING)
			return false;
		songCmds[head % SONG_RING] = c;
		songHead.store(head + 1, std::memory_order_release);
		return true;
	}

	void applySongCmds() {
		uint32_t tail = songTail.load(std::memory_order_relaxed);
		while (tail != songHead.load(std::memory_order_acquire)) {
			const SongCmd& c = songCmds[tail % SONG_RING];
			L::SongEntry e = c.e;
			e.row = (int8_t)clamp((int)e.row, 0, L::kNumSlots - 1);
			e.times = (uint8_t)clamp((int)e.times, 1, 16);
			switch (c.op) {
				case SongCmd::INSERT: song.insert(c.at, e); break;
				case SongCmd::REMOVE: song.remove(c.at); break;
				case SongCmd::SET: if (c.at >= 0 && c.at < song.len) song.e[c.at] = e; break;
				case SongCmd::ALL:
					song.len = clamp(c.len, 0, (int)L::kSongMax);
					for (int i = 0; i < L::kSongMax; i++) song.e[i] = c.all[i];
					if (song.pos >= song.len) song.pos = song.len ? 0 : -1;
					break;
			}
			tail++;
			songTail.store(tail, std::memory_order_release);
		}
	}

	// The song, once per frame after the launches: when its row's period comes round it
	// counts a pass, and when the entry is done the next one's row takes over.
	void serviceSong(bool newTick, uint32_t tick) {
		bool on = songOn.load();
		if (on != songWas) {
			song.stop();
			songWas = on;
		}
		if (!on || song.len <= 0) return;
		bool running = eng.v[S::kGRun];
		// started while stopped: the first row is there at once
		if (song.pos < 0 && !running) {
			int r = song.start();
			if (r >= 0) launchRow(r);
			return;
		}
		if (!newTick) return;
		int mode = launchQ;
		if (mode == kLaunchNow || mode == kLaunchStep || mode == kLaunchLoop) mode = kLaunchModulo;
		if (!syncAt(mode, tick)) return;
		int r = song.boundary();
		if (r >= 0) launchRow(r);		// a track that restarts sets the anchor
	}

	// Launch every queued slot whose moment has come. Called after each engine frame.
	void serviceLaunches() {
		uint32_t tick = eng.dtc->tickCount;
		bool newTick = tick != lastTick;
		lastTick = tick;
		bool rowSync = launchQ >= kLaunchModulo;
		bool rowGo = rowSync && newTick && syncAt(launchQ, tick);
		int grid = 0;
		switch (launchQ) {
			case kLaunchGrid4: grid = 4; break;
			case kLaunchGrid8: grid = 8; break;
			case kLaunchGrid16: grid = 16; break;
			case kLaunchGrid32: grid = 32; break;
			case kLaunchGrid64: grid = 64; break;
			default: break;
		}
		for (int t = 0; t < S::kNumTracks; t++) {
			const S::TrackState& tr = eng.dtc->tracks[t];
			bool advanced = tr.advances != lastAdvances[t];
			lastAdvances[t] = tr.advances;
			int q = queued[t].load();
			if (q < 0)
				continue;
			bool go;
			if (launchQ == kLaunchNow)
				go = true;
			else if (launchQ == kLaunchStep)
				go = advanced;
			else if (launchQ == kLaunchLoop) {
				// the step that just played was the loop's last: the next one is new. (eosCount
				// counts the steps played of the loop, so it is back at 0 just after the last.)
				go = advanced && tr.eosCount == 0;
			}
			else if (rowSync)
				go = rowGo;
			else
				go = newTick && grid > 0 && tick % (uint32_t)grid == 0;	// the next tick starts a bar
			if (!eng.v[S::kGRun])
				go = true;								// stopped: there is no moment to wait for
			if (go) {
				launch(t, q);
				queued[t].compare_exchange_strong(q, -1);
			}
		}
		serviceSong(newTick, tick);
	}

	// Queue a whole row: every track to slot k.
	void queueSequence(int k) {
		for (int t = 0; t < S::kNumTracks; t++)
			queued[t] = k;
	}

	// CAPTURE: the selected generator's loop, written into the next empty slot and
	// queued to launch.
	void captureSelected() {
		int t = sel;
		if (slots[t][active[t]].kind != L::kSlotGen)
			return;
		int k = -1;
		for (int i = 1; i <= L::kNumSlots && k < 0; i++) {
			int c = (active[t] + i) % L::kNumSlots;
			if (slots[t][c].kind == L::kSlotEmpty)
				k = c;
		}
		if (k < 0)
			return;
		L::Slot& s = slots[t][k];
		L::capture(&eng, t, (uint8_t)eng.v[S::kGMidiVelocity], s.pat);
		L::storeBlock(s.block, base, t);
		L::captureBlock(s.block);
		s.kind = L::kSlotPat;
		bumpSlot(t, k);
		queued[t] = k;
		captFlash = 1.f;
	}

	// The UI's edits, applied in the order they were made.
	void applyEdits() {
		uint32_t tail = editTail.load(std::memory_order_relaxed);
		while (tail != editHead.load(std::memory_order_acquire)) {
			const SlotEdit& e = edits[tail % EDIT_RING];
			if (e.track >= 0 && e.track < S::kNumTracks && e.slot >= 0 && e.slot < L::kNumSlots) {
				L::Slot& s = slots[e.track][e.slot];
				bool isActive = e.slot == active[e.track];
				if (e.op == SlotEdit::PUT_CONTENT) {
					bool wasEmpty = s.kind == L::kSlotEmpty;
					s.kind = e.data.kind;
					s.pat = e.data.pat;
					if (wasEmpty && s.kind != L::kSlotEmpty) {
						if (isActive) L::storeBlock(s.block, base, e.track);
						else std::memcpy(s.block, e.data.block, sizeof s.block);
					}
				}
				else if (e.op == SlotEdit::PUT_PATTERN) {
					s.pat = e.data.pat;
					if (s.kind == L::kSlotEmpty) {
						s.kind = L::kSlotPat;
						if (isActive) L::storeBlock(s.block, base, e.track);
					}
				}
				else {
					s = e.data;
					if (isActive && s.kind != L::kSlotEmpty)
						for (int i = 0; i < L::kNumBlock; i++)
							setBase(L::blockParam(i, e.track), s.block[i]);
					if (isActive && e.track == sel)
						resyncPots = true;
				}
				if (isActive) {
					bool ext = s.kind != L::kSlotGen;
					if (ext != eng.external[e.track]) {
						eng.external[e.track] = ext;
						psrc[e.track].releaseAll();
					}
				}
				bumpSlot(e.track, e.slot);
			}
			tail++;
			editTail.store(tail, std::memory_order_release);
		}
	}

	// UI thread: send an edit. False if the ring is full; try again next frame.
	bool sendEdit(const SlotEdit& e) {
		uint32_t head = editHead.load(std::memory_order_relaxed);
		if (head - editTail.load(std::memory_order_acquire) >= EDIT_RING)
			return false;
		edits[head % EDIT_RING] = e;
		editHead.store(head + 1, std::memory_order_release);
		return true;
	}
	bool editsInFlight() const { return editHead.load() != editTail.load(); }

	// An external track reached a step: play the pattern's notes for it.
	void patternStep(int t, int pos, uint32_t stepPeriod, bool silent) {
		L::Slot& s = slots[t][active[t]];
		patPos[t] = pos;
		// the looper: a step more of the loop being made, which at the most a pattern holds is done
		if (looping[t] && ++loopSteps[t] >= S::kMaxSteps) {
			looping[t] = false;
			loopDone[t] = true;
			loopAlign[t] = 0;
		}
		int len = eng.v[S::TP(t, S::kTLength)];
		// a loop just made: Shift so that this step is the one the loop says comes next
		if (loopAlign[t] >= 0 && len > 0) {
			int want = loopAlign[t] % len;
			setBase(S::XP(t, S::kXShift), ((want - pos) % len + len) % len);
			loopAlign[t] = -1;
		}
		int shift = eng.v[S::XP(t, S::kXShift)];
		int st = len > 0 ? ((pos + shift) % len + len) % len : 0;
		patStep[t] = st;
		patElapsed[t] = 0;
		patPeriod[t] = stepPeriod;
		if (s.kind != L::kSlotPat)
			return;
		// replacing: the step the playhead reaches is cleared before it plays
		if (recMode == L::kRecReplace && takes[t] && recording()) {
			int a = st * L::kTicksPerStep, b = a + L::kTicksPerStep;
			bool any = false;
			for (int i = s.pat.count - 1; i >= 0; i--)
				if (s.pat.notes[i].start >= a && s.pat.notes[i].start < b) { s.pat.remove(i); any = true; }
			if (any) bumpSlot(t, active[t]);
		}
		const S::TrackState& tr = eng.dtc->tracks[t];
		int transpose = eng.v[S::TP(t, S::kTTrans)] + 12 * eng.v[S::TP(t, S::kTOctave)] + transFor(t);
		psrc[t].step(s.pat, st, stepPeriod, silent, eng.v[S::TP(t, S::kTChance)],
			tr.activeSeed, tr.pass, transpose, ids[t]);
	}

	// ---- MIDI --------------------------------------------------------------

	int transFor(int t) const { return followTrans[t] ? transSemis : 0; }

	bool trackMuted(int t) const {
		return eng.v[S::TP(t, S::kTMute)] || (S::anySoloOf(&eng) && !eng.solo[t]);
	}

	// Recording is happening: REC is on, a punch-in has had its note, and the clock runs.
	bool recording() const {
		return recOn.load() && (!punch || punched) && eng.v[S::kGRun];
	}

	// Which tracks hear the input: the selected one, or each by its channel.
	void updateTakes() {
		bool dev = midiIn.getDeviceId() >= 0;
		for (int t = 0; t < S::kNumTracks; t++) {
			bool tk = dev && (inMode == IN_SELECTED ? t == sel : inCh[t] != CH_OFF);
			if (!tk && takes[t])
				live[t].flush([&](const L::Ev& e) { liveEvent(t, e); });
			takes[t] = tk;
		}
	}

	void handleMidi(const midi::Message& msg) {
		int n = msg.getSize();
		if (n < 1) return;
		uint8_t b[3] = { msg.bytes[0], (uint8_t)(n > 1 ? msg.bytes[1] : 0), (uint8_t)(n > 2 ? msg.bytes[2] : 0) };
		if (b[0] >= 0xF8) {
			if (clockFromMidi) S::midiRealtime(&eng, b[0]);
			return;
		}
		if (b[0] < 0x80 || b[0] >= 0xF0) return;
		int st = b[0] & 0xF0, ch = b[0] & 15;
		if (st == 0xC0) {
			if (pcLaunches) queueSequence(b[1] % L::kNumSlots);
			return;
		}
		// the transpose leader's channel moves the tracks rather than playing in them
		if (transCh && ch == transCh - 1 && (st == 0x90 || st == 0x80)) {
			if (st == 0x90 && b[2] > 0) transposeTo(b[1]);
			return;
		}
		for (int t = 0; t < S::kNumTracks; t++) {
			if (!takes[t]) continue;
			if (inMode == IN_BY_CHANNEL && inCh[t] != CH_ANY && inCh[t] != ch + 1) continue;
			playLive(t, b);
		}
	}

	void transposeTo(int note) {
		if (transMode == 1) {
			int pc = note % 12;
			params[ROOT_PARAM].setValue((float)pc);
			bookLast[1] = pc;
			setBase(S::kGRoot, 48 + pc);
		}
		else
			transSemis = clamp(note - 60, -48, 48);
	}

	// A channel message for track t.
	void playLive(int t, const uint8_t* b) {
		if ((b[0] & 0xF0) == 0xB0 && b[1] != 64) {
			// every CC is a matrix source; one waiting to be learned is this one
			ccVal[t][b[1]] = b[2];
			ccSeen[t][b[1]] = true;
			int l = learn.load();
			if (l >= 0 && l / L::kNumMatrixSlots == t && b[1] < 120) {
				L::MatrixSlot& ms = matrix[t][l % L::kNumMatrixSlots];
				ms.source = L::kSrcCC;
				ms.cc = (int8_t)b[1];
				learn.compare_exchange_strong(l, -1);
				return;
			}
			if (b[1] != live[t].modCC) return;
		}
		live[t].message(b, ids[t], [&](const L::Ev& e) { liveEvent(t, e); });
	}

	// A live event: written down if recording, then played.
	void liveEvent(int t, const L::Ev& e) {
		record(t, e);
		toChain(t, e);
	}

	// ---- recording ----

	void record(int t, const L::Ev& e) {
		if (e.type == L::kEvNoteOff) {
			finishRec(t, e.id);
			return;
		}
		if (e.type == L::kEvNoteOn && recOn.load() && punch && !punched)
			punched = true;
		if (!recording())
			return;
		int k = active[t];
		L::Slot& s = slots[t][k];
		if (s.kind == L::kSlotGen)
			return;					// a generator is grown, not written: CAPTURE it first
		if (e.type == L::kEvNoteOn) {
			if (s.kind == L::kSlotEmpty) {
				s.pat.clear();
				L::storeBlock(s.block, base, t);
				s.kind = L::kSlotPat;
				bumpSlot(t, k);
			}
			if (recMode == L::kRecLooper && !looping[t] && !loopDone[t])
				startLoop(t);
			if (recN[t] >= REC_HELD)
				return;
			RecNote& r = recHeld[t][recN[t]++];
			r.id = e.id;
			r.note = e.note;
			r.vel = e.vel;
			r.tick = recPos(t);
			r.at = sampleNow;
			r.slot = k;
			return;
		}
		// a lane's value: its point at this step
		if (e.type == L::kEvMod && s.kind == L::kSlotPat && e.lane < L::kNumLanes) {
			int st = looping[t] ? loopSteps[t] : patStep[t];
			if (st < 0 || st >= S::kMaxSteps) return;
			s.pat.lane[e.lane][st] = (int16_t)clamp((int)e.value, L::laneMin(e.lane), L::laneMax(e.lane));
			bumpSlot(t, k);
		}
	}

	// Where in the pattern, in ticks, a note played now lands.
	int recPos(int t) const {
		int st = looping[t] ? loopSteps[t] : std::max(0, patStep[t]);
		return L::recTick(st, patElapsed[t], patPeriod[t], L::kTicksPerStep);
	}

	// A recorded note let go: it goes into the pattern it started in.
	void finishRec(int t, uint16_t id) {
		for (int i = 0; i < recN[t]; i++) {
			if (recHeld[t][i].id != id) continue;
			RecNote r = recHeld[t][i];
			recHeld[t][i] = recHeld[t][--recN[t]];
			L::Slot& s = slots[t][r.slot];
			if (s.kind != L::kSlotPat) return;
			L::Note n;
			n.start = (uint16_t)r.tick;
			n.len = (uint16_t)L::recLen(sampleNow - r.at, patPeriod[t] ? patPeriod[t] : 1, L::kTicksPerStep,
				S::kMaxSteps * L::kTicksPerStep);
			n.pitch = r.note;
			n.vel = r.vel;
			s.pat.add(n);
			bumpSlot(t, r.slot);
			return;
		}
	}

	// The looper's first note: the pattern starts again, as long as a pattern can be,
	// with this step its first.
	void startLoop(int t) {
		L::Slot& s = slots[t][active[t]];
		s.pat.clear();
		bumpSlot(t, active[t]);
		looping[t] = true;
		loopSteps[t] = 0;
		setBase(S::TP(t, S::kTLength), S::kMaxSteps);
		setBase(S::XP(t, S::kXShift), ((-patPos[t]) % S::kMaxSteps + S::kMaxSteps) % S::kMaxSteps);
	}

	// REC let go: a loop being made takes its length, to the nearest step.
	void endLoop(int t) {
		float frac = patPeriod[t] ? std::min(1.f, (float)patElapsed[t] / patPeriod[t]) : 0.f;
		int len = L::looperLength(loopSteps[t], frac, S::kMaxSteps);
		setBase(S::TP(t, S::kTLength), len);
		slots[t][active[t]].pat.truncate(len);
		bumpSlot(t, active[t]);
		loopAlign[t] = (loopSteps[t] + 1) % len;	// the step after this one
		looping[t] = false;
		loopDone[t] = true;
	}

	void recChanged(bool on) {
		punched = false;
		for (int t = 0; t < S::kNumTracks; t++) {
			if (!on && looping[t]) endLoop(t);
			loopDone[t] = false;
		}
	}

	// ---- out ----

	// An event leaving track t's chain: to its MIDI port, if it has one.
	void midiTrack(int t, const L::Ev& e) {
		if (outPort[t] < 0) return;
		if (e.type == L::kEvNoteOn && trackMuted(t)) return;
		trackTap[t].event(mports, outPort[t], outCh[t] - 1, e, &mmap[t]);
	}

	void sendPort(int p) {
		L::MidiPort& mp = mports[p];
		for (int i = 0; i < mp.n; i++) {
			const L::MidiMsg& m = mp.q[i];
			outMsg.setSize(m.n);
			for (int k = 0; k < m.n; k++) outMsg.bytes[k] = m.b[k];
			midiOut[p].sendMessage(outMsg);
		}
		mp.n = 0;
	}

	// End every note on both ports now (the module is going, or a panic).
	void panicNow() {
		for (int p = 0; p < L::kMidiPorts; p++) {
			mports[p].allOff();
			sendPort(p);
		}
	}

	// UI thread: end what a port has sounding before its device changes. The audio
	// thread does it; this waits for it, or gives up if the engine is not running.
	void portFlushWait(int p) {
		outPanic[p] = true;
		for (int i = 0; i < 50 && outPanic[p].load(); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		outPanic[p] = false;
	}

	// ---- process -----------------------------------------------------------

	void process(const ProcessArgs& args) override {
		applyEdits();
		applyFxCmds();
		applySongCmds();
		int req = requestSel.exchange(-1);
		if (req >= 0) select(req);
		if (requestReseedAll.exchange(false)) reseedAll();

		for (int t = 0; t < S::kNumTracks; t++)
			if (trkTrig[t].process(params[TRK_PARAM + t].getValue() > 0.f))
				select(t);
		for (int p = 0; p < kNumPages; p++)
			if (pageTrig[p].process(params[PAGE_PARAM + p].getValue() > 0.f))
				page = p;
		if (captTrig.process(params[CAPT_PARAM].getValue() > 0.f) || requestCapture.exchange(false))
			captureSelected();
		if (recTrig.process(params[REC_PARAM].getValue() > 0.f))
			recOn = !recOn.load();
		{
			bool r = recOn.load();
			if (r != recWas) recChanged(r);
			recWas = r;
		}
		// the ports: which have a device, and any that must end what they hold
		for (int p = 0; p < L::kMidiPorts; p++) {
			bool lv = midiOut[p].getDeviceId() >= 0;
			if (!lv && mports[p].live) std::memset(mports[p].count, 0, sizeof mports[p].count);
			mports[p].live = lv;
			if (outPanic[p].load()) {
				mports[p].allOff();
				sendPort(p);
				outPanic[p] = false;
			}
		}
		updateTakes();
		while (midiIn.tryPop(&inMsg, args.frame))
			handleMidi(inMsg);
		syncPots();
		syncBooks();
		if (modDiv.process())
			modulate();

		if (muteTrig.process(params[MUTE_PARAM].getValue() > 0.f))
			setBase(S::TP(sel, S::kTMute), !base[S::TP(sel, S::kTMute)]);
		if (soloTrig.process(params[SOLO_PARAM].getValue() > 0.f)) {
			// one soloist: soloing another track moves it, soloing it again clears
			bool was = eng.solo[sel];
			for (int t = 0; t < S::kNumTracks; t++) eng.solo[t] = false;
			eng.solo[sel] = !was;
		}
		if (rsedTrig.process(params[RSED_PARAM].getValue() > 0.f))
			reseedTrack(sel);
		bool resetNow = rsetTrig.process(params[RSET_PARAM].getValue() > 0.f);
		if (resetNow)
			eng.dtc->resetPrime = true;
		if (frzeTrig.process(params[FRZE_PARAM].getValue() > 0.f))
			freezeLatch = !freezeLatch;

		// Run: the button toggles; a patched RUN gate is the transport instead.
		if (inputs[RUN_INPUT].isConnected()) {
			runIn.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
			setBase(S::kGRun, runIn.isHigh());
		}
		else if (runTrig.process(params[RUN_PARAM].getValue() > 0.f))
			setBase(S::kGRun, !base[S::kGRun]);

		bool frz = freezeLatch;
		if (inputs[FREEZE_INPUT].isConnected()) {
			freezeIn.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
			frz = frz || freezeIn.isHigh();
		}
		setBase(S::kGFreeze, frz);

		if (inputs[SEED_INPUT].isConnected()) {
			int s = clamp((int)std::round(inputs[SEED_INPUT].getVoltage() * 99.9f), 0, 999);
			if (s != seedInLast) {
				seedInLast = s;
				setBase(S::TP(sel, S::kTSeed), s);
			}
		}
		else
			seedInLast = -1;

		setBase(S::kGClockSource, inputs[CLK_INPUT].isConnected() ? 0 : clockFromMidi ? 2 : 1);

		// One frame of the engine.
		bool primed = eng.dtc->resetPrime;
		float clkIn = inputs[CLK_INPUT].getVoltage();
		float rstIn = inputs[RST_INPUT].getVoltage();
		float rsdIn = inputs[RESEED_INPUT].getVoltage();
		float clkOut = 0.f, pitch[S::kNumTracks], gate[S::kNumTracks], cur[S::kNumTracks], eos[S::kNumTracks];
		S::Io io;
		io.clockIn = &clkIn;
		io.resetIn = &rstIn;
		io.reseedIn = &rsdIn;
		io.clkOut = &clkOut;
		for (int t = 0; t < S::kNumTracks; t++) {
			io.pitchOut[t] = &pitch[t];
			io.gateOut[t] = &gate[t];
			io.currentOut[t] = &cur[t];
			io.eosOut[t] = &eos[t];
		}
		bool wasPrimed = primed || eng.dtc->resetPrime;
		// a reset with the song on: its first row is there for the reset's own tick
		if (eng.dtc->resetPrime && songOn.load() && !songPrimed) {
			int r = song.start();
			if (r >= 0) launchRow(r);
			songWas = true;
			songPrimed = true;
		}
		S::step(&eng, io, 1);
		if (!eng.dtc->resetPrime) songPrimed = false;
		if (wasPrimed && !eng.dtc->resetPrime) anchorTick = 0;	// the reset's tick is every loop's start
		if (wasPrimed && !eng.dtc->resetPrime) {
			// a reset landed this frame: patterns restart, and the effects start over
			for (int t = 0; t < S::kNumTracks; t++) {
				if (eng.external[t]) psrc[t].releaseAll();
				chains[t].flushAll();
			}
		}
		serviceLaunches();
		trackBeat();
		midiClock(wasPrimed && !eng.dtc->resetPrime);

		bool frozen = eng.v[S::kGFreeze];
		bool anySolo = S::anySoloOf(&eng);
		bool laneTick = (laneDiv++ & 31) == 0;
		// Each track's voices: Shoal's own gate and pitch for a generator with no live effect;
		// otherwise source -> effects -> voices.
		float vp[S::kNumTracks][L::kMaxVoices], vg[S::kNumTracks][L::kMaxVoices];
		int nv[S::kNumTracks];
		for (int t = 0; t < S::kNumTracks; t++) {
			float scale = eng.v[S::XP(t, S::kXPitchScale)] * 0.01f;
			float offset = eng.v[S::XP(t, S::kXPitchOffset)] * 0.1f;
			float level = (float)eng.v[S::XP(t, S::kXGateVolts)];
			bool ext = eng.external[t];
			bool r = ext || chains[t].active() || takes[t];
			int ts = transFor(t);
			int genNote = (int)lroundf(eng.dtc->tracks[t].pitchVolts * 12.f) + 48 + ts;
			// the generator source feeds the chain only while a generator is routed through
			// it; when it stops (the track became a pattern, or lost its effects), the note
			// it had sounding ends here
			// ...except when a pattern takes over from it mid-note: that note is the voices'
			// now, and ends when Shoal ends it -- a tie stays legato into the pattern's first
			if (ext && !extWas[t] && gate[t] > 0.f)
				handover[t] = true;
			if (!ext) handover[t] = false;
			extWas[t] = ext;
			if (gsrc[t].high && !handover[t] && (ext || !r)) {
				toChain(t, L::Ev::off(gsrc[t].id));
				gsrc[t].reset();
			}
			if (r != routed[t]) {
				if (!r) { chains[t].flushAll(); voices[t].releaseAll(); }
				routed[t] = r;
			}
			// MIDI out: a change of port, channel or path ends what the track had sounding
			int cfg = outPort[t] < 0 ? -1 : (outPort[t] * 16 + outCh[t] - 1 + (r ? 256 : 0));
			if (cfg != tapCfg[t]) {
				trackTap[t].flush(mports);
				gmid[t].reset();
				tapCfg[t] = cfg;
			}
			if (!r) {
				nv[t] = 1;
				vp[t][0] = L::applyOutStd(outStd[t], pitch[t] + ts * scale * (1.f / 12.f));
				vg[t][0] = gate[t];
				// a generator's notes, read back for MIDI: the jacks are Shoal's own
				if (outPort[t] >= 0)
					gmid[t].tick(gate[t] > 0.f, genNote, eng.v[S::kGMidiVelocity], ids[t],
						[&](const L::Ev& e) { trackTap[t].event(mports, outPort[t], outCh[t] - 1, e, &mmap[t]); });
			}
			else {
				L::Voices& vs = voices[t];
				bool muted = eng.v[S::TP(t, S::kTMute)] || (anySolo && !eng.solo[t]);
				if (ext && muted && !wasMuted[t]) {
					psrc[t].releaseAll();
					chains[t].flushAll();
					vs.releaseAll();
				}
				wasMuted[t] = muted;
				if (!frozen) {
					L::TimeCtx& c = chains[t].ctx;
					c.now = sampleNow;
					c.beat = beatPos;
					c.spb = beatSamples;
					c.sampleRate = (uint32_t)args.sampleRate;
					c.root = eng.v[S::kGRoot];
					c.scale = eng.v[S::kGScale];
					c.running = eng.v[S::kGRun];
					if (ext) {
						psrc[t].tick([&](const L::Ev& e) { toChain(t, e); });
						const L::Slot& sl = slots[t][active[t]];
						if (laneTick && sl.kind == L::kSlotPat && patStep[t] >= 0) {
							float frac = patPeriod[t] ? std::min(1.f, (float)patElapsed[t] / patPeriod[t]) : 0.f;
							psrc[t].lanes(sl.pat, patStep[t] + frac, eng.v[S::TP(t, S::kTLength)],
								[&](const L::Ev& e) { toChain(t, e); });
						}
						patElapsed[t]++;
						if (handover[t]) {
							gsrc[t].tick(gate[t] > 0.f, genNote, eng.v[S::kGMidiVelocity], ids[t],
								[&](const L::Ev& e) { toChain(t, e); });
							if (!gsrc[t].high) { handover[t] = false; gsrc[t].reset(); }
						}
					}
					else {
						gsrc[t].tick(gate[t] > 0.f, genNote, eng.v[S::kGMidiVelocity], ids[t],
							[&](const L::Ev& e) { toChain(t, e); });
					}
					chains[t].tick();
					vs.tick();
				}
				nv[t] = vs.voices;
				for (int c = 0; c < vs.voices; c++) {
					vp[t][c] = L::applyOutStd(outStd[t], vs.volts(c) * scale + offset);
					// Freeze holds every voice that has played -- Shoal's bloom, chord and all
					bool high = frozen ? vs.voice[c].used : vs.gate(c);
					vg[t][c] = (high && !muted) ? level : 0.f;
				}
			}
			gateNow[t] = vg[t][0];
			for (int c = 1; c < nv[t]; c++) gateNow[t] = std::max(gateNow[t], vg[t][c]);
		}
		sampleNow++;
		// Each jack carries its own track's voices, or -- as a hub -- its group's first voices.
		for (int j = 0; j < S::kNumTracks; j++) {
			L::HubSpan h = L::hubSpan(hubMode, j);
			int ch = h.channels > 1 ? h.channels : nv[j];
			outputs[PITCH_OUTPUT + j].setChannels(ch);
			outputs[GATE_OUTPUT + j].setChannels(ch);
			for (int c = 0; c < ch; c++) {
				int t = h.channels > 1 ? h.firstTrack + c : j;
				int voice = h.channels > 1 ? 0 : c;
				outputs[PITCH_OUTPUT + j].setVoltage(vp[t][voice], c);
				outputs[GATE_OUTPUT + j].setVoltage(vg[t][voice], c);
			}
		}
		float curOff = currentsBipolar ? -5.f : 0.f;
		for (int t = 0; t < S::kNumTracks; t++) {
			outputs[CURRENT_OUTPUT].setVoltage(cur[t] + curOff, t);
			outputs[EOS_OUTPUT].setVoltage(eos[t], t);
			int vel = routed[t] ? voices[t].lastVel : eng.v[S::kGMidiVelocity];
			outputs[VEL_OUTPUT].setVoltage(vel * (10.f / 127.f), t);
			float mod = routed[t] && voices[t].laneSet[L::kLaneMod1] ? voices[t].lane[L::kLaneMod1] * (10.f / 127.f) : 0.f;
			outputs[MOD_OUTPUT].setVoltage(mod, t);
		}
		for (int o : { (int)CURRENT_OUTPUT, (int)EOS_OUTPUT, (int)VEL_OUTPUT, (int)MOD_OUTPUT })
			outputs[o].setChannels(S::kNumTracks);
		outputs[CLK_OUTPUT].setVoltage(clkOut);
		for (int p = 0; p < L::kMidiPorts; p++)
			if (mports[p].n) sendPort(p);

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int t = 0; t < S::kNumTracks; t++)
				lights[TRK_LIGHT + t].setSmoothBrightness(t == sel ? 1.f : (gateNow[t] > 0.f ? 0.4f : 0.06f), dt);
			lights[MUTE_LIGHT].setBrightness(base[S::TP(sel, S::kTMute)] ? 1.f : 0.f);
			lights[SOLO_LIGHT].setBrightness(eng.solo[sel] ? 1.f : 0.f);
			lights[RSED_LIGHT].setBrightness(eng.dtc->tracks[sel].pendingSeed >= 0 ? 1.f : 0.f);
			lights[RUN_LIGHT].setBrightness(eng.v[S::kGRun] ? 1.f : 0.f);
			lights[FRZE_LIGHT].setBrightness(eng.v[S::kGFreeze] ? 1.f : 0.f);
			for (int p = 0; p < kNumPages; p++)
				lights[PAGE_LIGHT + p].setBrightness(page == p ? 1.f : 0.f);
			lights[CAPT_LIGHT].setBrightness(captFlash);
			// REC: lit while recording, flashing while it waits for a first note or the clock
			bool blink = (sampleNow / (uint32_t)std::max(1.f, args.sampleRate * 0.25f)) & 1;
			lights[REC_LIGHT].setBrightness(!recOn.load() ? 0.f : recording() ? 1.f : (blink ? 1.f : 0.15f));
			captFlash = std::max(0.f, captFlash - dt * 3.f);
		}
		if (snapDiv.process())
			publish();
	}

	// The master clock as beats (one tick, a quarter note), for the effects.
	double beatPos = 0.0;
	float beatSamples = 24000.f;

	void trackBeat() {
		uint32_t tc = eng.dtc->tickCount;
		const S::Dtc* d = eng.dtc;
		uint32_t period = eng.v[S::kGClockSource] == 0 ? (d->extPeriod ? d->extPeriod : d->sampleRate / 4)
			: eng.v[S::kGClockSource] == 2 ? (d->midiPeriod ? d->midiPeriod : d->sampleRate / 4) : d->intPeriod;
		if (period < 1) period = 1;
		beatSamples = (float)period;
		if (tc != tickSeen) { tickSeen = tc; lastTickAt = sampleNow; }
		// the tick that just happened is beat tc - 1; between ticks the beat creeps on,
		// stopping short of the next one if the clock is late
		double frac = (double)(sampleNow - lastTickAt) / period;
		if (frac > 0.999) frac = 0.999;
		beatPos = tc > 0 ? (double)(tc - 1) + frac : 0.0;
	}

	// MIDI clock and transport out. Run from the top (nothing played since a reset) is
	// Start; run resuming in place, as Shoal's does, is Continue; a reset landing while it
	// runs is Start again; stopping is Stop. Between, 24 clocks a beat, the first on the beat.
	void midiClock(bool resetLanded) {
		bool running = eng.v[S::kGRun] && (eng.v[S::kGClockSource] != 2 || eng.dtc->midiRunning);
		uint32_t tc = eng.dtc->tickCount;
		bool newTick = tc != clkTickSeen;
		clkTickSeen = tc;
		bool any = false;
		for (int p = 0; p < L::kMidiPorts; p++) any = any || clkOut[p] != L::kClkOutOff;
		if (!any) { wasRunning = running; return; }
		auto transport = [&](uint8_t b) {
			for (int p = 0; p < L::kMidiPorts; p++)
				if (clkOut[p] == L::kClkOutAll || clkOut[p] == L::kClkOutTransport) mports[p].realtime(b);
		};
		if (running && !wasRunning) {
			transport(tc == 0 || eng.dtc->resetPrime ? 0xFA : 0xFB);
			clockOut.stop();
		}
		else if (running && resetLanded) { transport(0xFA); clockOut.stop(); }
		else if (!running && wasRunning) { transport(0xFC); clockOut.stop(); }
		wasRunning = running;
		if (running)
			clockOut.tick(newTick, sampleNow, beatSamples, [&]() {
				for (int p = 0; p < L::kMidiPorts; p++)
					if (clkOut[p] == L::kClkOutAll || clkOut[p] == L::kClkOutClock) mports[p].realtime(0xF8);
			});
	}

	// ---- effects -----------------------------------------------------------

	// UI thread: send a change to a track's effects. False if the ring is full.
	bool sendFx(const FxCmd& c) {
		uint32_t head = fxHead.load(std::memory_order_relaxed);
		if (head - fxTail.load(std::memory_order_acquire) >= 32)
			return false;
		fxCmds[head % 32] = c;
		fxHead.store(head + 1, std::memory_order_release);
		return true;
	}

	void applyFxCmds() {
		uint32_t tail = fxTail.load(std::memory_order_relaxed);
		while (tail != fxHead.load(std::memory_order_acquire)) {
			const FxCmd& c = fxCmds[tail % 32];
			int t = c.track;
			if (t >= 0 && t < S::kNumTracks && c.fx >= 0 && c.fx < L::kChainSlots) {
				L::Chain& ch = chains[t];
				switch (c.op) {
					case FxCmd::SET_TYPE:
					case FxCmd::PASTE:
						ch.setType(c.fx, c.value);
						if (c.op == FxCmd::PASTE)
							for (int k = 0; k < L::kMaxFxParams; k++) ch.fx[c.fx].p[k] = ch.fx[c.fx].pe[k] = c.params[k];
						for (int k = 0; k < L::kNumSlots; k++) slots[t][k].clearOverrides(c.fx);
						break;
					case FxCmd::SWAP:
						if (c.other >= 0 && c.other < L::kChainSlots && c.other != c.fx) {
							swapFx(t, c.fx, c.other);
						}
						break;
					case FxCmd::SLOT_MUTE:
						if (c.slot >= 0 && c.slot < L::kNumSlots) {
							uint8_t& m = slots[t][c.slot].fxMute;
							m = c.value ? (m | (1 << c.fx)) : (m & ~(1 << c.fx));
							bumpSlot(t, c.slot);
						}
						break;
					case FxCmd::OVERRIDE_SET:
						if (c.slot >= 0 && c.slot < L::kNumSlots) { slots[t][c.slot].setOverride(c.fx, c.param, c.value); bumpSlot(t, c.slot); }
						break;
					case FxCmd::OVERRIDE_CLEAR:
						if (c.slot >= 0 && c.slot < L::kNumSlots) { slots[t][c.slot].clearOverride(c.fx, c.param); bumpSlot(t, c.slot); }
						break;
					case FxCmd::RESTORE:
						applyRestore(t, restores[c.value % RESTORE_RING]);
						break;
				}
			}
			tail++;
			fxTail.store(tail, std::memory_order_release);
		}
	}

	// Undo's copy of a track's chain (UI thread: a read of what the audio thread owns, as
	// the display's are), and putting one back (audio thread).
	void readFxState(int t, FxState& s) const {
		for (int i = 0; i < L::kChainSlots; i++) {
			const L::Fx& f = chains[t].fx[i];
			s.type[i] = f.type;
			std::memcpy(s.p[i], f.p, sizeof s.p[i]);
			s.gmute[i] = f.gmute;
		}
		for (int k = 0; k < L::kNumSlots; k++) {
			const L::Slot& sl = slots[t][k];
			s.fxMute[k] = sl.fxMute;
			s.nOv[k] = sl.nOverrides;
			std::memcpy(s.ov[k], sl.ov, sizeof s.ov[k]);
		}
	}

	void applyRestore(int t, const FxState& s) {
		L::Chain& ch = chains[t];
		for (int i = 0; i < L::kChainSlots; i++) {
			if (ch.fx[i].type != s.type[i]) ch.setType(i, s.type[i]);
			L::Fx& f = ch.fx[i];
			for (int k = 0; k < L::kMaxFxParams; k++) {
				int v = s.p[i][k];
				if (k < f.desc().nParams) v = clamp(v, (int)f.desc().p[k].min, (int)f.desc().p[k].max);
				f.p[k] = f.pe[k] = (int16_t)v;
			}
			f.gmute = s.gmute[i];
		}
		for (int k = 0; k < L::kNumSlots; k++) {
			L::Slot& sl = slots[t][k];
			sl.fxMute = s.fxMute[k];
			sl.nOverrides = (uint8_t)std::min((int)s.nOv[k], (int)L::kMaxOverrides);
			std::memcpy(sl.ov, s.ov[k], sizeof sl.ov);
			bumpSlot(t, k);
		}
	}

	// UI thread: put track t's chain back to `s`. A restore's buffer is free again once
	// fewer than RESTORE_RING commands are waiting; this waits (briefly) for that.
	bool sendRestore(int t, const FxState& s) {
		for (int i = 0; i < 100 && fxHead.load() - fxTail.load() >= RESTORE_RING; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		uint32_t n = restoreHead.load();
		restores[n % RESTORE_RING] = s;
		restoreHead = n + 1;
		FxCmd c = {};
		c.op = FxCmd::RESTORE; c.track = t; c.fx = 0; c.value = (int)(n % RESTORE_RING);
		return sendFx(c);
	}

	// UI thread: an edit, waiting (briefly) for room in the ring.
	bool sendEditWait(const SlotEdit& e) {
		for (int i = 0; i < 200; i++) {
			if (sendEdit(e)) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	// Exchange two effects of track t: what they hold ends first; their slot values,
	// slot mutes and matrix routings go with them.
	void swapFx(int t, int a, int b) {
		L::Chain& ch = chains[t];
		if (ch.fx[a].type != L::kFxNone) ch.fx[a].flush(ch.outFrom(a + 1));
		if (ch.fx[b].type != L::kFxNone) ch.fx[b].flush(ch.outFrom(b + 1));
		L::Fx* tmp = new L::Fx;		// an Fx is several KB: not on the audio thread's stack
		*tmp = ch.fx[a]; ch.fx[a] = ch.fx[b]; ch.fx[b] = *tmp;
		delete tmp;
		bool m = ch.muted[a]; ch.muted[a] = ch.muted[b]; ch.muted[b] = m;
		for (int k = 0; k < L::kNumSlots; k++) {
			L::Slot& sl = slots[t][k];
			for (int i = 0; i < sl.nOverrides; i++)
				sl.ov[i].fx = (int8_t)(sl.ov[i].fx == a ? b : sl.ov[i].fx == b ? a : sl.ov[i].fx);
			uint8_t ba = (sl.fxMute >> a) & 1, bb = (sl.fxMute >> b) & 1;
			sl.fxMute = (uint8_t)((sl.fxMute & ~((1 << a) | (1 << b))) | (bb << a) | (ba << b));
			bumpSlot(t, k);
		}
		for (int i = 0; i < L::kNumMatrixSlots; i++) {
			L::MatrixSlot& ms = matrix[t][i];
			if (!L::destIsFx(ms.dest)) continue;
			int f = L::destFxSlot(ms.dest), pp = L::destFxParam(ms.dest);
			if (f == a) ms.dest = (int8_t)L::fxDest(b, pp);
			else if (f == b) ms.dest = (int8_t)L::fxDest(a, pp);
		}
	}

	void publish() {
		snapSeq.fetch_add(1, std::memory_order_acq_rel);	// odd: writing
		Snapshot& s = snap[0];
		std::memcpy(s.v, eng.v, sizeof s.v);
		std::memcpy(&s.dtc, eng.dtc, sizeof s.dtc);
		std::memcpy(s.solo, eng.solo, sizeof s.solo);
		s.sel = sel;
		std::memcpy(s.gate, gateNow, sizeof s.gate);
		std::memcpy(s.modMask, modMask, sizeof s.modMask);
		s.gMod = gMod;
		for (int t = 0; t < S::kNumTracks; t++) {
			s.active[t] = active[t];
			s.queued[t] = queued[t].load();
			s.patStep[t] = patStep[t];
		}
		s.rec = !recOn.load() ? 0 : recording() ? 2 : 1;
		s.songOn = songOn.load();
		s.songPos = song.pos;
		s.songLeft = song.left;
		s.trans = transSemis;
		snapSeq.fetch_add(1, std::memory_order_acq_rel);	// even: done
	}

	// UI thread: a consistent copy, or false if none could be had this frame.
	bool readSnapshot(Snapshot& out) {
		for (int tries = 0; tries < 4; tries++) {
			uint32_t a = snapSeq.load(std::memory_order_acquire);
			if (a & 1u) continue;
			std::memcpy(&out, &snap[0], sizeof out);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (snapSeq.load(std::memory_order_acquire) == a)
				return a != 0;
		}
		return false;
	}

	// UI thread: a copy of one slot as it would be saved -- the playing slot's settings
	// are the books', not what it held when it was launched.
	void readSlotFull(int t, int k, L::Slot& out) {
		readSlot(t, k, out);
		if (k == active[t] && out.kind != L::kSlotEmpty)
			L::storeBlock(out.block, base, t);
	}

	// UI thread: a copy of one slot, and the version it was copied at.
	uint32_t readSlot(int t, int k, L::Slot& out) {
		uint32_t v = slotVersion[t][k].load(std::memory_order_acquire);
		out = slots[t][k];
		return v;
	}

	// ---- patch state -------------------------------------------------------

	static json_t* slotToJson(const L::Slot& s) {
		json_t* js = json_object();
		json_object_set_new(js, "kind", json_integer(s.kind));
		json_t* b = json_array();
		for (int i = 0; i < L::kNumBlock; i++) json_array_append_new(b, json_integer(s.block[i]));
		json_object_set_new(js, "block", b);
		if (s.fxMute) json_object_set_new(js, "fxMute", json_integer(s.fxMute));
		if (s.nOverrides) {
			json_t* ov = json_array();
			for (int i = 0; i < s.nOverrides; i++) {
				json_t* o = json_array();
				json_array_append_new(o, json_integer(s.ov[i].fx));
				json_array_append_new(o, json_integer(s.ov[i].param));
				json_array_append_new(o, json_integer(s.ov[i].value));
				json_array_append_new(ov, o);
			}
			json_object_set_new(js, "fxValues", ov);
		}
		if (s.kind == L::kSlotPat) {
			json_t* notes = json_array();
			for (int i = 0; i < s.pat.count; i++) {
				const L::Note& n = s.pat.notes[i];
				json_t* jn = json_array();
				json_array_append_new(jn, json_integer(n.start));
				json_array_append_new(jn, json_integer(n.len));
				json_array_append_new(jn, json_integer(n.pitch));
				json_array_append_new(jn, json_integer(n.vel));
				json_array_append_new(notes, jn);
			}
			json_object_set_new(js, "notes", notes);
			json_t* lanes = json_array();
			for (int l = 0; l < L::kNumLanes; l++) {
				json_t* jl = json_object();
				json_object_set_new(jl, "interp", json_integer(s.pat.interp[l]));
				json_t* pts = json_array();
				for (int st = 0; st < S::kMaxSteps; st++)
					if (s.pat.lane[l][st] != L::kLaneUnset) {
						json_t* pt = json_array();
						json_array_append_new(pt, json_integer(st));
						json_array_append_new(pt, json_integer(s.pat.lane[l][st]));
						json_array_append_new(pts, pt);
					}
				json_object_set_new(jl, "points", pts);
				json_array_append_new(lanes, jl);
			}
			json_object_set_new(js, "lanes", lanes);
		}
		return js;
	}

	static void slotFromJson(json_t* js, int t, L::Slot& s) {
		s.clear();
		if (!js) return;
		s.kind = (uint8_t)clamp((int)json_integer_value(json_object_get(js, "kind")), 0, 2);
		json_t* b = json_object_get(js, "block");
		for (int i = 0; i < L::kNumBlock; i++) {
			int p = L::blockParam(i, t);
			S::ParamRange r = S::paramRange(p);
			int v = b && i < (int)json_array_size(b) ? (int)json_integer_value(json_array_get(b, i)) : r.def;
			s.block[i] = (int16_t)clamp(v, (int)r.min, (int)r.max);
		}
		s.fxMute = (uint8_t)json_integer_value(json_object_get(js, "fxMute"));
		if (json_t* ov = json_object_get(js, "fxValues"))
			for (size_t i = 0; i < json_array_size(ov); i++) {
				json_t* o = json_array_get(ov, i);
				int fx = (int)json_integer_value(json_array_get(o, 0)), pr = (int)json_integer_value(json_array_get(o, 1));
				if (fx < 0 || fx >= L::kChainSlots || pr < 0 || pr >= L::kMaxFxParams) continue;
				s.setOverride(fx, pr, (int)json_integer_value(json_array_get(o, 2)));
			}
		if (json_t* notes = json_object_get(js, "notes")) {
			for (size_t i = 0; i < json_array_size(notes) && s.pat.count < L::kMaxNotes; i++) {
				json_t* jn = json_array_get(notes, i);
				L::Note n;
				n.start = (uint16_t)clamp((int)json_integer_value(json_array_get(jn, 0)), 0, S::kMaxSteps * L::kTicksPerStep - 1);
				n.len = (uint16_t)clamp((int)json_integer_value(json_array_get(jn, 1)), 1, S::kMaxSteps * L::kTicksPerStep);
				n.pitch = (uint8_t)clamp((int)json_integer_value(json_array_get(jn, 2)), 0, 127);
				n.vel = (uint8_t)clamp((int)json_integer_value(json_array_get(jn, 3)), 1, 127);
				s.pat.notes[s.pat.count++] = n;
			}
			s.pat.sort();
		}
		if (json_t* lanes = json_object_get(js, "lanes")) {
			for (int l = 0; l < L::kNumLanes && l < (int)json_array_size(lanes); l++) {
				json_t* jl = json_array_get(lanes, l);
				s.pat.interp[l] = (uint8_t)clamp((int)json_integer_value(json_object_get(jl, "interp")), 0, L::kNumInterps - 1);
				json_t* pts = json_object_get(jl, "points");
				for (size_t i = 0; pts && i < json_array_size(pts); i++) {
					json_t* pt = json_array_get(pts, i);
					int st = (int)json_integer_value(json_array_get(pt, 0));
					if (st < 0 || st >= S::kMaxSteps) continue;
					s.pat.lane[l][st] = (int16_t)clamp((int)json_integer_value(json_array_get(pt, 1)),
						L::laneMin(l), L::laneMax(l));
				}
			}
		}
	}

	json_t* dataToJson() override {
		// the active slots' settings are in base[]; the saved slots should say so too
		for (int t = 0; t < S::kNumTracks; t++)
			if (slots[t][active[t]].kind != L::kSlotEmpty)
				L::storeBlock(slots[t][active[t]].block, base, t);
		json_t* root = json_object();
		json_object_set_new(root, "version", json_integer(1));
		json_t* v = json_array();
		for (int p = 0; p < S::kNumParameters; p++)
			json_array_append_new(v, json_integer(base[p]));
		json_object_set_new(root, "shoal", v);
		json_t* solo = json_array();
		for (int t = 0; t < S::kNumTracks; t++)
			json_array_append_new(solo, json_boolean(eng.solo[t]));
		json_object_set_new(root, "solo", solo);
		json_t* tracks = json_array();
		for (int t = 0; t < S::kNumTracks; t++) {
			json_t* tr = json_object();
			json_object_set_new(tr, "pitchStandard", json_integer(outStd[t]));
			json_object_set_new(tr, "voices", json_integer(voices[t].voices));
			json_object_set_new(tr, "allocator", json_integer(voices[t].alloc));
			json_object_set_new(tr, "retrigger", json_boolean(voices[t].retrig));
			json_t* fx = json_array();
			for (int i = 0; i < L::kChainSlots; i++) {
				const L::Fx& f = chains[t].fx[i];
				json_t* jf = json_object();
				json_object_set_new(jf, "type", json_integer(f.type));
				json_object_set_new(jf, "mute", json_boolean(f.gmute));
				json_t* pa = json_array();
				for (int k = 0; k < f.desc().nParams; k++) json_array_append_new(pa, json_integer(f.p[k]));
				json_object_set_new(jf, "params", pa);
				json_array_append_new(fx, jf);
			}
			json_object_set_new(tr, "effects", fx);
			json_object_set_new(tr, "activeSlot", json_integer(active[t]));
			json_t* mx = json_array();
			for (int i = 0; i < L::kNumMatrixSlots; i++) {
				const L::MatrixSlot& s = matrix[t][i];
				json_t* js = json_object();
				json_object_set_new(js, "source", json_integer(s.source));
				json_object_set_new(js, "dest", json_integer(s.dest));
				json_object_set_new(js, "amount", json_integer(s.amount));
				json_object_set_new(js, "unipolar", json_boolean(s.unipolar));
				json_object_set_new(js, "offset", json_integer(s.offset));
				json_object_set_new(js, "cc", json_integer(s.cc));
				json_array_append_new(mx, js);
			}
			json_object_set_new(tr, "matrix", mx);
			json_t* jm = json_object();
			json_object_set_new(jm, "inChannel", json_integer(inCh[t]));
			json_object_set_new(jm, "outPort", json_integer(outPort[t]));
			json_object_set_new(jm, "outChannel", json_integer(outCh[t]));
			json_t* cc = json_array();
			for (int l = 0; l < L::kModCCs; l++) json_array_append_new(cc, json_integer(mmap[t].modCC[l]));
			json_object_set_new(jm, "modCC", cc);
			json_object_set_new(jm, "modInCC", json_integer(live[t].modCC));
			json_object_set_new(jm, "followsTranspose", json_boolean(followTrans[t]));
			json_object_set_new(tr, "midi", jm);
			json_t* sl = json_array();
			for (int k = 0; k < L::kNumSlots; k++)
				json_array_append_new(sl, slotToJson(slots[t][k]));
			json_object_set_new(tr, "slots", sl);
			json_array_append_new(tracks, tr);
		}
		json_object_set_new(root, "tracks", tracks);
		json_object_set_new(root, "selected", json_integer(sel));
		json_object_set_new(root, "freeze", json_boolean(freezeLatch));
		json_object_set_new(root, "currentsBipolar", json_boolean(currentsBipolar));
		json_object_set_new(root, "polyRouting", json_integer(hubMode));
		json_object_set_new(root, "launch", json_integer(launchQ));
		json_object_set_new(root, "launchTrack", json_integer(launchTrack));
		json_object_set_new(root, "launchRestarts", json_boolean(launchRestart));
		json_t* js = json_array();
		for (int i = 0; i < song.len; i++) {
			json_t* e = json_array();
			json_array_append_new(e, json_integer(song.e[i].row));
			json_array_append_new(e, json_integer(song.e[i].times));
			json_array_append_new(js, e);
		}
		json_object_set_new(root, "song", js);
		json_object_set_new(root, "songOn", json_boolean(songOn.load()));
		json_object_set_new(root, "page", json_integer(page.load()));
		json_t* jm = json_object();
		json_object_set_new(jm, "in", midiIn.toJson());
		json_t* outs = json_array();
		for (int p = 0; p < L::kMidiPorts; p++) {
			json_t* o = midiOut[p].toJson();
			json_object_set_new(o, "clock", json_integer(clkOut[p]));
			json_array_append_new(outs, o);
		}
		json_object_set_new(jm, "out", outs);
		json_object_set_new(jm, "inputTo", json_integer(inMode));
		json_object_set_new(jm, "clockIn", json_boolean(clockFromMidi));
		json_object_set_new(jm, "programChange", json_boolean(pcLaunches));
		json_object_set_new(jm, "transposeChannel", json_integer(transCh));
		json_object_set_new(jm, "transposeMode", json_integer(transMode));
		json_object_set_new(jm, "recMode", json_integer(recMode));
		json_object_set_new(jm, "punchIn", json_boolean(punch));
		json_object_set_new(root, "midi", jm);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* v = json_object_get(root, "shoal");
		if (v && json_is_array(v)) {
			for (int p = 0; p < S::kNumParameters && p < (int)json_array_size(v); p++) {
				S::ParamRange r = S::paramRange(p);
				int x = (int)json_integer_value(json_array_get(v, p));
				base[p] = (int16_t)clamp(x, (int)r.min, (int)r.max);
			}
		}
		bool solo[S::kNumTracks] = {};
		if (json_t* s = json_object_get(root, "solo"))
			for (int t = 0; t < S::kNumTracks && t < (int)json_array_size(s); t++)
				solo[t] = json_is_true(json_array_get(s, t));
		json_t* tracks = json_object_get(root, "tracks");
		uint32_t sr = (uint32_t)std::max(1.f, APP->engine->getSampleRate());
		for (int t = 0; t < S::kNumTracks; t++) {
			json_t* tr = tracks ? json_array_get(tracks, t) : NULL;
			outStd[t] = tr ? clamp((int)json_integer_value(json_object_get(tr, "pitchStandard")), 0, L::kNumOutStds - 1) : 0;
			voices[t].init(sr);
			for (int i = 0; i < L::kChainSlots; i++) { chains[t].fx[i].setType(L::kFxNone); chains[t].fx[i].gmute = false; chains[t].muted[i] = false; }
			if (tr) {
				if (json_t* j = json_object_get(tr, "voices"))
					voices[t].setVoices((int)json_integer_value(j));
				voices[t].alloc = clamp((int)json_integer_value(json_object_get(tr, "allocator")), 0, L::kNumAllocs - 1);
				voices[t].retrig = json_is_true(json_object_get(tr, "retrigger"));
				json_t* fx = json_object_get(tr, "effects");
				for (int i = 0; fx && i < L::kChainSlots && i < (int)json_array_size(fx); i++) {
					json_t* jf = json_array_get(fx, i);
					L::Fx& f = chains[t].fx[i];
					f.setType(clamp((int)json_integer_value(json_object_get(jf, "type")), 0, L::kNumFxTypes - 1));
					f.gmute = json_is_true(json_object_get(jf, "mute"));
					json_t* pa = json_object_get(jf, "params");
					for (int k = 0; pa && k < f.desc().nParams && k < (int)json_array_size(pa); k++) {
						const L::ParamDesc& pd = f.desc().p[k];
						f.p[k] = f.pe[k] = (int16_t)clamp((int)json_integer_value(json_array_get(pa, k)), (int)pd.min, (int)pd.max);
					}
				}
			}
			json_t* mx = tr ? json_object_get(tr, "matrix") : NULL;
			for (int i = 0; i < L::kNumMatrixSlots; i++) {
				L::MatrixSlot s;
				json_t* js = mx ? json_array_get(mx, i) : NULL;
				if (js) {
					s.source = (int8_t)clamp((int)json_integer_value(json_object_get(js, "source")), 0, L::kNumMatrixSources);
					s.dest = (int8_t)clamp((int)json_integer_value(json_object_get(js, "dest")), 0, L::fxDest(L::kChainSlots - 1, L::kFxParamStride - 1));
					s.amount = (int16_t)clamp((int)json_integer_value(json_object_get(js, "amount")), -100, 100);
					s.unipolar = json_is_true(json_object_get(js, "unipolar"));
					s.offset = (int16_t)clamp((int)json_integer_value(json_object_get(js, "offset")), -100, 100);
					if (json_t* c = json_object_get(js, "cc"))
						s.cc = (int8_t)clamp((int)json_integer_value(c), 0, 119);
				}
				matrix[t][i] = s;
			}
			inCh[t] = t + 1;
			outPort[t] = -1;
			outCh[t] = t + 1;
			mmap[t] = L::MidiMap();
			live[t].modCC = 1;
			followTrans[t] = true;
			if (json_t* jm = tr ? json_object_get(tr, "midi") : NULL) {
				inCh[t] = clamp((int)json_integer_value(json_object_get(jm, "inChannel")), 0, (int)CH_ANY);
				outPort[t] = clamp((int)json_integer_value(json_object_get(jm, "outPort")), -1, L::kMidiPorts - 1);
				outCh[t] = clamp((int)json_integer_value(json_object_get(jm, "outChannel")), 1, 16);
				json_t* cc = json_object_get(jm, "modCC");
				for (int l = 0; cc && l < L::kModCCs && l < (int)json_array_size(cc); l++)
					mmap[t].modCC[l] = (int8_t)clamp((int)json_integer_value(json_array_get(cc, l)), -1, 119);
				if (json_t* j = json_object_get(jm, "modInCC"))
					live[t].modCC = clamp((int)json_integer_value(j), 0, 119);
				followTrans[t] = !json_is_false(json_object_get(jm, "followsTranspose"));
			}
			json_t* sl = tr ? json_object_get(tr, "slots") : NULL;
			if (sl) {
				for (int k = 0; k < L::kNumSlots; k++)
					slotFromJson(json_array_get(sl, k), t, slots[t][k]);
				active[t] = clamp((int)json_integer_value(json_object_get(tr, "activeSlot")), 0, L::kNumSlots - 1);
			}
			else {
				// a patch from before slots: the track's settings are its one generator
				for (int k = 0; k < L::kNumSlots; k++) slots[t][k].clear();
				slots[t][0].kind = L::kSlotGen;
				L::storeBlock(slots[t][0].block, base, t);
				active[t] = 0;
			}
			for (int k = 0; k < L::kNumSlots; k++) bumpSlot(t, k);
		}
		if (json_t* j = json_object_get(root, "selected"))
			sel = clamp((int)json_integer_value(j), 0, S::kNumTracks - 1);
		freezeLatch = json_is_true(json_object_get(root, "freeze"));
		currentsBipolar = json_is_true(json_object_get(root, "currentsBipolar"));
		hubMode = clamp((int)json_integer_value(json_object_get(root, "polyRouting")), 0, L::kNumHubs - 1);
		if (json_t* j = json_object_get(root, "launch"))
			launchQ = clamp((int)json_integer_value(j), 0, kNumLaunchQ - 1);
		launchTrack = clamp((int)json_integer_value(json_object_get(root, "launchTrack")), 0, S::kNumTracks - 1);
		launchRestart = !json_is_false(json_object_get(root, "launchRestarts"));
		song.clear();
		if (json_t* js = json_object_get(root, "song"))
			for (size_t i = 0; i < json_array_size(js) && song.len < L::kSongMax; i++) {
				json_t* e = json_array_get(js, i);
				L::SongEntry se;
				se.row = (int8_t)clamp((int)json_integer_value(json_array_get(e, 0)), 0, L::kNumSlots - 1);
				se.times = (uint8_t)clamp((int)json_integer_value(json_array_get(e, 1)), 1, 16);
				song.e[song.len++] = se;
			}
		songOn = json_is_true(json_object_get(root, "songOn"));
		page = clamp((int)json_integer_value(json_object_get(root, "page")), 0, kNumPages - 1);
		if (json_t* jm = json_object_get(root, "midi")) {
			if (json_t* j = json_object_get(jm, "in")) midiIn.fromJson(j);
			midiIn.channel = -1;
			json_t* outs = json_object_get(jm, "out");
			for (int p = 0; p < L::kMidiPorts; p++) {
				json_t* o = outs ? json_array_get(outs, p) : NULL;
				if (o) {
					midiOut[p].fromJson(o);
					clkOut[p] = clamp((int)json_integer_value(json_object_get(o, "clock")), 0, L::kNumClkOut - 1);
				}
				midiOut[p].channel = -1;	// whatever was saved: each message carries its own
			}
			inMode = clamp((int)json_integer_value(json_object_get(jm, "inputTo")), 0, 1);
			clockFromMidi = json_is_true(json_object_get(jm, "clockIn"));
			pcLaunches = !json_is_false(json_object_get(jm, "programChange"));
			transCh = clamp((int)json_integer_value(json_object_get(jm, "transposeChannel")), 0, 16);
			transMode = clamp((int)json_integer_value(json_object_get(jm, "transposeMode")), 0, 1);
			recMode = clamp((int)json_integer_value(json_object_get(jm, "recMode")), 0, L::kNumRecModes - 1);
			punch = json_is_true(json_object_get(jm, "punchIn"));
		}
		initEngine(false);
		std::memcpy(eng.solo, solo, sizeof solo);
	}
};
