#include "../plugin.hpp"
#include "Panel.hpp"
#include "Encoder.hpp"
#include "../VideoBus.hpp"
#include "../Process.hpp"
#include "Publisher.hpp"

#include <atomic>
#include <chrono>
#include <thread>


// ---------------------------------------------------------------------------
// Transmittal -- the plugin's video output.
//
// Frames come off the video bus, where any module in the plugin can publish
// them (see src/VideoBus.hpp for why they do not come down a cable). They go
// out through an ffmpeg this module spawns and feeds raw RGBA on a pipe, which
// writes a live HLS playlist that TouchDesigner reads with a Video Stream In
// TOP -- the path is on the panel to be copied.
//
// HLS is not the fast way to do this. A player sits a segment or two behind, so
// two or three seconds, which is fine for recording and wrong for performing.
// It is what ships first because it needs nothing of ffmpeg that every build
// has -- and this machine's ffmpeg has neither SRT nor RTSP compiled in -- and
// because it costs the plugin no third-party dependency at all, which
// docs/LIBRARY.md is otherwise able to claim. The fast path is Syphon on macOS
// and Spout on Windows, publishing a GL texture instead of piping bytes, and it
// goes alongside this rather than replacing it: the bus, the source selection
// and this whole panel stay exactly as they are.
//
// Everything that can block lives on the worker thread. Writing to a pipe
// blocks when the encoder is behind, and the audio thread is the one place that
// must never wait for a subprocess.
// ---------------------------------------------------------------------------

namespace tx = transmittal;

static const char* panel_publisher_name() { return transmittal::publisherName(); }


struct Transmittal : Module {
	enum ParamId { SOURCE_PARAM, SIZE_PARAM, RATE_PARAM, SEND_PARAM, PARAMS_LEN };
	enum InputId { SEND_IN_INPUT, INPUTS_LEN };
	enum OutputId { SENDING_OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { SEND_LED_LIGHT, STATE_LIGHT, STATE_LIGHT_G, LIGHTS_LEN };

	enum State { IDLE, RUNNING, FAILED };
	/** SYPHON hands a GPU texture straight to a compositor: one Rack frame plus
	    one TouchDesigner frame of latency, which is what you can perform with.
	    HLS encodes and segments, two to three seconds behind, which is what you
	    can capture with. Syphon is the default wherever it exists. */
	enum Backend { SYPHON, HLS };

	// --- shared with the worker ---------------------------------------------
	std::thread worker;
	std::atomic<bool> quit;         // set once, at destruction
	std::atomic<bool> want;         // the transport, as the panel asks for it
	std::atomic<int> wantSize;
	std::atomic<int> wantRate;
	std::atomic<long long> wantSource;
	std::atomic<int> state;
	std::atomic<bool> live;         // frames are actually being written
	std::atomic<int> backend;
	std::atomic<int> clients;       // how many are watching the fast path

	// Written by the worker, read by the UI. A mutex rather than an atomic
	// because they are strings, and they change about once a second.
	std::mutex textMu;
	std::string playlist;
	std::string note;

	dsp::BooleanTrigger sendTrig;
	dsp::SchmittTrigger sendGate;
	bool sending = false;

	Transmittal() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configSwitch(SOURCE_PARAM, 0.f, 7.f, 0.f, "Source");
		std::vector<std::string> sizes, rates;
		for (int i = 0; i < tx::kNumSizes; i++)
			sizes.push_back(tx::kSizes[i].name);
		for (int i = 0; i < tx::kNumRates; i++)
			rates.push_back(std::to_string(tx::kRates[i]) + " fps");
		configSwitch(SIZE_PARAM, 0.f, (float)(tx::kNumSizes - 1), 0.f, "Size", sizes);
		configSwitch(RATE_PARAM, 0.f, (float)(tx::kNumRates - 1), 1.f, "Rate", rates);
		configButton(SEND_PARAM, "Send");

		configInput(SEND_IN_INPUT, "Send gate");
		configOutput(SENDING_OUT_OUTPUT, "High while frames are being written");

		quit = false; want = false; live = false; clients = 0;
		backend = tx::publisherAvailable() ? SYPHON : HLS;
		wantSize = 0; wantRate = 1; wantSource = 0;
		state = IDLE;

		playlist = dir() + "stream.m3u8";
		note = "idle";
		worker = std::thread(&Transmittal::run, this);
	}

	/** The playlist is named for the module, so two Transmittals in one rack
	    write two streams instead of fighting over one file. The id only exists
	    once Rack has added the module, which is why this is not the
	    constructor's job. */
	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		std::lock_guard<std::mutex> lock(textMu);
		playlist = dir() + rack::string::f("stream-%lld.m3u8", (long long)id);
	}

	~Transmittal() {
		// Rack unmaps the library after this; a worker still inside it would be
		// executing freed code. Join unconditionally.
		quit = true;
		want = false;
		if (worker.joinable())
			worker.join();
	}

	static std::string dir() {
		return rack::asset::user("MoonTechnologies/Transmittal/");
	}

	/** The sources on the bus, with a "nothing" entry first so the knob always
	    has something to point at. */
	static std::vector<std::pair<int64_t, std::string> > sources() {
		std::vector<std::pair<int64_t, std::string> > v;
		v.push_back(std::make_pair((int64_t)0, std::string("--")));
		std::vector<std::pair<int64_t, std::string> > reg = videobus::bus().list();
		for (size_t i = 0; i < reg.size() && v.size() < 8; i++)
			v.push_back(reg[i]);
		return v;
	}

	void setNote(const std::string& s) {
		std::lock_guard<std::mutex> lock(textMu);
		note = s;
	}

	// --- the worker ----------------------------------------------------------
	void run() {
		rp::Streamer enc;
		tx::Pacer pacer;
		int curSize = -1, curRate = -1;
		long long curSource = -1;
		uint64_t seen = 0;
		bool preferHw = true;
		long framesThisRun = 0;
		int curRateFps = tx::kRates[0];
		videobus::Frame frame;
		std::vector<uint8_t> scaled;
		std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

		while (!quit) {
			bool on = want;
			int ws = wantSize, wr = wantRate;
			long long src = wantSource;

			// Any of the three is baked into ffmpeg's command line, so changing
			// one means a new ffmpeg. Restarting on a knob turn is the honest
			// behaviour; pretending otherwise would shear the picture.
			bool changed = (ws != curSize || wr != curRate || src != curSource);

			if (enc.running && (!on || changed)) {
				enc.stop();
				live = false;
				if (!on) { state = IDLE; setNote("idle"); }
			}

			if (on && !enc.running) {
				curSize = ws; curRate = wr; curSource = src;
				seen = 0;
				if (src == 0) {
					state = FAILED;
					setNote("no source selected");
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					continue;
				}
				std::string ff = rp::which("ffmpeg", rp::defaultToolDirs());
				if (ff.empty()) {
					state = FAILED;
					setNote("ffmpeg not found");
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				rack::system::createDirectories(dir());
				std::string pl;
				{
					std::lock_guard<std::mutex> lock(textMu);
					pl = playlist;
				}
				const tx::Size& sz = tx::kSizes[clamp(curSize, 0, tx::kNumSizes - 1)];
				int fps = tx::kRates[clamp(curRate, 0, tx::kNumRates - 1)];
				curRateFps = fps;
				pacer.setRate(fps);
				pacer.reset();
				scaled.assign((size_t)sz.w * (size_t)sz.h * 4u, 0);
#if defined ARCH_MAC
				const char* hwCodec = "h264_videotoolbox";
#elif defined ARCH_WIN
				const char* hwCodec = "h264_mf";
#else
				const char* hwCodec = "h264_vaapi";
#endif
				// Hardware first, software after it has once failed. An ffmpeg
				// built without the platform encoder does not refuse to start
				// -- fork and exec both succeed -- it exits a moment later, and
				// the only symptom is the first write finding a closed pipe.
				// So the fallback is driven from there, not from here.
				if (!enc.start(tx::argvFor(ff, pl, sz.w, sz.h, fps, preferHw, hwCodec))) {
					state = FAILED;
					setNote(enc.error);
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				state = RUNNING;
				setNote(preferHw ? "streaming" : "streaming (software)");
				framesThisRun = 0;
				last = std::chrono::steady_clock::now();
			}

			if (!enc.running) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}

			std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			double dt = std::chrono::duration<double>(now - last).count();
			last = now;

			if (pacer.tick(dt)) {
				const tx::Size& sz = tx::kSizes[clamp(curSize, 0, tx::kNumSizes - 1)];
				// A source that has published nothing new keeps its last frame
				// on screen: a stalled source should look frozen, not black.
				if (videobus::bus().latest((int64_t)curSource, seen, frame)) {
					seen = frame.seq;
					if (frame.w == sz.w && frame.h == sz.h)
						std::memcpy(&scaled[0], &frame.rgba[0], scaled.size());
					else
						tx::scaleRgba(&frame.rgba[0], frame.w, frame.h,
						              &scaled[0], sz.w, sz.h);
				}
				if (!enc.write(&scaled[0], scaled.size())) {
					enc.stop();
					live = false;
					// Dying within the first second of a run, on the hardware
					// encoder, is what an ffmpeg without that encoder looks
					// like. Drop to libx264 once and try again rather than
					// reporting a failure the user cannot act on.
					if (preferHw && framesThisRun < curRateFps) {
						preferHw = false;
						curSize = -1;          // force a restart next pass
						setNote("no hardware encoder, retrying");
						continue;
					}
					state = FAILED;
					setNote(enc.error);
					continue;
				}
				framesThisRun++;
				live = true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		enc.stop();
	}

	void process(const ProcessArgs& args) override {
		bool pressed = sendTrig.process(params[SEND_PARAM].getValue() > 0.5f);
		bool gated = inputs[SEND_IN_INPUT].isConnected()
			&& sendGate.process(inputs[SEND_IN_INPUT].getVoltage(), 0.1f, 1.f);
		if (pressed || gated)
			sending = !sending;
		// A gate held low is a stop, so a sequencer can hold the transport
		// rather than having to toggle it.
		if (inputs[SEND_IN_INPUT].isConnected() && !sendGate.isHigh())
			sending = false;

		// The worker only exists for the encoder. On the fast path the widget
		// publishes on the UI thread, where the GL context is, and ffmpeg is
		// never started at all.
		want = sending && backend == HLS;
		wantSize = (int)std::round(params[SIZE_PARAM].getValue());
		wantRate = (int)std::round(params[RATE_PARAM].getValue());

		std::vector<std::pair<int64_t, std::string> > srcs = sources();
		int pick = clamp((int)std::round(params[SOURCE_PARAM].getValue()),
		                 0, (int)srcs.size() - 1);
		wantSource = (long long)srcs[pick].first;

		bool fast = (backend == SYPHON);
		int st = state;
		bool isLive = fast ? (sending && (bool)live) : (bool)live;
		outputs[SENDING_OUT_OUTPUT].setVoltage(isLive ? 10.f : 0.f);
		lights[SEND_LED_LIGHT].setBrightness(sending ? 1.f : 0.f);
		lights[STATE_LIGHT].setBrightness(!fast && st == FAILED ? 1.f : 0.f);
		lights[STATE_LIGHT_G].setBrightness(isLive ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "backend", json_integer(backend));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* b = json_object_get(root, "backend");
		if (b) {
			int v = (int)json_integer_value(b);
			// A patch saved on a Mac and opened on Linux must not come up
			// asking for a backend that is not compiled in.
			backend = (v == SYPHON && tx::publisherAvailable()) ? SYPHON : HLS;
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and
// src/Transmittal/Panel.hpp, generated by tools/panels/Transmittal.py.

/** The read-out. Everything this module has to say is text, and the playlist
    path is the point of it: it is what gets pasted into TouchDesigner's Video
    Stream In TOP, and it is too long to guess. */
struct TransmittalDisplay : Widget {
	Transmittal* module = NULL;
	panel::FittedText fitted, fittedSrc, fittedVia;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		float x = panel::mm(2.6f, 0.f).x;
		float y = panel::mm(0.f, 4.6f).y;
		float lh = panel::mm(0.f, 4.5f).y;

		std::string src = "--", size = "--", rate = "--", note = "no module", pl;
		std::string via = "";
		if (module) {
			bool fast = module->backend == Transmittal::SYPHON;
			via = fast ? std::string(panel_publisher_name())
			           : std::string("HLS  ~2-3 s behind");
			if (fast) {
				int n = module->clients;
				via += n ? rack::string::f("  %d watching", n) : "  nobody watching";
			}
			std::vector<std::pair<int64_t, std::string> > s = Transmittal::sources();
			int pick = clamp((int)std::round(module->params[Transmittal::SOURCE_PARAM].getValue()),
			                 0, (int)s.size() - 1);
			src = s[pick].second;
			const transmittal::Size& sz = transmittal::kSizes[
				clamp((int)std::round(module->params[Transmittal::SIZE_PARAM].getValue()),
				      0, transmittal::kNumSizes - 1)];
			size = sz.name;
			rate = std::to_string(transmittal::kRates[
				clamp((int)std::round(module->params[Transmittal::RATE_PARAM].getValue()),
				      0, transmittal::kNumRates - 1)]) + " fps";
			std::lock_guard<std::mutex> lock(module->textMu);
			note = fast ? (module->sending ? "sending" : "idle") : module->note;
			pl = fast ? std::string("") : module->playlist;
		}

		panel::TextStyle mono(panel::Face::Mono, 9.f, panel::PAPER,
		                      NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		// Every line here is fitted, not just the path. A source's name carries
		// its module's id, which is eighteen digits and will not fit any well
		// this panel could have -- and text that runs off the glass reads as a
		// broken panel rather than as a long name.
		float inner = box.size.x - panel::mm(5.2f, 0.f).x;
		const std::string& srcLine = fittedSrc.get(args.vg, mono, "SRC  " + src, inner);
		panel::text(args.vg, mono, x, y, srcLine.c_str());
		panel::text(args.vg, mono.inked(panel::SAGE), x, y + lh,
		            (size + "   " + rate).c_str());
		panel::text(args.vg, mono.inked(panel::LIME), x, y + lh * 2.f, note.c_str());
		if (!via.empty()) {
			panel::TextStyle vs = mono.sized(7.5f).inked(panel::SAGE);
			panel::text(args.vg, vs, x, y + lh * 3.2f,
			            fittedVia.get(args.vg, vs, via, inner).c_str());
		}
		if (!pl.empty()) {
			// Ellipsized rather than clipped: the path is the one string here
			// that has to be read character by character, and a run of it
			// disappearing off the well's edge would look like the whole path.
			panel::TextStyle small = mono.sized(7.5f).inked(panel::SAGE);
			const std::string& shown = fitted.get(args.vg, small, pl, inner);
			panel::text(args.vg, small, x, y + lh * 4.3f, shown.c_str());
		}
	}
};


struct TransmittalWidget : ModuleWidget {
	TransmittalWidget(Transmittal* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Transmittal.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		TransmittalDisplay* d = new TransmittalDisplay;
		d->module = module;
		d->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		d->box.size = math::Vec(panel::mm(panel::GLASS_W, 0.f).x,
		                        panel::mm(0.f, panel::GLASS_H).y);
		addChild(d);

		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::SOURCE_POS.x, panel::SOURCE_POS.y), module, Transmittal::SOURCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::SIZE_POS.x, panel::SIZE_POS.y), module, Transmittal::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::RATE_POS.x, panel::RATE_POS.y), module, Transmittal::RATE_PARAM));
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
			panel::mm(panel::SEND_POS.x, panel::SEND_POS.y), module,
			Transmittal::SEND_PARAM, Transmittal::SEND_LED_LIGHT));

		// The caption light is the transport at a glance, from across a room:
		// clay when ffmpeg has gone away, mint while frames are actually being
		// written -- which is not the same as having been asked to send.
		addChild(createLightCentered<SmallLight<panel::ClayLight> >(
			panel::mm(panel::STATE_POS.x, panel::STATE_POS.y), module,
			Transmittal::STATE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
			panel::mm(panel::STATE_POS.x, panel::STATE_POS.y), module,
			Transmittal::STATE_LIGHT_G));

		addInput(createInputCentered<panel::PortTrigIn>(
			panel::mm(panel::SEND_IN_POS.x, panel::SEND_IN_POS.y), module, Transmittal::SEND_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::SENDING_OUT_POS.x, panel::SENDING_OUT_POS.y), module, Transmittal::SENDING_OUT_OUTPUT));
	}

	// --- the fast path ------------------------------------------------------
	// Rack calls step() on the UI thread once a frame, before it begins its
	// NanoVG frame (Rack/src/window/Window.cpp:496), so the GL context is
	// current and nothing NanoVG is midway through can be disturbed. That is
	// the only place a texture can be published from, which is why this lives
	// on the widget and not on the module.
	transmittal::Publisher pub;
	uint64_t seenSeq = 0;
	std::vector<uint8_t> buf;
	int bufW = 0, bufH = 0;

	void step() override {
		ModuleWidget::step();
		Transmittal* m = dynamic_cast<Transmittal*>(module);
		if (!m || m->backend != Transmittal::SYPHON || !m->sending) {
			if (pub.running()) {
				pub.stop();
				seenSeq = 0;
			}
			if (m) { m->clients = 0; m->live = false; }
			return;
		}

		const transmittal::Size& sz = transmittal::kSizes[
			clamp((int)std::round(m->params[Transmittal::SIZE_PARAM].getValue()),
			      0, transmittal::kNumSizes - 1)];
		long long src = m->wantSource;
		if (src == 0) { m->live = false; return; }

		if (!pub.start(rack::string::f("Transmittal %lld", (long long)m->id),
		               sz.w, sz.h)) {
			m->live = false;
			return;
		}
		if (bufW != sz.w || bufH != sz.h) {
			buf.assign((size_t)sz.w * (size_t)sz.h * 4u, 0);
			bufW = sz.w; bufH = sz.h;
			seenSeq = 0;
		}

		// Only on a frame the source has not published before. Re-publishing
		// the same picture would cost an upload per screen refresh and change
		// nothing downstream: Syphon holds the surface, so a stalled source
		// stays visible without being re-sent.
		videobus::Frame f;
		if (videobus::bus().latest((int64_t)src, seenSeq, f)) {
			seenSeq = f.seq;
			if (f.w == sz.w && f.h == sz.h)
				std::memcpy(&buf[0], &f.rgba[0], buf.size());
			else
				transmittal::scaleRgba(&f.rgba[0], f.w, f.h, &buf[0], sz.w, sz.h);
			pub.publish(&buf[0], sz.w, sz.h);
		}
		m->clients = pub.hasClients() ? 1 : 0;
		m->live = true;
	}

	void appendContextMenu(Menu* menu) override {
		Transmittal* m = dynamic_cast<Transmittal*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Transmittal"));

		if (transmittal::publisherAvailable()) {
			menu->addChild(createIndexSubmenuItem("Transport",
				{ std::string(transmittal::publisherName()) + " (fast, for performing)",
				  "HLS playlist (2-3 s behind, for capture)" },
				[=]() { return (int)m->backend; },
				[=](int i) { m->backend = i ? Transmittal::HLS : Transmittal::SYPHON; }));
		}
		else {
			menu->addChild(createMenuLabel("Transport: HLS (no texture sharing on this platform)"));
		}

		menu->addChild(createMenuItem("Copy playlist path", "", [=]() {
			std::lock_guard<std::mutex> lock(m->textMu);
			glfwSetClipboardString(APP->window->win, m->playlist.c_str());
		}));
	}
};


Model* modelTransmittal = createModel<Transmittal, TransmittalWidget>("Transmittal");
