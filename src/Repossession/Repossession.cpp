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
#include "Panel.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>


static const char* MODE_NAMES[] = {"Forward", "Random", "Ping-pong", "CV only"};
static const int MAX_MINUTES[] = {1, 3, 6, 10};
static const float XFADE_MS[] = {0.f, 2.f, 4.f, 10.f};


/** rack::clamp overloads on int and float only, and the read pointer is a
    double: at 48 kHz a float runs out of integer precision 3 seconds in. */
static inline double clampd(double x, double a, double b) {
	return (x < a) ? a : ((x > b) ? b : x);
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
		RUN_PARAM,
		SLOT_PARAM,
		PARAMS_LEN = SLOT_PARAM + rp::NUM_SLOTS
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		REGION_INPUT,
		SCAN_INPUT,
		SPEED_INPUT,
		FIRE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		POS_OUTPUT,
		GATE_OUTPUT,
		EOR_OUTPUT,
		REGION_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		BUSY_LIGHT,
		CLOCK_LIGHT,
		SLOT_LIGHT,
		LIGHTS_LEN = SLOT_LIGHT + rp::NUM_SLOTS
	};
	enum Mode { MODE_FORWARD, MODE_RANDOM, MODE_PINGPONG, MODE_CV, NUM_MODES };

	/** One seized span, in fractions of the whole clip so it survives a
	    re-import at another sample rate. Plain floats, not atomics: the UI
	    thread edits them while the audio thread reads them, and process()
	    re-clamps and re-orders every sample, so the worst a half-applied drag
	    can do is move a loop point one buffer early. */
	struct Region {
		bool active = false;
		float start = 0.f;
		float end = 0.f;
		float speed = 0.f;      // octaves: the rate multiplier is 2^speed
		bool reverse = false;
		float gain = 1.f;
		bool loop = true;

		float lo() const { return std::min(start, end); }
		float hi() const { return std::max(start, end); }
	};
	Region regions[rp::NUM_SLOTS];

	// --- the media, and how it crosses to the audio thread -------------------
	rp::Loader loader;
	std::atomic<rp::Media*> active;
	std::shared_ptr<rp::Media> owner;       // UI thread
	std::shared_ptr<rp::Media> retired;     // UI thread
	std::atomic<uint64_t> tick;
	uint64_t retireTick = 0;

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

	dsp::SchmittTrigger clockTrig, resetTrig, fireTrig;
	dsp::SchmittTrigger slotTrig[rp::NUM_SLOTS];
	dsp::PulseGenerator eorPulse;
	dsp::ClockDivider lightDiv;

	Repossession() {
		active.store(NULL);
		tick.store(0);
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
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			configButton(SLOT_PARAM + i, string::f("Region %d", i + 1));

		configInput(CLOCK_INPUT, "Clock / step");
		configInput(RESET_INPUT, "Reset");
		configInput(REGION_INPUT, "Region select CV (0-10 V over the eight slots)");
		configInput(SCAN_INPUT, "Scan CV (0-10 V scrubs inside the region)");
		configInput(SPEED_INPUT, "Speed CV (1 V per 0.4 octaves)");
		configInput(FIRE_INPUT, "Fire (re-triggers the current region)");

		configOutput(OUT_L_OUTPUT, "Audio left");
		configOutput(OUT_R_OUTPUT, "Audio right");
		configOutput(POS_OUTPUT, "Playhead position (0-10 V over the clip)");
		configOutput(GATE_OUTPUT, "Gate (high while a region plays)");
		configOutput(EOR_OUTPUT, "End of region trigger");
		configOutput(REGION_OUTPUT, "Region number (1 V per slot)");

		lightDiv.setDivision(32);
	}

	~Repossession() override {
		// Cancels the child process and joins, so a module deleted mid-download
		// does not leave a worker holding a pipe.
		loader.stop();
		active.store(NULL);
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
		if (!anyRegion())
			resetRegions();
	}

	/** Frees the previous Media once process() has certainly re-read `active`.
	    With the engine paused the counter does not move and the buffer is simply
	    held until it does, or until the module is deleted. */
	void releaseRetired() {
		if (retired && tick.load() > retireTick)
			retired.reset();
	}

	bool anyRegion() const {
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			if (regions[i].active)
				return true;
		return false;
	}

	void resetRegions() {
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			regions[i].active = true;
			regions[i].start = (float) i / rp::NUM_SLOTS;
			regions[i].end = (float) (i + 1) / rp::NUM_SLOTS;
			regions[i].speed = 0.f;
			regions[i].reverse = false;
			regions[i].gain = 1.f;
			regions[i].loop = true;
		}
		lastSel = -1;
	}

	void clearRegions() {
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			regions[i].active = false;
		lastSel = -1;
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

	int firstActive(int from, int step) const {
		for (int n = 0; n < rp::NUM_SLOTS; n++) {
			int i = from + step * n;
			i = ((i % rp::NUM_SLOTS) + rp::NUM_SLOTS) % rp::NUM_SLOTS;
			if (regions[i].active)
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
					if (regions[i].active)
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

	static inline float tap(const rp::Media* m, int64_t i, int ch) {
		if (i < 0)
			i = 0;
		if (i >= m->frames)
			i = m->frames - 1;
		return m->pcm[(size_t) (i * 2 + ch)];
	}

	static inline void readAt(const rp::Media* m, double p, float* l, float* r) {
		int64_t i1 = (int64_t) std::floor(p);
		float t = (float) (p - (double) i1);
		for (int ch = 0; ch < 2; ch++) {
			float y = hermite(tap(m, i1 - 1, ch), tap(m, i1, ch),
				tap(m, i1 + 1, ch), tap(m, i1 + 2, ch), t);
			if (ch == 0)
				*l = y;
			else
				*r = y;
		}
	}

	void jumpTo(double target, double step) {
		// A discontinuity: keep the old read pointer alive for the crossfade so
		// the seam is a fade rather than an edge.
		if (xfadeLen > 0) {
			oldPos = pos;
			oldStep = step;
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
		bool running = params[RUN_PARAM].getValue() > 0.5f;

		// selection: the slot buttons and the REGION knob are the same control
		int sel = clamp((int) std::round(params[REGION_PARAM].getValue()),
			0, rp::NUM_SLOTS - 1);
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			if (slotTrig[i].process(params[SLOT_PARAM + i].getValue())) {
				params[REGION_PARAM].setValue((float) i);
				sel = i;
				if (!regions[i].active) {
					// An empty slot is a skipped slot; pressing it fills it with
					// an eighth of the clip so there is something to skip.
					regions[i].active = true;
					regions[i].start = (float) i / rp::NUM_SLOTS;
					regions[i].end = (float) (i + 1) / rp::NUM_SLOTS;
				}
			}
		}
		if (sel != lastSel) {
			lastSel = sel;
			pushToParams(sel);
		}
		else {
			pullFromParams(sel);
		}

		bool fire = false;

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

		bool clocked = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		if (clocked) {
			if (!inputs[REGION_INPUT].isConnected())
				playSlot = nextSlot(playSlot, mode);
			fire = true;
		}
		if (fireTrig.process(inputs[FIRE_INPUT].getVoltage(), 0.1f, 1.f))
			fire = true;

		playSlot = clamp(playSlot, 0, rp::NUM_SLOTS - 1);

		float outL = 0.f, outR = 0.f;
		double posFrac = 0.0;

		if (m && m->playable()) {
			const Region& R = regions[playSlot];
			double n = (double) m->frames;
			float lo = clamp(R.lo(), 0.f, 1.f);
			float hi = clamp(R.hi(), 0.f, 1.f);
			if (hi - lo < 1e-4f)
				hi = std::min(1.f, lo + 1e-4f);
			double s = lo * n;
			double e = hi * n;
			if (e <= s + 1.0)
				e = s + 1.0;

			float oct = R.speed;
			if (inputs[SPEED_INPUT].isConnected())
				oct += inputs[SPEED_INPUT].getVoltage() * 0.4f;
			oct = clamp(oct, -3.f, 3.f);
			double step = std::pow(2.0, (double) oct)
				* (double) m->sampleRate / (double) args.sampleRate;
			if (R.reverse)
				step = -step;

			if (fire && R.active) {
				jumpTo(step >= 0.0 ? s : e - 1.0, step);
				playing = running;
			}
			if (!R.active)
				playing = false;

			if (inputs[SCAN_INPUT].isConnected()) {
				// SCAN takes the playhead over: the region becomes a window you
				// are scrubbing rather than something being played through.
				float sc = clamp(inputs[SCAN_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
				pos = s + (e - s) * (double) sc;
				playing = running && R.active;
			}
			else if (playing && running) {
				pos += step;
				if (step >= 0.0 && pos >= e) {
					eorPulse.trigger(1e-3f);
					if (R.loop)
						jumpTo(s + (pos - e), step);
					else {
						pos = e;
						playing = false;
					}
				}
				else if (step < 0.0 && pos <= s) {
					eorPulse.trigger(1e-3f);
					if (R.loop)
						jumpTo(e - (s - pos), step);
					else {
						pos = s;
						playing = false;
					}
				}
			}
			if (!running)
				playing = false;

			pos = clampd(pos, 0.0, n - 1.0);
			posFrac = pos / n;

			if (playing) {
				readAt(m, pos, &outL, &outR);
				if (xfadeLeft > 0) {
					float a, b;
					readAt(m, clampd(oldPos, 0.0, n - 1.0), &a, &b);
					oldPos += oldStep;
					float t = (float) xfadeLeft / (float) xfadeLen;
					outL = outL * (1.f - t) + a * t;
					outR = outR * (1.f - t) + b * t;
					xfadeLeft--;
				}
				float g = clamp(R.gain, 0.f, 2.f);
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

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int i = 0; i < rp::NUM_SLOTS; i++) {
				float b = 0.f;
				if (regions[i].active)
					b = (i == playSlot && playing) ? 1.f : (i == sel ? 0.45f : 0.12f);
				lights[SLOT_LIGHT + i].setBrightnessSmooth(b, dt);
			}
			lights[RUN_LIGHT].setBrightnessSmooth(running ? 1.f : 0.f, dt);
			lights[BUSY_LIGHT].setBrightnessSmooth(uiBusy.load(), dt);
			lights[CLOCK_LIGHT].setBrightnessSmooth(
				inputs[CLOCK_INPUT].getVoltage() > 1.f ? 1.f : 0.f, dt);

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
		json_object_set_new(rootJ, "toolDir", json_string(toolDir.c_str()));

		json_t* regionsJ = json_array();
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			const Region& R = regions[i];
			json_t* r = json_object();
			json_object_set_new(r, "active", json_boolean(R.active));
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
		if (image >= 0 && imageVg)
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

	float toFrac(float x) const { return clamp(x / box.size.x, 0.f, 1.f); }
	float toPx(float f) const { return f * box.size.x; }

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
			R.start = f;
			R.end = std::min(1.f, f + 0.004f);
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
			if (hi - lo < 0.002f)
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
			int bars = (int) std::min((float) rp::PEAK_BUCKETS, w);
			if (bars < 1)
				bars = 1;
			nvgBeginPath(args.vg);
			for (int i = 0; i < bars; i++) {
				int b = i * rp::PEAK_BUCKETS / bars;
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

		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			const Repossession::Region& R = module->regions[i];
			if (!R.active)
				continue;
			float x0 = toPx(R.lo()), x1 = toPx(R.hi());
			bool isLive = (i == live && playing);
			NVGcolor c = isLive ? panel::MINT : panel::LIME;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 0.f, std::max(1.f, x1 - x0), h);
			nvgFillColor(args.vg, panel::alpha(c, isLive ? 0.34f : 0.16f));
			nvgFill(args.vg);

			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 0.f, std::max(1.f, x1 - x0), h);
			nvgStrokeColor(args.vg, panel::alpha(c, i == sel ? 0.95f : 0.45f));
			nvgStrokeWidth(args.vg, i == sel ? 1.2f : 0.7f);
			nvgStroke(args.vg);

			if (x1 - x0 >= 14.f) {
				float cx = (x0 + x1) / 2.f;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, cx - 3.f, 0.f, 6.f, 2.2f);
				nvgFillColor(args.vg, panel::alpha(panel::CLAY, 0.85f));
				nvgFill(args.vg);
			}
		}

		float px = toPx(module->uiPos.load());
		nvgBeginPath(args.vg);
		nvgRect(args.vg, px - 0.5f, 0.f, 1.f, h);
		nvgFillColor(args.vg, panel::PAPER);
		nvgFill(args.vg);
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
			panel::text(args.vg, body.inked(panel::LIME), x, y + 2 * lead,
				string::f("R%d  %s - %s  %s%.2fx  %d%%%s", sel + 1,
					timeOf(R.lo() * dur).c_str(), timeOf(R.hi() * dur).c_str(),
					R.reverse ? "-" : "", std::pow(2.f, R.speed),
					(int) (R.gain * 100.f + 0.5f), R.loop ? "  LOOP" : ""));
		}
		else {
			panel::text(args.vg, body.inked(panel::alpha(panel::SAGE, 0.6f)),
				x, y + 2 * lead, string::f("R%d  released", sel + 1));
		}

		if (!status.empty()) {
			NVGcolor c = statusIsError ? panel::CLAY : panel::SAGE;
			panel::text(args.vg, body.inked(c), x, y + 3 * lead,
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
			addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
				panel::mm(slotPos()[i].x, slotPos()[i].y), module,
				Repossession::SLOT_PARAM + i, Repossession::SLOT_LIGHT + i));
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
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::MintLight> >(
			panel::mm(panel::RUN_POS.x, panel::RUN_POS.y), module,
			Repossession::RUN_PARAM, Repossession::RUN_LIGHT));

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
			panel::mm(panel::SPEED_IN_POS.x, panel::SPEED_IN_POS.y), module,
			Repossession::SPEED_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::FIRE_IN_POS.x, panel::FIRE_IN_POS.y), module,
			Repossession::FIRE_INPUT));

		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module,
			Repossession::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module,
			Repossession::OUT_R_OUTPUT));
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
		mod->releaseRetired();

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

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tools"));
		menu->addChild(createMenuItem("Tools folder...",
			self->mod->toolDir.empty() ? "PATH" : self->mod->toolDir, [self]() {
				char* dir = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
				if (dir) {
					self->mod->toolDir = dir;
					std::free(dir);
				}
			}));
		menu->addChild(createMenuItem("Clear tools folder", "",
			[self]() { self->mod->toolDir = ""; }));
		menu->addChild(createMenuItem("Open cache folder", "", []() {
			system::createDirectories(rp::cacheDir());
			system::openDirectory(rp::cacheDir());
		}));
	}
};


Model* modelRepossession = createModel<Repossession, RepossessionWidget>("Repossession");
