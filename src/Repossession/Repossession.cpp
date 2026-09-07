/** Repossession -- FORM 1099-A, acquisition or abandonment of secured property.

Paste a link. The module seizes the media: yt-dlp fetches it, ffmpeg renders the
audio to float PCM at the engine's rate and the video to a 160x90 RGBA strip at
12 fps, and both land in a cache keyed by the video id. The panel then shows the
frames, a waveform, and up to eight regions you draw on the timeline with the
mouse. A clock walks the regions; the audio and the picture move together,
because the picture is drawn from the same playhead the sampler reads with.

Three threads touch this module and each one has a lane:

  worker  Media.hpp's Loader. Child processes and file reads. Never touches the
          Module, the widget, or anything Rack owns -- it is handed a Request by
          value and hands back a shared_ptr<Media>.
  UI      the widget's step(), which is the only place a finished Media is
          swapped in, and draw(), which is the only place a video frame is read
          off disk.
  audio   process(), which reads the Media through an atomic raw pointer and
          allocates, locks and opens nothing.

The swap is the interesting part. process() reads `active`, a plain
atomic<Media*>. When a new one arrives the UI thread keeps the old shared_ptr in
`retired` and only drops it once process() has ticked past the store, so a
pointer the audio thread was mid-way through using cannot be freed under it.
*/
#include "../plugin.hpp"
#include "Media.hpp"
#include "Expander.hpp"
#include "Panel.hpp"
#include "Windows.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>


/** How long a replaced window is kept alive after the audio thread has moved
    past the swap.

    The Media handover next to it can afford four ticks because nothing holds a
    Media pointer across samples. A window is different: the crossfade's trailing
    tap goes on reading the *old* window for the whole fade, up to 10 ms at the
    longest setting -- and `tick` counts samples, so four of them is about a
    ten-thousandth of what is needed. 8192 is a sixth of a second at 48 kHz,
    comfortably past any fade and still freed inside the drag that caused it. */
static const uint64_t WINDOW_RETIRE_TICKS = 8192;

static const char* MODE_NAMES[] = {"Forward", "Random", "Ping-pong", "CV only"};
static const int MAX_MINUTES[] = {1, 3, 6, 10};
static const float XFADE_MS[] = {0.f, 2.f, 4.f, 10.f};


/** rack::clamp overloads on int and float only, and the read pointer is a
    double: at 48 kHz a float runs out of integer precision 3 seconds in. */
static inline double clampd(double x, double a, double b) {
	return (x < a) ? a : ((x > b) ? b : x);
}


/** Each slot's own colour: a LIME to MINT ramp across the eight.

    Alternating two inks tells a slot from its neighbour and nothing else -- slot
    1 and slot 3 would still be the same green. A ramp gives every slot a shade
    of its own while staying inside the family's two live inks, so eight lights
    in a row read as one instrument rather than a fruit salad. The timeline spans
    are drawn from this same function, which is the point: a span and its button
    cannot end up different colours. Disabled slots are drawn in CLAY, the
    family's alert ink, because being skipped is a state worth seeing. */
static NVGcolor slotInk(int i) {
	float t = (rp::NUM_SLOTS > 1)
		? (float) i / (float) (rp::NUM_SLOTS - 1) : 0.f;
	return nvgLerpRGBA(panel::LIME, panel::MINT, t);
}


/** Catmull-Rom, the usual four-point cubic for a fractional read pointer. */
static inline float hermite(float y0, float y1, float y2, float y3, float t) {
	float c0 = y1;
	float c1 = 0.5f * (y2 - y0);
	float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
	float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
	return ((c3 * t + c2) * t + c1) * t + c0;
}


struct Repossession : Module {
	enum ParamId {
		REGION_PARAM,
		SPEED_PARAM,
		GAIN_PARAM,
		LOOP_PARAM,
		REV_PARAM,
		MODE_PARAM,
		TEMPO_PARAM,
		RUNMODE_PARAM,
		SLOT_PARAM,
		PARAMS_LEN = SLOT_PARAM + rp::NUM_SLOTS
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		REGION_INPUT,
		SCAN_INPUT,
		FIRE_INPUT,
		SPEED_INPUT,
		GAIN_INPUT,
		START_INPUT,
		LEN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		STEPS_OUTPUT,
		POS_OUTPUT,
		GATE_OUTPUT,
		EOR_OUTPUT,
		REGION_OUTPUT,
		OUTPUTS_LEN
	};
	/** Three light ids per slot, because each slot wears its own colour and a
	    fixed ink could only have said on or off. See slotInk(). */
	enum LightId {
		BUSY_LIGHT,
		CLOCK_LIGHT,
		SLOT_LIGHT,
		LIGHTS_LEN = SLOT_LIGHT + rp::NUM_SLOTS * 3
	};
	enum Mode { MODE_FORWARD, MODE_RANDOM, MODE_PINGPONG, MODE_CV, NUM_MODES };
	/** The transport, as one control with three things to say. CKSSThree counts
	    from the bottom, so LATCH is down, STOP is the middle, RUN is up. Latching
	    used to be reachable only by accident -- patch a clock, stop it, and
	    whichever step you landed on looped forever. */
	enum RunMode { RUN_LATCH = 0, RUN_STOP = 1, RUN_GO = 2 };

	/** One seized span, in fractions of the whole clip so it survives a
	    re-import at another sample rate. Plain floats, not atomics: the UI
	    thread edits them while the audio thread reads them, and process()
	    re-clamps and re-orders every sample, so the worst a half-applied drag
	    can do is move a loop point one buffer early. */
	struct Region {
		bool active = false;
		/** Held but skipped. Distinct from `active`: an empty slot has nothing in
		    it, a disabled one keeps its span, its speed and its gain and simply
		    is not visited. Emptying a slot to skip it and then wanting it back is
		    the mistake this exists to prevent. */
		bool enabled = true;
		float start = 0.f;
		float end = 0.f;
		float speed = 0.f;      // octaves: the rate multiplier is 2^speed
		bool reverse = false;
		float gain = 1.f;
		bool loop = true;

		float lo() const { return std::min(start, end); }
		float hi() const { return std::max(start, end); }
		/** What the sequencer will visit: something is there, and it is armed. */
		bool live() const { return active && enabled; }
	};
	Region regions[rp::NUM_SLOTS];

	// --- the media, and how it crosses to the audio thread -------------------
	rp::Loader loader;
	std::atomic<rp::Media*> active;
	std::shared_ptr<rp::Media> owner;       // UI thread
	std::shared_ptr<rp::Media> retired;     // UI thread
	std::atomic<uint64_t> tick;
	uint64_t retireTick = 0;

	// --- the windows: the only audio actually in RAM -------------------------
	// Same handover as the media above, one per slot: the audio thread reads a
	// plain pointer, and the shared_ptr under it is dropped only once process()
	// has demonstrably ticked past the swap.
	rp::WindowLoader windows;
	std::atomic<rp::Window*> activeWin[rp::NUM_SLOTS];
	std::shared_ptr<rp::Window> winOwner[rp::NUM_SLOTS];    // UI thread
	std::shared_ptr<rp::Window> winRetired[rp::NUM_SLOTS];  // UI thread
	uint64_t winRetireTick[rp::NUM_SLOTS];
	/** What the UI last asked the loader for, so a drag that has not changed
	    anything does not queue a re-read every frame. */
	float wantLo[rp::NUM_SLOTS], wantHi[rp::NUM_SLOTS];
	std::string wantId[rp::NUM_SLOTS];

	/** The SCHEDULE A seam. Rack gives each side two buffers and flips them once
	    a frame, so neither module reads what the other is writing. */
	rp::SchedMessage schedA, schedB;

	rp::Budget budget;
	int budgetIndex = 2;        // into BUDGET_MB: 64 MB
	/** Raised when arming a step found the pool empty. The step blinks rather
	    than silently staying dark, because "there is no memory left" is a
	    different thing from "you missed the button". */
	std::atomic<int> uiBlinkSlot;
	std::atomic<float> uiBlinkPhase;

	// --- persisted settings --------------------------------------------------
	std::string url;
	std::string mediaId, mediaSource, mediaTitle;
	bool mediaIsPath = false;
	int maxIndex = 1;           // into MAX_MINUTES: 3 minutes
	int xfadeIndex = 2;         // into XFADE_MS: 4 ms
	std::string toolDir;
	/** Set by dataFromJson, consumed by the widget: the loader may only be
	    started from the UI thread, and dataFromJson runs before the widget has
	    finished being wired up. */
	bool restorePending = false;

	// --- published for the panel ---------------------------------------------
	std::atomic<float> uiPos;       // 0..1 over the whole clip
	std::atomic<int> uiSlot;
	std::atomic<bool> uiPlaying;
	std::atomic<float> uiBusy;      // brightness for the caption light

	// --- audio-thread state --------------------------------------------------
	int playSlot = 0;
	int pingDir = 1;
	double pos = 0.0;
	bool playing = false;
	double oldPos = 0.0;
	double oldStep = 0.0;
	int xfadeLeft = 0;
	int xfadeLen = 0;
	int lastSel = -1;

	/** The internal clock's phase, in beats. Only advanced when nothing is
	    patched into CLOCK: an external clock is always the authority, so a cable
	    silences the internal one rather than fighting it. */
	double clockPhase = 0.0;
	bool internalPulse = false;

	/** The step buttons, which mean different things held than tapped. A tap
	    fires the step; holding past HOLD_SECONDS loops it for as long as the
	    finger is down, whatever the region's own LOOP says. -1 is nobody. */
	int heldSlot = -1;
	double heldFor = 0.0;
	bool heldLooping = false;
	bool slotDown[rp::NUM_SLOTS];

	dsp::SchmittTrigger clockTrig, resetTrig, fireTrig;
	dsp::PulseGenerator eorPulse;
	dsp::ClockDivider lightDiv;

	Repossession() {
		active.store(NULL);
		tick.store(0);
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			activeWin[i].store(NULL);
			winRetireTick[i] = 0;
			wantLo[i] = wantHi[i] = -1.f;
		}
		uiBlinkSlot.store(-1);
		uiBlinkPhase.store(0.f);
		uiPos.store(0.f);
		uiSlot.store(0);
		uiPlaying.store(false);
		uiBusy.store(0.f);

		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configSwitch(REGION_PARAM, 0.f, (float) (rp::NUM_SLOTS - 1), 0.f, "Region",
			{"1", "2", "3", "4", "5", "6", "7", "8"});
		configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Region speed", "x", 2.f);
		configParam(GAIN_PARAM, 0.f, 2.f, 1.f, "Region gain", "%", 0.f, 100.f);
		configSwitch(LOOP_PARAM, 0.f, 1.f, 1.f, "Region loop", {"Off", "On"});
		configSwitch(REV_PARAM, 0.f, 1.f, 0.f, "Region direction",
			{"Forward", "Reverse"});
		configSwitch(MODE_PARAM, 0.f, (float) (NUM_MODES - 1), 0.f, "Sequence mode",
			{MODE_NAMES[0], MODE_NAMES[1], MODE_NAMES[2], MODE_NAMES[3]});
		configParam(TEMPO_PARAM, 30.f, 300.f, 120.f, "Internal tempo", " BPM");
		configSwitch(RUNMODE_PARAM, 0.f, 2.f, 2.f, "Transport",
			{"Latch (loop the selected step)", "Stopped", "Running"});
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			configButton(SLOT_PARAM + i, string::f("Region %d", i + 1));

		configInput(CLOCK_INPUT, "Clock / step (silences the internal clock)");
		configInput(RESET_INPUT, "Reset");
		configInput(REGION_INPUT, "Region select CV (0-10 V over the eight slots)");
		configInput(SCAN_INPUT, "Scan CV (0-10 V scrubs inside the region)");
		configInput(FIRE_INPUT, "Fire (re-triggers the current region)");
		// The four per-step inputs. Polyphonic by channel, monophonic by cable:
		// see polyAt(). Saying so in the tooltip matters, because one cable
		// behaving as eight is not something a patcher should have to discover.
		configInput(SPEED_INPUT, "Speed CV, per step (1 V per 0.4 octaves; "
			"poly channel N is step N, mono applies to all)");
		configInput(GAIN_INPUT, "Gain CV, per step (5 V doubles; "
			"poly channel N is step N, mono applies to all)");
		configInput(START_INPUT, "Window start CV, per step (5 V shifts by a "
			"tenth of the clip; poly channel N is step N)");
		configInput(LEN_INPUT, "Window length CV, per step (5 V doubles, "
			"-5 V halves; poly channel N is step N)");

		configOutput(OUT_L_OUTPUT, "Audio left");
		configOutput(OUT_R_OUTPUT, "Audio right");
		configOutput(STEPS_OUTPUT, "Per-step audio (8 channels; step N is on "
			"channel N, mono)");
		configOutput(POS_OUTPUT, "Playhead position (0-10 V over the clip)");
		configOutput(GATE_OUTPUT, "Gate (high while a region plays)");
		configOutput(EOR_OUTPUT, "End of region trigger");
		configOutput(REGION_OUTPUT, "Region number (1 V per slot)");

		for (int i = 0; i < rp::NUM_SLOTS; i++)
			slotDown[i] = false;

		rightExpander.producerMessage = &schedA;
		rightExpander.consumerMessage = &schedB;

		lightDiv.setDivision(32);
	}

	~Repossession() override {
		// Cancels the child process and joins, so a module deleted mid-download
		// does not leave a worker holding a pipe. The window loader's own
		// destructor joins the same way.
		loader.stop();
		active.store(NULL);
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			activeWin[i].store(NULL);
	}

	// --- media handover (UI thread only) -------------------------------------

	double maxSeconds() const {
		return 60.0 * MAX_MINUTES[clamp(maxIndex, 0, 3)];
	}

	void requestLoad(const std::string& source, bool isPath, const std::string& id,
	                 float sampleRate) {
		if (source.empty())
			return;
		rp::Request req;
		req.source = source;
		req.isPath = isPath;
		req.id = id;
		req.sampleRate = (int) (sampleRate + 0.5f);
		req.maxSeconds = maxSeconds();
		req.toolDir = toolDir;
		mediaSource = source;
		mediaIsPath = isPath;
		loader.start(req);
	}

	void adopt(std::shared_ptr<rp::Media> m) {
		if (!m)
			return;
		retired = owner;
		retireTick = tick.load() + 4;
		owner = m;
		mediaId = m->id;
		mediaSource = m->source;
		mediaTitle = m->title;
		active.store(m.get(), std::memory_order_release);
		// The budget is denominated in seconds, and how many seconds a megabyte
		// buys depends on the rate the clip was decoded at.
		budget.rescale(budgetSeconds());
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			wantLo[i] = wantHi[i] = -1.f;   // every window is stale now
		if (!anyRegion())
			resetRegions();
	}

	/** Frees the previous Media once process() has certainly re-read `active`.
	    With the engine paused the counter does not move and the buffer is simply
	    held until it does, or until the module is deleted. */
	void releaseRetired() {
		if (retired && tick.load() > retireTick)
			retired.reset();
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			if (winRetired[i] && tick.load() > winRetireTick[i])
				winRetired[i].reset();
	}

	// --- the budget, and the windows it pays for -----------------------------
	// All UI thread. The audio thread never sees any of this; it only ever reads
	// whichever window happens to be published for the slot it is playing.

	double budgetSeconds() const {
		int mb = rp::BUDGET_MB[clamp(budgetIndex, 0, rp::NUM_BUDGETS - 1)];
		int rate = owner ? owner->sampleRate : 48000;
		return rp::Budget::secondsFor(mb, rate);
	}

	/** Bytes actually resident, which is what the meter reports. Deliberately
	    measured rather than assumed: the granted seconds are a promise, and the
	    only honest number is the one from the buffers themselves. */
	size_t residentBytes() const {
		size_t t = 0;
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			if (winOwner[i])
				t += winOwner[i]->bytes();
		return t;
	}

	/** Arm or disable a step, keeping the budget straight.

	    Disabling always works and hands the step's seconds back to the pool.
	    Arming takes an even share out of whatever is left -- which can be
	    nothing, if the other steps have grown into it, and then the step stays
	    disabled and blinks. That refusal is the mechanic, not an error: eight
	    steps share one budget, and taking room back is not free. */
	bool setStepEnabled(int i, bool on) {
		if (i < 0 || i >= rp::NUM_SLOTS)
			return false;
		Region& R = regions[i];
		if (!R.active)
			return false;
		if (on == R.enabled)
			return true;
		if (!on) {
			R.enabled = false;
			budget.release(i);
			return true;
		}
		if (budget.reclaim(i) <= 1e-6) {
			uiBlinkSlot.store(i);
			uiBlinkPhase.store(0.f);
			return false;
		}
		R.enabled = true;
		return true;
	}

	/** Holds every armed step to the seconds it has been granted, growing into
	    the free pool where it can. Called every frame: a drag changes a region
	    continuously, and this is what turns "as long as you like" into "as long
	    as the budget allows". */
	void reconcileBudget() {
		rp::Media* m = owner.get();
		if (!m || m->duration <= 0.0)
			return;
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			Region& R = regions[i];
			if (!R.active || !R.enabled)
				continue;
			float lo = R.lo(), hi = R.hi();
			double want = (double) (hi - lo) * m->duration;
			double got = budget.request(i, want);
			if (got + 1e-9 < want) {
				// Trim from the far edge, so the start the user placed stays
				// where they put it and only the length gives.
				hi = lo + (float) (got / m->duration);
				R.start = lo;
				R.end = hi;
			}
		}
	}

	/** Asks the loader for any window that no longer matches its region, and
	    publishes any that have arrived. */
	void reconcileWindows() {
		rp::Media* m = owner.get();
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			std::shared_ptr<rp::Window> w = windows.take(i);
			if (w) {
				// A window cut from a clip that has since been replaced is not
				// this module's audio any more.
				if (m && w->mediaId == m->id) {
					winRetired[i] = winOwner[i];
					winRetireTick[i] = tick.load() + WINDOW_RETIRE_TICKS;
					winOwner[i] = w;
					activeWin[i].store(w.get(), std::memory_order_release);
				}
			}
			if (!m || !m->playable()) {
				if (winOwner[i]) {
					activeWin[i].store(NULL, std::memory_order_release);
					winRetired[i] = winOwner[i];
					winRetireTick[i] = tick.load() + WINDOW_RETIRE_TICKS;
					winOwner[i].reset();
				}
				wantLo[i] = wantHi[i] = -1.f;
				continue;
			}

			const Region& R = regions[i];
			if (!R.active) {
				// An empty slot keeps no audio: that is the whole point of the
				// budget, and a released slot must give its bytes back at once.
				if (winOwner[i]) {
					activeWin[i].store(NULL, std::memory_order_release);
					winRetired[i] = winOwner[i];
					winRetireTick[i] = tick.load() + WINDOW_RETIRE_TICKS;
					winOwner[i].reset();
				}
				wantLo[i] = wantHi[i] = -1.f;
				continue;
			}

			float lo = R.lo(), hi = R.hi();
			// A pixel of a three-minute clip is about 3e-4; an eighth of that is
			// below what any drag can express, so this only suppresses the
			// no-op frames rather than real edits.
			static const float EPS = 4e-5f;
			if (wantId[i] == m->id && std::fabs(lo - wantLo[i]) < EPS
				&& std::fabs(hi - wantHi[i]) < EPS)
				continue;

			wantLo[i] = lo;
			wantHi[i] = hi;
			wantId[i] = m->id;

			int64_t s = (int64_t) ((double) lo * (double) m->frames);
			int64_t e = (int64_t) ((double) hi * (double) m->frames);
			if (e <= s)
				e = s + 1;
			if (e > m->frames)
				e = m->frames;
			windows.request(i, m->pcmPath, m->id, s, e - s, lo, hi);
		}
	}

	bool anyRegion() const {
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			if (regions[i].active)
				return true;
		return false;
	}

	/** Fill all eight slots with the biggest windows the budget will pay for,
	    spread evenly from the start of the clip to the end.

	    This used to cut the clip into eight touching eighths, which made the
	    windows a function of the clip's length -- a ten-minute video gave eight
	    seventy-five-second regions whether or not there was memory for them. The
	    budget decides the length now and the clip only decides where they sit,
	    so the same setting behaves the same way on any source. A clip shorter
	    than one window is a windfall: every slot simply gets the whole thing. */
	void resetRegions() {
		rp::Media* m = owner.get();
		double dur = m ? m->duration : 0.0;
		budget.reset(budgetSeconds());
		double w = budget.pool / (double) rp::NUM_SLOTS;
		if (dur > 0.0 && w > dur)
			w = dur;

		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			regions[i].active = true;
			regions[i].enabled = true;
			if (dur > 0.0) {
				double slack = dur - w;
				double at = (rp::NUM_SLOTS > 1)
					? slack * (double) i / (double) (rp::NUM_SLOTS - 1) : 0.0;
				regions[i].start = (float) (at / dur);
				regions[i].end = (float) ((at + w) / dur);
			}
			else {
				regions[i].start = (float) i / rp::NUM_SLOTS;
				regions[i].end = (float) (i + 1) / rp::NUM_SLOTS;
			}
			regions[i].speed = 0.f;
			regions[i].reverse = false;
			regions[i].gain = 1.f;
			regions[i].loop = true;
		}
		lastSel = -1;
	}

	void clearRegions() {
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			regions[i].active = false;
			regions[i].enabled = true;
		}
		lastSel = -1;
	}

	/** One per-step CV value. A polyphonic cable addresses the slots by channel;
	    a monophonic one applies to every slot at once, which is what a single LFO
	    into SPEED should obviously do and what a split cable would otherwise cost
	    eight jacks to say. A cable with fewer channels than slots leaves the
	    slots above it at zero rather than wrapping -- wrapping would make a
	    four-channel cable secretly control eight steps. */
	float polyAt(int inputId, int slot) {
		Input& in = inputs[inputId];
		if (!in.isConnected())
			return 0.f;
		int n = in.getChannels();
		if (n <= 1)
			return in.getVoltage(0);
		return (slot < n) ? in.getVoltage(slot) : 0.f;
	}

	/** The expander's message, or NULL when there is not one attached. Checked
	    by model pointer, which is what Rack's own expanders do: an expander can
	    be deleted between one frame and the next, so nothing may be cached. */
	rp::SchedMessage* schedIn() {
		if (!rightExpander.module || rightExpander.module->model != modelScheduleA)
			return NULL;
		return (rp::SchedMessage*) rightExpander.consumerMessage;
	}

	/** One per-step control, from whichever source is actually driving it.

	    A discrete jack on the expander wins over the host's polyphonic one, but
	    only where a cable is really in it -- so a poly LFO into SPEED can drive
	    all eight steps while one hand-patched envelope takes over step 5, and an
	    empty expander jack overrides nothing. Summing them would make an unused
	    jack a silent zero, which is why `has[]` exists. */
	float stepCv(int which, int slot) {
		static const int JACK[rp::NUM_STEP_CV] =
			{SPEED_INPUT, GAIN_INPUT, START_INPUT, LEN_INPUT};
		rp::SchedMessage* x = schedIn();
		if (x && x->has[which][slot])
			return x->cv[which][slot];
		return polyAt(JACK[which], slot);
	}

	/** The region as the sequencer will actually play it: the stored span with
	    START and LENGTH CV folded in. The stored values are never written by CV,
	    so a modulated window springs back the moment the cable is pulled. */
	void windowOf(int slot, float* loOut, float* hiOut) {
		const Region& R = regions[slot];
		float lo = clamp(R.lo(), 0.f, 1.f);
		float hi = clamp(R.hi(), 0.f, 1.f);
		float len = hi - lo;

		float lv = stepCv(rp::CV_LEN, slot);
		if (lv != 0.f)
			len = clamp(len * std::pow(2.f, clamp(lv, -5.f, 5.f) * 0.2f),
				1e-4f, 1.f);

		float sv = stepCv(rp::CV_START, slot);
		lo = clamp(lo + clamp(sv, -5.f, 5.f) * 0.02f, 0.f, 1.f);
		hi = lo + len;
		if (hi > 1.f) {
			hi = 1.f;
			lo = std::max(0.f, hi - len);
		}
		if (hi - lo < 1e-4f)
			hi = std::min(1.f, lo + 1e-4f);
		*loOut = lo;
		*hiOut = hi;
	}

	// --- the per-region knobs ------------------------------------------------
	// One set of controls edits whichever region is selected, so the knobs are
	// loaded from the region when the selection moves and written back to it on
	// every other sample. Doing it here rather than in the widget means it works
	// with no widget at all -- CV on REGION edits the same way a mouse does.

	void pushToParams(int i) {
		const Region& R = regions[i];
		params[SPEED_PARAM].setValue(clamp(R.speed, -2.f, 2.f));
		params[GAIN_PARAM].setValue(clamp(R.gain, 0.f, 2.f));
		params[LOOP_PARAM].setValue(R.loop ? 1.f : 0.f);
		params[REV_PARAM].setValue(R.reverse ? 1.f : 0.f);
	}

	void pullFromParams(int i) {
		Region& R = regions[i];
		R.speed = params[SPEED_PARAM].getValue();
		R.gain = params[GAIN_PARAM].getValue();
		R.loop = params[LOOP_PARAM].getValue() > 0.5f;
		R.reverse = params[REV_PARAM].getValue() > 0.5f;
	}

	/** The next slot the sequencer would visit. A disabled slot is passed over
	    exactly like an empty one -- that is what disabling is for. */
	int firstActive(int from, int step) const {
		for (int n = 0; n < rp::NUM_SLOTS; n++) {
			int i = from + step * n;
			i = ((i % rp::NUM_SLOTS) + rp::NUM_SLOTS) % rp::NUM_SLOTS;
			if (regions[i].live())
				return i;
		}
		return -1;
	}

	int nextSlot(int cur, int mode) {
		switch (mode) {
			case MODE_RANDOM: {
				int n = 0;
				int pick[rp::NUM_SLOTS];
				for (int i = 0; i < rp::NUM_SLOTS; i++)
					if (regions[i].live())
						pick[n++] = i;
				if (n == 0)
					return cur;
				return pick[(int) std::floor(random::uniform() * n) % n];
			}
			case MODE_PINGPONG: {
				int n = firstActive(cur + pingDir, pingDir);
				// Bounce at whichever end of the active slots we reached.
				if (n < 0)
					return cur;
				if ((pingDir > 0 && n <= cur) || (pingDir < 0 && n >= cur)) {
					pingDir = -pingDir;
					int b = firstActive(cur + pingDir, pingDir);
					if (b >= 0)
						return b;
				}
				return n;
			}
			case MODE_CV:
				return cur;
			default: {
				int n = firstActive(cur + 1, 1);
				return (n < 0) ? cur : n;
			}
		}
	}

	// --- reading the store ---------------------------------------------------

	/** Index within the window, clamped. The interpolator wants a frame either
	    side of the pair it is between, and at a window's first and last frame
	    those do not exist -- but those are exactly the loop points, where the
	    crossfade is already running over the seam. */
	static inline float tap(const rp::Window* w, int64_t i, int ch) {
		if (i < 0)
			i = 0;
		if (i >= w->frames)
			i = w->frames - 1;
		return w->pcm[(size_t) (i * 2 + ch)];
	}

	/** `p` is a position in the *clip*, not in the window: the playhead, POS and
	    the timeline all speak clip frames, and only this function knows that the
	    audio under it lives in a buffer starting somewhere else. */
	static inline void readAt(const rp::Window* w, double p, float* l, float* r) {
		double rel = p - (double) w->startFrame;
		int64_t i1 = (int64_t) std::floor(rel);
		float t = (float) (rel - (double) i1);
		for (int ch = 0; ch < 2; ch++) {
			float y = hermite(tap(w, i1 - 1, ch), tap(w, i1, ch),
				tap(w, i1 + 1, ch), tap(w, i1 + 2, ch), t);
			if (ch == 0)
				*l = y;
			else
				*r = y;
		}
	}

	/** The window the last sample came out of, and the one the crossfade's
	    trailing tap is still reading. A seam between two slots reads two
	    different buffers, so the old one has to be named rather than assumed --
	    it is no longer "the same audio at another offset". */
	rp::Window* curWin = NULL;
	rp::Window* oldWin = NULL;

	void jumpTo(double target, double step) {
		// A discontinuity: keep the old read pointer alive for the crossfade so
		// the seam is a fade rather than an edge.
		if (xfadeLen > 0) {
			oldPos = pos;
			oldStep = step;
			oldWin = curWin;
			xfadeLeft = xfadeLen;
		}
		pos = target;
	}

	void process(const ProcessArgs& args) override {
		tick.fetch_add(1, std::memory_order_relaxed);
		rp::Media* m = active.load(std::memory_order_acquire);

		xfadeLen = (int) (XFADE_MS[clamp(xfadeIndex, 0, 3)] * 0.001f * args.sampleRate);

		int mode = (int) std::round(params[MODE_PARAM].getValue());
		mode = clamp(mode, 0, NUM_MODES - 1);
		int runMode = clamp((int) std::round(params[RUNMODE_PARAM].getValue()),
			0, 2);
		bool running = (runMode != RUN_STOP);

		// selection: the slot buttons and the REGION knob are the same control
		int sel = clamp((int) std::round(params[REGION_PARAM].getValue()),
			0, rp::NUM_SLOTS - 1);

		bool fire = false;

		// The step buttons. A tap selects, and -- when the sequence is not
		// running -- fires the step, so the eight buttons are playable on their
		// own like the pads they are modelled on. Holding one past HOLD_SECONDS
		// loops that step for as long as the finger is down, whatever the
		// region's own LOOP setting says; letting go hands it back.
		static const double HOLD_SECONDS = 0.25;
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			bool down = params[SLOT_PARAM + i].getValue() > 0.5f;
			if (down && !slotDown[i]) {
				params[REGION_PARAM].setValue((float) i);
				sel = i;
				if (!regions[i].active) {
					// An empty slot is a skipped slot; pressing it fills it with
					// an eighth of the clip so there is something to skip.
					regions[i].active = true;
					regions[i].enabled = true;
					regions[i].start = (float) i / rp::NUM_SLOTS;
					regions[i].end = (float) (i + 1) / rp::NUM_SLOTS;
				}
				heldSlot = i;
				heldFor = 0.0;
				heldLooping = false;
				// Latch already loops whatever is selected, and while the
				// sequence runs the clock owns the playhead; in both cases a tap
				// should move the selection without hijacking playback.
				if (runMode == RUN_STOP && regions[i].live()) {
					playSlot = i;
					fire = true;
				}
			}
			else if (!down && slotDown[i] && heldSlot == i) {
				heldSlot = -1;
				heldFor = 0.0;
				heldLooping = false;
			}
			slotDown[i] = down;
		}
		if (heldSlot >= 0) {
			heldFor += args.sampleTime;
			if (heldFor >= HOLD_SECONDS && !heldLooping) {
				heldLooping = true;
				// Take the playhead only if the step is not already the one
				// sounding, so holding the step that is playing does not restart
				// it under the finger.
				if (regions[heldSlot].live() && playSlot != heldSlot) {
					playSlot = heldSlot;
					fire = true;
				}
			}
		}
		bool forceLoop = heldLooping && heldSlot >= 0 && heldSlot == playSlot;
		bool manualPlay = forceLoop || (runMode == RUN_STOP && heldSlot >= 0);

		if (sel != lastSel) {
			lastSel = sel;
			pushToParams(sel);
		}
		else {
			pullFromParams(sel);
		}

		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			int f = firstActive(0, 1);
			playSlot = (f < 0) ? 0 : f;
			pingDir = 1;
			fire = true;
		}

		// REGION CV picks the slot outright whenever it is patched; the clock
		// then re-fires rather than advancing, which is what makes CV-only mode
		// the same code path as every other mode.
		if (inputs[REGION_INPUT].isConnected()) {
			float v = clamp(inputs[REGION_INPUT].getVoltage(), 0.f, 10.f);
			int want = clamp((int) (v * rp::NUM_SLOTS / 10.f), 0, rp::NUM_SLOTS - 1);
			if (want != playSlot) {
				playSlot = want;
				fire = true;
			}
		}

		// The clock. An external one is always the authority: patching CLOCK
		// silences the internal one rather than racing it, and unpatching hands
		// the tempo knob back. Latch advances nothing -- that is what latching
		// is -- but the clock still runs, so unlatching lands in time.
		bool clocked = false;
		internalPulse = false;
		if (inputs[CLOCK_INPUT].isConnected()) {
			clocked = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		}
		else if (runMode != RUN_STOP) {
			double bps = (double) clamp(params[TEMPO_PARAM].getValue(),
				30.f, 300.f) / 60.0;
			clockPhase += (double) args.sampleTime * bps;
			if (clockPhase >= 1.0) {
				clockPhase -= std::floor(clockPhase);
				clocked = true;
			}
			internalPulse = (clockPhase < 0.05);
		}
		else {
			clockPhase = 0.0;
		}
		if (clocked) {
			if (runMode == RUN_LATCH) {
				// Latched: re-fire the step under the playhead, never advance.
				fire = true;
			}
			else {
				if (!inputs[REGION_INPUT].isConnected())
					playSlot = nextSlot(playSlot, mode);
				fire = true;
			}
		}
		if (fireTrig.process(inputs[FIRE_INPUT].getVoltage(), 0.1f, 1.f))
			fire = true;

		// Latched: the selected step is the one that sounds, and it loops. This
		// is the behaviour that used to be an accident of stopping an external
		// clock; here it is a position on a switch.
		if (runMode == RUN_LATCH && regions[sel].live() && playSlot != sel) {
			playSlot = sel;
			fire = true;
		}

		playSlot = clamp(playSlot, 0, rp::NUM_SLOTS - 1);

		// The transport is on while the sequence runs, while it is latched, and
		// while a step button is held with everything else stopped -- the last
		// being what makes the eight buttons playable on their own.
		bool transportOn = running || manualPlay;

		float outL = 0.f, outR = 0.f;
		double posFrac = 0.0;

		// The audio for this step, or NULL while the loader is still reading it.
		// A window that has not arrived is silence, not a stall: the sequence
		// keeps time and the step comes in when it is there.
		rp::Window* w = activeWin[playSlot].load(std::memory_order_acquire);

		if (m && m->playable()) {
			const Region& R = regions[playSlot];
			double n = (double) m->frames;
			float lo, hi;
			windowOf(playSlot, &lo, &hi);
			double s = lo * n;
			double e = hi * n;
			if (e <= s + 1.0)
				e = s + 1.0;

			float oct = R.speed + stepCv(rp::CV_SPEED, playSlot) * 0.4f;
			oct = clamp(oct, -3.f, 3.f);
			double step = std::pow(2.0, (double) oct)
				* (double) m->sampleRate / (double) args.sampleRate;
			if (R.reverse)
				step = -step;

			// A held step loops for as long as it is held, and a latched one
			// loops until unlatched, whatever the region's own LOOP says.
			bool loopNow = R.loop || forceLoop || (runMode == RUN_LATCH);

			if (fire && R.live()) {
				jumpTo(step >= 0.0 ? s : e - 1.0, step);
				playing = transportOn;
			}
			if (!R.live())
				playing = false;

			if (inputs[SCAN_INPUT].isConnected()) {
				// SCAN takes the playhead over: the region becomes a window you
				// are scrubbing rather than something being played through.
				float sc = clamp(inputs[SCAN_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
				pos = s + (e - s) * (double) sc;
				playing = transportOn && R.live();
			}
			else if (playing && transportOn) {
				pos += step;
				if (step >= 0.0 && pos >= e) {
					eorPulse.trigger(1e-3f);
					if (loopNow)
						jumpTo(s + (pos - e), step);
					else {
						pos = e;
						playing = false;
					}
				}
				else if (step < 0.0 && pos <= s) {
					eorPulse.trigger(1e-3f);
					if (loopNow)
						jumpTo(e - (s - pos), step);
					else {
						pos = s;
						playing = false;
					}
				}
			}
			if (!transportOn)
				playing = false;

			pos = clampd(pos, 0.0, n - 1.0);
			posFrac = pos / n;

			if (playing && w && w->frames > 1) {
				curWin = w;
				readAt(w, pos, &outL, &outR);
				if (xfadeLeft > 0 && oldWin && oldWin->frames > 1) {
					float a, b;
					readAt(oldWin, clampd(oldPos, 0.0, n - 1.0), &a, &b);
					oldPos += oldStep;
					float t = (float) xfadeLeft / (float) xfadeLen;
					outL = outL * (1.f - t) + a * t;
					outR = outR * (1.f - t) + b * t;
					xfadeLeft--;
				}
				else {
					xfadeLeft = 0;
				}
				// 5 V doubles, -5 V silences: an exponential law, so a triangle
				// into GAIN reads as a fade rather than as a lump in the middle.
				float g = clamp(R.gain, 0.f, 2.f);
				float gv = stepCv(rp::CV_GAIN, playSlot);
				if (gv != 0.f)
					g = clamp(g * std::pow(2.f, clamp(gv, -5.f, 5.f) * 0.2f),
						0.f, 4.f);
				outL *= g;
				outR *= g;
			}
			else {
				xfadeLeft = 0;
			}
		}
		else {
			playing = false;
			xfadeLeft = 0;
		}

		outputs[OUT_L_OUTPUT].setVoltage(clamp(outL * 5.f, -12.f, 12.f));
		outputs[OUT_R_OUTPUT].setVoltage(clamp(outR * 5.f, -12.f, 12.f));
		outputs[POS_OUTPUT].setVoltage((float) (posFrac * 10.0));
		outputs[GATE_OUTPUT].setVoltage(playing ? 10.f : 0.f);
		outputs[EOR_OUTPUT].setVoltage(eorPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[REGION_OUTPUT].setVoltage((float) playSlot);

		// Per-step audio. One slot sounds at a time, so this is the same signal
		// routed to the channel of whichever step made it -- which is exactly
		// what lets a step have a chain of its own without a mixer guessing.
		float stepMono = clamp((outL + outR) * 2.5f, -12.f, 12.f);
		{
			Output& steps = outputs[STEPS_OUTPUT];
			steps.setChannels(rp::NUM_SLOTS);
			for (int i = 0; i < rp::NUM_SLOTS; i++)
				steps.setVoltage((playing && i == playSlot) ? stepMono : 0.f, i);
		}

		// What SCHEDULE A gets back: the same per-step audio, and the colour each
		// step is wearing, so the expander's row lights match the host's buttons
		// without knowing the palette or which steps are disabled.
		if (rightExpander.module && rightExpander.module->model == modelScheduleA) {
			rp::SchedMessage* out = (rp::SchedMessage*)
				rightExpander.module->leftExpander.producerMessage;
			if (out) {
				for (int i = 0; i < rp::NUM_SLOTS; i++) {
					out->stepOut[i] =
						(playing && i == playSlot) ? stepMono : 0.f;
					NVGcolor c = regions[i].enabled ? slotInk(i) : panel::CLAY;
					float b = 0.f;
					if (regions[i].active) {
						if (i == playSlot && playing)
							b = 1.f;
						else if (i == sel)
							b = 0.45f;
						else
							b = regions[i].enabled ? 0.12f : 0.22f;
					}
					out->ink[i][0] = c.r * b;
					out->ink[i][1] = c.g * b;
					out->ink[i][2] = c.b * b;
				}
				out->hostPresent = true;
				rightExpander.module->leftExpander.requestMessageFlip();
			}
		}

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int i = 0; i < rp::NUM_SLOTS; i++) {
				// Colour says which slot; brightness says what it is doing. A
				// disabled slot goes to CLAY, the family's alert ink, because
				// "still here but skipped" is a state worth seeing across a room.
				NVGcolor c = regions[i].enabled ? slotInk(i) : panel::CLAY;
				float b = 0.f;
				if (regions[i].active) {
					if (i == playSlot && playing)
						b = 1.f;
					else if (i == sel)
						b = 0.45f;
					else
						b = regions[i].enabled ? 0.12f : 0.22f;
				}
				// A step that could not be armed for want of memory blinks, so
				// "nothing happened" is told apart from "you missed the button".
				if (i == uiBlinkSlot.load()) {
					c = panel::CLAY;
					float ph = uiBlinkPhase.load() * 6.f;
					b = ((ph - std::floor(ph)) < 0.5f) ? 1.f : 0.f;
				}
				lights[SLOT_LIGHT + i * 3 + 0].setBrightnessSmooth(c.r * b, dt);
				lights[SLOT_LIGHT + i * 3 + 1].setBrightnessSmooth(c.g * b, dt);
				lights[SLOT_LIGHT + i * 3 + 2].setBrightnessSmooth(c.b * b, dt);
			}
			lights[BUSY_LIGHT].setBrightnessSmooth(uiBusy.load(), dt);
			// The clock light follows whichever clock is actually in charge.
			bool tick = inputs[CLOCK_INPUT].isConnected()
				? (inputs[CLOCK_INPUT].getVoltage() > 1.f) : internalPulse;
			lights[CLOCK_LIGHT].setBrightnessSmooth(tick ? 1.f : 0.f, dt);

			uiPos.store((float) posFrac);
			uiSlot.store(playSlot);
			uiPlaying.store(playing);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		loader.stop();
		active.store(NULL);
		owner.reset();
		retired.reset();
		url = "";
		mediaId = mediaSource = mediaTitle = "";
		mediaIsPath = false;
		clearRegions();
		playSlot = 0;
		pingDir = 1;
		pos = 0.0;
		playing = false;
		xfadeLeft = 0;
		clockPhase = 0.0;
		heldSlot = -1;
		heldFor = 0.0;
		heldLooping = false;
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			slotDown[i] = false;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "url", json_string(url.c_str()));
		json_object_set_new(rootJ, "mediaId", json_string(mediaId.c_str()));
		json_object_set_new(rootJ, "mediaSource", json_string(mediaSource.c_str()));
		json_object_set_new(rootJ, "mediaTitle", json_string(mediaTitle.c_str()));
		json_object_set_new(rootJ, "mediaIsPath", json_boolean(mediaIsPath));
		json_object_set_new(rootJ, "maxIndex", json_integer(maxIndex));
		json_object_set_new(rootJ, "xfadeIndex", json_integer(xfadeIndex));
		json_object_set_new(rootJ, "budgetIndex", json_integer(budgetIndex));
		json_object_set_new(rootJ, "toolDir", json_string(toolDir.c_str()));

		json_t* regionsJ = json_array();
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			const Region& R = regions[i];
			json_t* r = json_object();
			json_object_set_new(r, "active", json_boolean(R.active));
			json_object_set_new(r, "enabled", json_boolean(R.enabled));
			json_object_set_new(r, "start", json_real(R.start));
			json_object_set_new(r, "end", json_real(R.end));
			json_object_set_new(r, "speed", json_real(R.speed));
			json_object_set_new(r, "reverse", json_boolean(R.reverse));
			json_object_set_new(r, "gain", json_real(R.gain));
			json_object_set_new(r, "loop", json_boolean(R.loop));
			json_array_append_new(regionsJ, r);
		}
		json_object_set_new(rootJ, "regions", regionsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (!rootJ)
			return;
		url = jstr(rootJ, "url");
		mediaId = jstr(rootJ, "mediaId");
		mediaSource = jstr(rootJ, "mediaSource");
		mediaTitle = jstr(rootJ, "mediaTitle");
		mediaIsPath = jbool(rootJ, "mediaIsPath", false);
		maxIndex = clamp((int) jint(rootJ, "maxIndex", 1), 0, 3);
		xfadeIndex = clamp((int) jint(rootJ, "xfadeIndex", 2), 0, 3);
		budgetIndex = clamp((int) jint(rootJ, "budgetIndex", 2), 0,
			rp::NUM_BUDGETS - 1);
		toolDir = jstr(rootJ, "toolDir");

		clearRegions();
		json_t* regionsJ = json_object_get(rootJ, "regions");
		if (regionsJ && json_is_array(regionsJ)) {
			size_t i;
			json_t* r;
			json_array_foreach(regionsJ, i, r) {
				if (i >= (size_t) rp::NUM_SLOTS || !json_is_object(r))
					continue;
				Region& R = regions[i];
				R.active = jbool(r, "active", false);
				// Patches saved before steps could be disabled have no such key,
				// and every region in them was armed. Default accordingly.
				R.enabled = jbool(r, "enabled", true);
				R.start = clamp((float) jreal(r, "start", 0.0), 0.f, 1.f);
				R.end = clamp((float) jreal(r, "end", 0.0), 0.f, 1.f);
				R.speed = clamp((float) jreal(r, "speed", 0.0), -2.f, 2.f);
				R.reverse = jbool(r, "reverse", false);
				R.gain = clamp((float) jreal(r, "gain", 1.0), 0.f, 2.f);
				R.loop = jbool(r, "loop", true);
			}
		}
		lastSel = -1;
		// The cache is keyed by id, so a saved patch reopens the same .pcm and
		// .rgba without going near the network. The widget starts it.
		restorePending = !mediaSource.empty();
	}

	// --- tiny json helpers, local so this module pulls in no shared header ---
	static std::string jstr(json_t* o, const char* k) {
		json_t* j = json_object_get(o, k);
		return (j && json_is_string(j)) ? json_string_value(j) : "";
	}
	static int64_t jint(json_t* o, const char* k, int64_t d) {
		json_t* j = json_object_get(o, k);
		return (j && json_is_integer(j)) ? json_integer_value(j) : d;
	}
	static double jreal(json_t* o, const char* k, double d) {
		json_t* j = json_object_get(o, k);
		return (j && json_is_number(j)) ? json_number_value(j) : d;
	}
	static bool jbool(json_t* o, const char* k, bool d) {
		json_t* j = json_object_get(o, k);
		return j ? json_is_true(j) : d;
	}
};


// ---------------------------------------------------------------------------
// The panel's live half. Everything below draws through panel:: and lays itself
// out from the millimetre constants in Panel.hpp, so the artwork and the widget
// cannot disagree about where a field is.
// ---------------------------------------------------------------------------

namespace ui_rp {

/** What the sub-widgets need from the module widget. */
struct SeizeHost {
	virtual ~SeizeHost() {}
	/** The link box echoing what was typed back into the module, so the text
	    survives a patch save without the field owning the state. */
	virtual void setUrl(const std::string& text) = 0;
	virtual void seize(const std::string& source, bool isPath) = 0;
};


/** One of the eight step buttons.

    It carries two gestures beyond the tap and hold the module reads off the
    param: ctrl-click (cmd on a Mac) disables the step, and ctrl-click again
    arms it. Disabling is deliberately not a tap -- a step you are playing with
    one finger must not be silenced by the same finger -- and deliberately not
    the timeline's release notch either, which empties the slot instead of
    parking it. */
struct SlotBezel : VCVLightBezel<panel::RgbLight> {
	Repossession* module = NULL;
	int slot = 0;

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS
			&& e.button == GLFW_MOUSE_BUTTON_LEFT
			&& (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			e.consume(this);
			// Through the module, not the flag: arming a step has to go past
			// the budget, and may be refused when nothing is left in the pool.
			module->setStepEnabled(slot, !module->regions[slot].enabled);
			return;
		}
		VCVLightBezel<panel::RgbLight>::onButton(e);
	}

	/** With no module -- the browser, the library thumbnail -- Rack turns every
	    base colour of a light fully on, and red plus green plus blue is white.
	    Eight white buttons is exactly what the colour coding exists to avoid, so
	    the unlit preview wears each slot's own ink instead. Runs after the base
	    step(), which is what walks the children and sets them all on. */
	void step() override {
		VCVLightBezel<panel::RgbLight>::step();
		if (!module && light) {
			NVGcolor c = slotInk(slot);
			std::vector<float> b;
			b.push_back(c.r);
			b.push_back(c.g);
			b.push_back(c.b);
			light->setBrightnesses(b);
		}
	}
};


/** A dark pill with centred text that runs a callback when clicked. */
struct FlatButton : widget::OpaqueWidget {
	std::string text;
	std::function<void()> action;
	bool hovered = false;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.2f);
		nvgFillColor(args.vg, panel::alpha(panel::LIME, hovered ? 0.22f : 0.10f));
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			panel::TextStyle st(panel::Face::Ui, 8.5f, panel::LIME,
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
			panel::text(args.vg, st, box.size.x / 2, box.size.y / 2 + 0.5f, text);
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			if (action)
				action();
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onEnter(const EnterEvent& e) override { hovered = true; OpaqueWidget::onEnter(e); }
	void onLeave(const LeaveEvent& e) override { hovered = false; OpaqueWidget::onLeave(e); }
};


/** The link box. Like PatchAudit's search field it deliberately does not grab
    focus by itself: a panel that swallowed every keystroke in Rack would be a
    menace. Enter seizes. */
struct UrlField : ui::TextField {
	SeizeHost* host = NULL;

	UrlField() { placeholder = "Paste a link, or a file path"; }

	void draw(const DrawArgs& args) override {
		static const panel::TextStyle ENTRY(panel::Face::Mono, 11.f, panel::PAPER,
			NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		bool focused = APP->event->getSelectedWidget() == this;
		if (focused) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.f);
			nvgStrokeColor(args.vg, panel::LIME);
			nvgStrokeWidth(args.vg, 0.9f);
			nvgStroke(args.vg);
		}
		panel::TextStyle st = ENTRY.inked(
			text.empty() ? panel::alpha(panel::SAGE, 0.45f) : panel::PAPER);
		nvgScissor(args.vg, 3.f, 0.f, box.size.x - 6.f, box.size.y);
		const float x = 6.f;
		panel::text(args.vg, st, x, box.size.y / 2, text.empty() ? placeholder : text);
		if (focused && !text.empty()) {
			int c = clamp(cursor, 0, (int) text.size());
			float cx = x + panel::textWidth(args.vg, ENTRY, text.substr(0, (size_t) c));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, cx, 3.f, 1.f, box.size.y - 6.f);
			nvgFillColor(args.vg, panel::LIME);
			nvgFill(args.vg);
		}
		nvgResetScissor(args.vg);
	}

	void onChange(const ChangeEvent& e) override {
		if (host)
			host->setUrl(text);
		TextField::onChange(e);
	}

	void onAction(const ActionEvent& e) override {
		if (host)
			host->seize(text, false);
		TextField::onAction(e);
	}
};


/** The screen. One fread per changed frame, on the UI thread at draw time, into
    a buffer that is uploaded to a single nanovg image -- geometry and a texture,
    which is all Rack's renderer needs to be given. */
struct VideoScreen : widget::Widget {
	Repossession* module = NULL;
	FILE* file = NULL;
	std::string openPath;
	std::vector<uint8_t> frame;
	int frameIndex = -1;
	int image = -1;
	NVGcontext* imageVg = NULL;

	VideoScreen() { frame.resize(rp::FRAME_BYTES, 0); }

	~VideoScreen() override {
		if (file)
			std::fclose(file);
		// Rack tears the window down *before* the scene
		// (Rack/src/context.cpp:19 vs :27), so on quit this destructor runs
		// with `imageVg` already pointing at a freed NVGcontext -- deleting
		// the image there segfaults inside glnvg__renderDeleteTexture. The
		// window outliving us is the only case where the handle is still
		// ours to free; when it does not, the GL context is gone and the
		// texture with it.
		if (image >= 0 && imageVg && APP->window
				&& (imageVg == APP->window->vg || imageVg == APP->window->fbVg))
			nvgDeleteImage(imageVg, image);
	}

	/** Returns true if `frame` holds the wanted picture. */
	bool fetchFrame(const rp::Media* m, int want) {
		if (!m || m->videoFrames <= 0)
			return false;
		if (openPath != m->rgbaPath) {
			if (file) {
				std::fclose(file);
				file = NULL;
			}
			openPath = m->rgbaPath;
			file = std::fopen(openPath.c_str(), "rb");
			frameIndex = -1;
		}
		if (!file)
			return false;
		want = clamp(want, 0, m->videoFrames - 1);
		if (want == frameIndex)
			return true;
		if (std::fseek(file, (long) ((int64_t) want * (int64_t) rp::FRAME_BYTES),
				SEEK_SET) != 0)
			return false;
		if (std::fread(&frame[0], 1, rp::FRAME_BYTES, file) != rp::FRAME_BYTES)
			return false;
		frameIndex = want;
		return true;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		const rp::Media* m = module ? module->owner.get() : NULL;
		if (m && m->videoFrames > 0) {
			int want = (int) (module->uiPos.load() * m->duration * rp::FRAME_FPS);
			if (fetchFrame(m, want)) {
				if (image < 0) {
					image = nvgCreateImageRGBA(args.vg, rp::FRAME_W, rp::FRAME_H,
						0, &frame[0]);
					imageVg = args.vg;
				}
				else if (imageVg == args.vg) {
					nvgUpdateImage(args.vg, image, &frame[0]);
				}
				if (image >= 0) {
					NVGpaint p = nvgImagePattern(args.vg, 0.f, 0.f,
						box.size.x, box.size.y, 0.f, image, 1.f);
					nvgBeginPath(args.vg);
					nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
					nvgFillPaint(args.vg, p);
					nvgFill(args.vg);
				}
			}
		}
		else {
			panel::TextStyle st(panel::Face::Mono, 9.f,
				panel::alpha(panel::SAGE, 0.55f),
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(args.vg, st, box.size.x / 2, box.size.y / 2,
				m ? "NO PICTURE" : "NO PROPERTY SEIZED");
		}
		Widget::drawLayer(args, layer);
	}
};


/** The schedule of seized spans. Waveform underneath, regions over it; drag in
    empty space to take a new one, drag an edge to trim, drag the middle to move,
    click the notch at the top of a span to release it. */
struct TimelineStrip : widget::OpaqueWidget {
	Repossession* module = NULL;

	enum DragMode { DRAG_NONE, DRAG_START, DRAG_END, DRAG_MOVE };
	int dragMode = DRAG_NONE;
	int dragSlot = -1;
	float dragX = 0.f;
	float grabLo = 0.f, grabHi = 0.f;

	/** The visible slice of the clip. Rack's own zoom enlarges the whole panel,
	    which runs out long before a region boundary can be placed to the frame
	    on a three-minute clip; this magnifies the strip alone. Scroll to zoom
	    about the pointer, shift-scroll to pan. */
	float viewLo = 0.f, viewHi = 1.f;
	/** About a fifth of a second of a three-minute clip across the whole strip.
	    Far enough in that the limit is the mouse, not the view. */
	static constexpr float MIN_SPAN = 0.001f;

	bool zoomed() const { return viewHi - viewLo < 0.999f; }

	float toFrac(float x) const {
		return clamp(viewLo + (x / box.size.x) * (viewHi - viewLo), 0.f, 1.f);
	}
	float toPx(float f) const {
		float span = viewHi - viewLo;
		if (span < 1e-6f)
			span = 1e-6f;
		return (f - viewLo) / span * box.size.x;
	}

	/** Keeps the window inside the clip without changing how much of it is
	    shown, so panning against either end slides rather than squeezes. */
	void clampView() {
		float span = clamp(viewHi - viewLo, MIN_SPAN, 1.f);
		if (viewLo < 0.f)
			viewLo = 0.f;
		if (viewLo + span > 1.f)
			viewLo = 1.f - span;
		viewHi = viewLo + span;
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!module) {
			OpaqueWidget::onHoverScroll(e);
			return;
		}
		e.consume(this);
		float span = viewHi - viewLo;
		if ((APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			viewLo -= e.scrollDelta.y / 200.f * span;
			clampView();
			return;
		}
		// Zoom about the pointer, so the frame under the cursor stays under it.
		float at = clamp(e.pos.x / box.size.x, 0.f, 1.f);
		float anchor = viewLo + at * span;
		float next = clamp(span * std::pow(1.25f, -e.scrollDelta.y / 30.f),
			MIN_SPAN, 1.f);
		viewLo = anchor - at * next;
		viewHi = viewLo + next;
		clampView();
	}

	void fitAll() {
		viewLo = 0.f;
		viewHi = 1.f;
	}

	bool inHandle(int i, Vec p) const {
		const Repossession::Region& R = module->regions[i];
		float x0 = toPx(R.lo()), x1 = toPx(R.hi());
		if (x1 - x0 < 14.f)
			return false;
		float cx = (x0 + x1) / 2.f;
		return p.y <= 5.f && p.x >= cx - 3.f && p.x <= cx + 3.f;
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS
			|| e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		e.consume(this);
		dragMode = DRAG_NONE;
		dragSlot = -1;
		dragX = e.pos.x;
		float f = toFrac(e.pos.x);

		// The release notch first: it sits inside the span it deletes.
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			if (module->regions[i].active && inHandle(i, e.pos)) {
				module->regions[i].active = false;
				return;
			}
		}
		// Then the edges, then the body, nearest slot first.
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			Repossession::Region& R = module->regions[i];
			if (!R.active)
				continue;
			float x0 = toPx(R.lo()), x1 = toPx(R.hi());
			if (std::fabs(e.pos.x - x0) <= 3.f) {
				select(i);
				dragSlot = i;
				dragMode = DRAG_START;
				return;
			}
			if (std::fabs(e.pos.x - x1) <= 3.f) {
				select(i);
				dragSlot = i;
				dragMode = DRAG_END;
				return;
			}
		}
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			Repossession::Region& R = module->regions[i];
			if (!R.active)
				continue;
			if (f >= R.lo() && f <= R.hi()) {
				select(i);
				dragSlot = i;
				dragMode = DRAG_MOVE;
				grabLo = f - R.lo();
				grabHi = R.hi() - f;
				return;
			}
		}
		// Empty space: seize a new span in the first free slot.
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			if (module->regions[i].active)
				continue;
			Repossession::Region& R = module->regions[i];
			R.active = true;
			R.enabled = true;
			R.start = f;
			// A new span opens at a pixel or so of whatever is on screen, not of
			// the whole clip: zoomed in, a fixed fraction would be born already
			// wider than the view and impossible to trim by dragging.
			R.end = std::min(1.f, f + (viewHi - viewLo) * 0.004f);
			R.speed = module->params[Repossession::SPEED_PARAM].getValue();
			R.gain = module->params[Repossession::GAIN_PARAM].getValue();
			R.loop = module->params[Repossession::LOOP_PARAM].getValue() > 0.5f;
			R.reverse = module->params[Repossession::REV_PARAM].getValue() > 0.5f;
			select(i);
			dragSlot = i;
			dragMode = DRAG_END;
			return;
		}
	}

	void select(int i) {
		module->params[Repossession::REGION_PARAM].setValue((float) i);
	}

	void onDragMove(const DragMoveEvent& e) override {
		OpaqueWidget::onDragMove(e);
		if (!module || dragMode == DRAG_NONE || dragSlot < 0)
			return;
		float zoom = getAbsoluteZoom();
		if (zoom <= 0.f)
			zoom = 1.f;
		dragX += e.mouseDelta.x / zoom;
		float f = toFrac(dragX);
		Repossession::Region& R = module->regions[dragSlot];
		switch (dragMode) {
			case DRAG_START:
				R.start = f;
				break;
			case DRAG_END:
				R.end = f;
				break;
			case DRAG_MOVE: {
				float w = grabLo + grabHi;
				float lo = clamp(f - grabLo, 0.f, 1.f - w);
				R.start = lo;
				R.end = lo + w;
				break;
			}
			default:
				break;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		OpaqueWidget::onDragEnd(e);
		if (module && dragSlot >= 0) {
			Repossession::Region& R = module->regions[dragSlot];
			float lo = R.lo(), hi = R.hi();
			// A span dragged to nothing is a span the user meant to abandon.
			// The threshold follows the zoom for the same reason the birth width
			// does: at 100x, 0.002 of the clip is most of the strip.
			if (hi - lo < (viewHi - viewLo) * 0.002f)
				R.active = false;
			else {
				R.start = lo;
				R.end = hi;
			}
		}
		dragMode = DRAG_NONE;
		dragSlot = -1;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		const rp::Media* m = module ? module->owner.get() : NULL;
		float w = box.size.x, h = box.size.y;

		if (m && !m->peaks.empty()) {
			// One bar per pixel column of the strip, each reading the bucket the
			// current view puts under it. Zoomed in, neighbouring columns land in
			// the same bucket and the waveform simply gets blockier -- which is
			// honest: 1024 buckets is all the detail there is.
			int bars = (int) std::max(1.f, std::floor(w));
			float span = viewHi - viewLo;
			nvgBeginPath(args.vg);
			for (int i = 0; i < bars; i++) {
				float f = viewLo + ((float) i + 0.5f) / bars * span;
				int b = clamp((int) (f * rp::PEAK_BUCKETS), 0,
					rp::PEAK_BUCKETS - 1);
				float p = m->peaks[(size_t) b];
				float bh = std::max(0.6f, p * (h - 2.f));
				nvgRect(args.vg, (float) i * w / bars, (h - bh) / 2.f,
					std::max(0.7f, w / bars - 0.3f), bh);
			}
			nvgFillColor(args.vg, panel::alpha(panel::SAGE, 0.55f));
			nvgFill(args.vg);
		}
		else {
			panel::TextStyle st(panel::Face::Mono, 7.5f,
				panel::alpha(panel::SAGE, 0.4f),
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(args.vg, st, w / 2, h / 2, "NO SCHEDULE OF ASSETS");
		}

		if (!module)
			return;

		int sel = clamp((int) std::round(
			module->params[Repossession::REGION_PARAM].getValue()),
			0, rp::NUM_SLOTS - 1);
		int live = module->uiSlot.load();
		bool playing = module->uiPlaying.load();

		nvgScissor(args.vg, 0.f, 0.f, w, h);
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			const Repossession::Region& R = module->regions[i];
			if (!R.active)
				continue;
			float x0 = toPx(R.lo()), x1 = toPx(R.hi());
			if (x1 < -2.f || x0 > w + 2.f)
				continue;
			bool isLive = (i == live && playing);
			// The same function the step lights use, so a span and its button
			// can never end up different colours. Disabled goes to CLAY, which
			// is what the light does too.
			NVGcolor c = R.enabled ? slotInk(i) : panel::CLAY;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 0.f, std::max(1.f, x1 - x0), h);
			nvgFillColor(args.vg, panel::alpha(c,
				isLive ? 0.42f : (R.enabled ? 0.16f : 0.10f)));
			nvgFill(args.vg);

			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 0.f, std::max(1.f, x1 - x0), h);
			nvgStrokeColor(args.vg, panel::alpha(c, i == sel ? 0.95f : 0.45f));
			nvgStrokeWidth(args.vg, i == sel ? 1.2f : 0.7f);
			nvgStroke(args.vg);

			// A disabled span is hatched as well as recoloured: colour alone is
			// the one channel a person may not have.
			if (!R.enabled && x1 - x0 > 2.f) {
				nvgSave(args.vg);
				nvgScissor(args.vg, x0, 0.f, x1 - x0, h);
				nvgBeginPath(args.vg);
				for (float d = x0 - h; d < x1 + h; d += 3.2f) {
					nvgMoveTo(args.vg, d, h);
					nvgLineTo(args.vg, d + h, 0.f);
				}
				nvgStrokeColor(args.vg, panel::alpha(panel::CLAY, 0.45f));
				nvgStrokeWidth(args.vg, 0.6f);
				nvgStroke(args.vg);
				nvgRestore(args.vg);
			}

			if (x1 - x0 >= 14.f) {
				float cx = (x0 + x1) / 2.f;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, cx - 3.f, 0.f, 6.f, 2.2f);
				nvgFillColor(args.vg, panel::alpha(panel::CLAY, 0.85f));
				nvgFill(args.vg);
			}
		}

		float pf = module->uiPos.load();
		if (pf >= viewLo && pf <= viewHi) {
			float px = toPx(pf);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, px - 0.5f, 0.f, 1.f, h);
			nvgFillColor(args.vg, panel::PAPER);
			nvgFill(args.vg);
		}
		nvgResetScissor(args.vg);

		// Where the view sits in the whole clip, drawn only when it is not the
		// whole clip. Without it a zoomed strip is a waveform with no address.
		if (zoomed()) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, h - 1.f, w, 1.f);
			nvgFillColor(args.vg, panel::alpha(panel::SAGE, 0.20f));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, viewLo * w, h - 1.f,
				std::max(1.5f, (viewHi - viewLo) * w), 1.f);
			nvgFillColor(args.vg, panel::alpha(panel::LIME, 0.85f));
			nvgFill(args.vg);
		}
	}
};


/** The whole read-out well: it owns the fields and draws the report column. */
struct RepossessionDisplay : widget::Widget {
	Repossession* module = NULL;
	SeizeHost* host = NULL;
	UrlField* urlField = NULL;
	VideoScreen* screen = NULL;
	TimelineStrip* strip = NULL;
	std::string status;
	bool statusIsError = false;

	RepossessionDisplay(Rect boxIn, Repossession* module, SeizeHost* host)
		: module(module), host(host) {
		box = boxIn;

		screen = new VideoScreen;
		screen->module = module;
		screen->box.pos = panel::mm(panel::VID_X, panel::VID_Y);
		screen->box.size = panel::mm(panel::VID_W, panel::VID_H);
		addChild(screen);

		const float btnW = 17.0f;    // mm
		urlField = new UrlField;
		urlField->host = host;
		urlField->box.pos = panel::mm(panel::URL_X + 0.6f, panel::URL_Y + 0.5f);
		urlField->box.size = panel::mm(panel::URL_W - btnW - 2.0f, panel::URL_H - 1.0f);
		addChild(urlField);

		FlatButton* seize = new FlatButton;
		seize->text = "SEIZE";
		seize->box.pos = panel::mm(panel::URL_X + panel::URL_W - btnW - 0.6f,
			panel::URL_Y + 0.7f);
		seize->box.size = panel::mm(btnW, panel::URL_H - 1.4f);
		SeizeHost* h = host;
		UrlField* f = urlField;
		if (host) {
			seize->action = [h, f]() { h->seize(f->text, false); };
		}
		addChild(seize);

		strip = new TimelineStrip;
		strip->module = module;
		strip->box.pos = panel::mm(panel::TL_X, panel::TL_Y);
		strip->box.size = panel::mm(panel::TL_W, panel::TL_H);
		addChild(strip);
	}

	static std::string timeOf(double seconds) {
		int t = (int) seconds;
		return string::f("%d:%02d", t / 60, t % 60);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		float x = panel::mm(panel::INFO_X + 2.4f, 0.f).x;
		float w = panel::mm(panel::INFO_W - 4.8f, 0.f).x;
		float y = panel::mm(0.f, panel::INFO_Y + 4.2f).y;
		float lead = panel::mm(0.f, 4.0f).y;

		panel::TextStyle head(panel::Face::Ui, 8.4f, panel::PAPER,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 0.4f);
		panel::TextStyle body(panel::Face::Mono, 8.0f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		nvgScissor(args.vg, x - 2.f, panel::mm(0.f, panel::INFO_Y).y - 1.f,
			w + 4.f, panel::mm(0.f, panel::INFO_H).y);

		if (!module) {
			panel::text(args.vg, head, x, y, "SECURED PROPERTY");
			panel::text(args.vg, body, x, y + lead,
				"Paste a link. yt-dlp and ffmpeg");
			panel::text(args.vg, body, x, y + 2 * lead,
				"do the seizing; the panel does");
			panel::text(args.vg, body, x, y + 3 * lead,
				"the sequencing.");
			nvgResetScissor(args.vg);
			Widget::drawLayer(args, layer);
			return;
		}

		const rp::Media* m = module->owner.get();

		std::string title = m ? m->title : module->mediaTitle;
		if (title.empty())
			title = "NO PROPERTY ON FILE";
		panel::text(args.vg, head, x, y, fitted.get(args.vg, head, title, w));

		if (m) {
			panel::text(args.vg, body, x, y + lead,
				string::f("%s  %d Hz  %d frames", timeOf(m->duration).c_str(),
					m->sampleRate, m->videoFrames));
		}
		else {
			panel::text(args.vg, body, x, y + lead, "--:--");
		}

		int sel = clamp((int) std::round(
			module->params[Repossession::REGION_PARAM].getValue()),
			0, rp::NUM_SLOTS - 1);
		const Repossession::Region& R = module->regions[sel];
		double dur = m ? m->duration : 0.0;
		if (R.active) {
			// The selected step is drawn in its own colour, which is how the
			// line ties itself to a button and a span without naming either.
			NVGcolor ink = R.enabled ? slotInk(sel) : panel::CLAY;
			panel::text(args.vg, body.inked(ink), x, y + 2 * lead,
				string::f("R%d  %s - %s  %s%.2fx  %d%%%s%s", sel + 1,
					timeOf(R.lo() * dur).c_str(), timeOf(R.hi() * dur).c_str(),
					R.reverse ? "-" : "", std::pow(2.f, R.speed),
					(int) (R.gain * 100.f + 0.5f), R.loop ? "  LOOP" : "",
					R.enabled ? "" : "  DISABLED"));
		}
		else {
			panel::text(args.vg, body.inked(panel::alpha(panel::SAGE, 0.6f)),
				x, y + 2 * lead, string::f("R%d  released", sel + 1));
		}

		// The memory meter. The number is measured off the buffers rather than
		// inferred from the grants, because the granted seconds are a promise
		// and the resident bytes are the fact.
		{
			double mb = (double) module->residentBytes() / (1024.0 * 1024.0);
			int cap = rp::BUDGET_MB[clamp(module->budgetIndex, 0,
				rp::NUM_BUDGETS - 1)];
			double frac = (cap > 0) ? clampd(mb / (double) cap, 0.0, 1.0) : 0.0;
			// Clay once the budget is nearly gone: the point of a meter is to be
			// read before it matters, not after.
			NVGcolor c = (frac > 0.92) ? panel::CLAY : panel::SAGE;
			panel::text(args.vg, body.inked(c), x, y + 3 * lead,
				string::f("MEM %.1f / %d MB   %.1fs free", mb, cap,
					module->budget.free));

			float bx = x;
			float bw = w;
			float by = panel::mm(0.f, panel::INFO_Y + 17.6f).y;
			float bh = panel::mm(0.f, 0.9f).y;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, bx, by, bw, bh);
			nvgFillColor(args.vg, panel::alpha(panel::SAGE, 0.16f));
			nvgFill(args.vg);
			// One segment per slot, each as wide as that slot's share of the
			// budget, so the bar says who is holding what and not merely how
			// much is gone.
			float at = bx;
			for (int i = 0; i < rp::NUM_SLOTS; i++) {
				if (!module->winOwner[i])
					continue;
				double share = (cap > 0)
					? (double) module->winOwner[i]->bytes()
						/ ((double) cap * 1024.0 * 1024.0) : 0.0;
				float sw = (float) (share * bw);
				if (sw < 0.5f)
					sw = 0.5f;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, at, by, std::max(0.f, sw - 0.6f), bh);
				nvgFillColor(args.vg, panel::alpha(
					module->regions[i].enabled ? slotInk(i) : panel::CLAY, 0.8f));
				nvgFill(args.vg);
				at += sw;
				if (at > bx + bw)
					break;
			}
		}

		if (!status.empty()) {
			NVGcolor c = statusIsError ? panel::CLAY : panel::SAGE;
			panel::text(args.vg, body.inked(c), x, y + 4.2f * lead,
				fittedStatus.get(args.vg, body, status, w));
		}

		nvgResetScissor(args.vg);
		Widget::drawLayer(args, layer);
	}

	panel::FittedText fitted;
	panel::FittedText fittedStatus;
};

} // namespace ui_rp


struct RepossessionWidget : ModuleWidget, ui_rp::SeizeHost {
	Repossession* mod = NULL;
	ui_rp::RepossessionDisplay* display = NULL;

	RepossessionWidget(Repossession* module) {
		setModule(module);
		mod = module;
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Repossession.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		display = new ui_rp::RepossessionDisplay(Rect(Vec(0, 0), box.size),
			module, module ? this : NULL);
		addChild(display);
		if (module) {
			display->urlField->text = module->url;
			display->urlField->cursor = (int) module->url.size();
			display->urlField->selection = display->urlField->cursor;
		}

		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			ui_rp::SlotBezel* b = createLightParamCentered<ui_rp::SlotBezel>(
				panel::mm(slotPos()[i].x, slotPos()[i].y), module,
				Repossession::SLOT_PARAM + i,
				Repossession::SLOT_LIGHT + i * 3);
			b->module = module;
			b->slot = i;
			addParam(b);
		}

		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::REGION_POS.x, panel::REGION_POS.y), module,
			Repossession::REGION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::SPEED_POS.x, panel::SPEED_POS.y), module,
			Repossession::SPEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::GAIN_POS.x, panel::GAIN_POS.y), module,
			Repossession::GAIN_PARAM));
		addParam(createParamCentered<CKSS>(
			panel::mm(panel::LOOP_POS.x, panel::LOOP_POS.y), module,
			Repossession::LOOP_PARAM));
		addParam(createParamCentered<CKSS>(
			panel::mm(panel::REV_POS.x, panel::REV_POS.y), module,
			Repossession::REV_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::MODE_POS.x, panel::MODE_POS.y), module,
			Repossession::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::TEMPO_POS.x, panel::TEMPO_POS.y), module,
			Repossession::TEMPO_PARAM));
		addParam(createParamCentered<CKSSThree>(
			panel::mm(panel::RUNMODE_POS.x, panel::RUNMODE_POS.y), module,
			Repossession::RUNMODE_PARAM));

		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module,
			Repossession::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module,
			Repossession::RESET_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::REGION_IN_POS.x, panel::REGION_IN_POS.y), module,
			Repossession::REGION_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::SCAN_IN_POS.x, panel::SCAN_IN_POS.y), module,
			Repossession::SCAN_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::FIRE_IN_POS.x, panel::FIRE_IN_POS.y), module,
			Repossession::FIRE_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::SPEED_IN_POS.x, panel::SPEED_IN_POS.y), module,
			Repossession::SPEED_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::GAIN_IN_POS.x, panel::GAIN_IN_POS.y), module,
			Repossession::GAIN_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::START_IN_POS.x, panel::START_IN_POS.y), module,
			Repossession::START_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::LEN_IN_POS.x, panel::LEN_IN_POS.y), module,
			Repossession::LEN_INPUT));

		addOutput(createOutputCentered<panel::PortOutMain>(
			panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module,
			Repossession::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(
			panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module,
			Repossession::OUT_R_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::STEPS_OUT_POS.x, panel::STEPS_OUT_POS.y), module,
			Repossession::STEPS_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::POS_OUT_POS.x, panel::POS_OUT_POS.y), module,
			Repossession::POS_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::GATE_OUT_POS.x, panel::GATE_OUT_POS.y), module,
			Repossession::GATE_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::EOR_OUT_POS.x, panel::EOR_OUT_POS.y), module,
			Repossession::EOR_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::REG_OUT_POS.x, panel::REG_OUT_POS.y), module,
			Repossession::REGION_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::ClayLight> >(
			panel::mm(panel::BUSY_POS.x, panel::BUSY_POS.y), module,
			Repossession::BUSY_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
			panel::mm(panel::CLOCK_LED_POS.x, panel::CLOCK_LED_POS.y), module,
			Repossession::CLOCK_LIGHT));
	}

	/** The eight slot positions, so the constructor above can loop. */
	static const Vec* slotPos() {
		static const Vec p[rp::NUM_SLOTS] = {
			panel::SLOT1_POS, panel::SLOT2_POS, panel::SLOT3_POS, panel::SLOT4_POS,
			panel::SLOT5_POS, panel::SLOT6_POS, panel::SLOT7_POS, panel::SLOT8_POS,
		};
		return p;
	}

	// --- SeizeHost -----------------------------------------------------------

	void setUrl(const std::string& text) override {
		if (mod)
			mod->url = text;
	}

	void seize(const std::string& source, bool isPath) override {
		if (!mod)
			return;
		std::string s = source;
		// A path typed into the link box is still a path.
		if (!isPath && !s.empty() && s.find("://") == std::string::npos)
			isPath = true;
		if (s.empty())
			return;
		mod->url = source;
		mod->requestLoad(s, isPath, "", APP->engine->getSampleRate());
	}

	// --- frame loop ----------------------------------------------------------

	void step() override {
		ModuleWidget::step();
		if (!mod || !display)
			return;

		if (mod->restorePending) {
			mod->restorePending = false;
			// The cache is keyed by id, so this reopens the same .pcm and .rgba
			// without going near the network unless they have been cleared.
			mod->requestLoad(mod->mediaSource, mod->mediaIsPath, mod->mediaId,
				APP->engine->getSampleRate());
			display->urlField->text = mod->url;
			display->urlField->cursor = (int) mod->url.size();
			display->urlField->selection = display->urlField->cursor;
		}

		if (mod->loader.dirty.exchange(false)) {
			display->status = mod->loader.message();
			display->statusIsError = (mod->loader.phase.load() == rp::PHASE_FAILED);
			std::shared_ptr<rp::Media> m = mod->loader.take();
			if (m)
				mod->adopt(m);
		}
		// The order matters: hold the regions to their grants first, then ask
		// for whatever windows the result needs, so a drag never queues a read
		// for a length the budget was about to refuse.
		mod->reconcileBudget();
		mod->reconcileWindows();
		mod->releaseRetired();

		// The refused-arming blink: elapsed seconds, counted here and turned into
		// on/off in process(), so the audio thread owns no timers of its own.
		if (mod->uiBlinkSlot.load() >= 0) {
			float t = mod->uiBlinkPhase.load()
				+ (float) APP->window->getLastFrameDuration();
			if (t >= 1.5f) {
				mod->uiBlinkSlot.store(-1);
				mod->uiBlinkPhase.store(0.f);
			}
			else {
				mod->uiBlinkPhase.store(t);
			}
		}

		int ph = mod->loader.phase.load();
		float busy = 0.f;
		if (ph == rp::PHASE_FAILED)
			busy = 1.f;
		else if (ph > rp::PHASE_IDLE && ph < rp::PHASE_READY)
			busy = 0.35f + 0.65f * std::fabs(std::sin((float) system::getTime() * 3.f));
		mod->uiBusy.store(busy);
	}

	// --- context menu --------------------------------------------------------

	void appendContextMenu(Menu* menu) override {
		if (!mod)
			return;
		RepossessionWidget* self = this;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Repossession"));

		menu->addChild(createMenuItem("Load media file...", "", [self]() {
			osdialog_filters* filters = osdialog_filters_parse(
				"Media:mp4,mkv,webm,mov,avi,m4v,wav,flac,mp3,m4a,ogg,aiff,aif");
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
			osdialog_filters_free(filters);
			if (path) {
				std::string p = path;
				std::free(path);
				self->display->urlField->text = p;
				self->display->urlField->cursor = (int) p.size();
				self->display->urlField->selection = self->display->urlField->cursor;
				self->mod->url = p;
				self->seize(p, true);
			}
		}));

		menu->addChild(createMenuItem("Re-import", "", [self]() {
			if (!self->mod->mediaSource.empty()) {
				self->mod->requestLoad(self->mod->mediaSource,
					self->mod->mediaIsPath, self->mod->mediaId,
					APP->engine->getSampleRate());
			}
		}));

		menu->addChild(createIndexSubmenuItem("Maximum import length",
			{"1 minute", "3 minutes", "6 minutes", "10 minutes"},
			[self]() { return self->mod->maxIndex; },
			[self](int i) { self->mod->maxIndex = clamp(i, 0, 3); }));

		menu->addChild(createIndexSubmenuItem("Memory for windows",
			{"16 MB", "32 MB", "64 MB", "128 MB", "256 MB", "512 MB"},
			[self]() { return self->mod->budgetIndex; },
			[self](int i) {
				self->mod->budgetIndex = clamp(i, 0, rp::NUM_BUDGETS - 1);
				// Shares keep their proportions, so a patch that has been shaped
				// survives being given more room -- or less.
				self->mod->budget.rescale(self->mod->budgetSeconds());
			}));

		menu->addChild(createIndexSubmenuItem("Edge crossfade",
			{"off", "2 ms", "4 ms", "10 ms"},
			[self]() { return self->mod->xfadeIndex; },
			[self](int i) { self->mod->xfadeIndex = clamp(i, 0, 3); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Regions"));
		menu->addChild(createMenuItem("Eight equal spans", "",
			[self]() { self->mod->resetRegions(); }));
		menu->addChild(createMenuItem("Release all", "",
			[self]() { self->mod->clearRegions(); }));
		menu->addChild(createMenuItem("Arm every step", "", [self]() {
			for (int i = 0; i < rp::NUM_SLOTS; i++)
				self->mod->setStepEnabled(i, true);
		}));
		// The gesture is ctrl-click on the step button; the menu says so,
		// because a modifier nobody is told about is a feature nobody has.
		menu->addChild(createMenuItem("Disable the selected step",
			RACK_MOD_CTRL_NAME "+click a step", [self]() {
				int i = clamp((int) std::round(
					self->mod->params[Repossession::REGION_PARAM].getValue()),
					0, rp::NUM_SLOTS - 1);
				self->mod->setStepEnabled(i, !self->mod->regions[i].enabled);
			}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Timeline"));
		menu->addChild(createMenuItem("Fit the whole clip", "scroll to zoom",
			[self]() { self->display->strip->fitAll(); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tools"));
		// The folder is remembered plugin-wide (MoonTechnologies/settings.json in
		// Rack's user directory) as well as in this module's own state, so the
		// next Repossession in the next patch already knows where the tools are.
		std::string current = self->mod->toolDir.empty() ? rp::loadGlobalToolDir()
		                                                : self->mod->toolDir;
		menu->addChild(createMenuItem("Tools folder...",
			current.empty() ? "auto" : current, [self]() {
				char* dir = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
				if (dir) {
					self->mod->toolDir = dir;
					rp::saveGlobalToolDir(dir);
					std::free(dir);
				}
			}));
		menu->addChild(createMenuItem("Clear tools folder", "", [self]() {
			self->mod->toolDir = "";
			rp::saveGlobalToolDir("");
		}));
		menu->addChild(createMenuItem("Open drop folder for tools", "", []() {
			std::string d = asset::user("MoonTechnologies/tools");
			system::createDirectories(d);
			system::openDirectory(d);
		}));
		menu->addChild(createMenuItem("Open cache folder", "", []() {
			system::createDirectories(rp::cacheDir());
			system::openDirectory(rp::cacheDir());
		}));
	}
};


Model* modelRepossession = createModel<Repossession, RepossessionWidget>("Repossession");
