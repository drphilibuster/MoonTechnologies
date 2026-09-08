#include "../plugin.hpp"
#include "Panel.hpp"
#include "Bands.hpp"
#include "Render.hpp"
#include "../VideoBus.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>


// ---------------------------------------------------------------------------
// Projection -- video made out of what the rack is already doing.
//
// Audio in, spectrum out, and a picture driven by both, published to the video
// bus (src/VideoBus.hpp) where Transmittal picks it up exactly as it picks up
// Repossession. Nothing new is needed downstream: a Syphon Spout In TOP sees
// this the same way it sees a seized video.
//
// Three threads, and what happens on each of them is the whole design:
//
//   audio    fills a ring buffer, and nothing else. No FFT, no rendering, no
//            allocation -- the engine's deadline is 20-odd microseconds and a
//            2048-point transform is not going to happen inside it.
//   worker   takes a window when one is ready, transforms it, follows the
//            bands, draws the frame and publishes it. Everything expensive.
//   UI       reads the last published frame to draw the preview, and nothing
//            more.
//
// The band envelopes are written by the worker and read by the audio thread for
// the CV outputs. That is a race in the strict sense and deliberately unguarded:
// they are floats being read for a control voltage, a torn read is a value
// somewhere between the old and the new, and paying a lock per sample to avoid
// an error smaller than the CV's own quantisation would be the wrong trade.
// ---------------------------------------------------------------------------

namespace pj = projection;

static const int kFftSize = 2048;
static const int kHop = 512;          // 43 windows a second at 44.1 kHz


struct Projection : Module {
	enum ParamId {
		SENS_PARAM, TILT_PARAM, MODE_PARAM, SCALE_PARAM, WARP_PARAM,
		HUE_PARAM, TRAIL_PARAM, SAT_PARAM, PARAMS_LEN
	};
	enum InputId {
		AUDIO_L_INPUT, AUDIO_R_INPUT,
		MODE_CV_INPUT, SCALE_CV_INPUT, WARP_CV_INPUT, HUE_CV_INPUT,
		FLASH_INPUT, FREEZE_INPUT, INPUTS_LEN
	};
	enum OutputId {
		LOW_OUT_OUTPUT, MID_OUT_OUTPUT, HIGH_OUT_OUTPUT, ONSET_OUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId { LIGHTS_LEN };

	// --- audio -> worker: a ring of interleaved L/R -------------------------
	static const int kRing = 1 << 14;
	float ringL[kRing];
	float ringR[kRing];
	std::atomic<uint64_t> written;      // samples written, ever

	// --- worker -> everyone --------------------------------------------------
	pj::BandFollower follower;
	pj::Onset onset;
	std::atomic<float> bandLow, bandMid, bandHigh;
	std::atomic<bool> onsetFired;
	dsp::PulseGenerator onsetPulse;

	// --- worker -> UI: the last frame, for the preview ----------------------
	std::mutex previewMu;
	std::vector<uint8_t> preview;
	int previewW = 0, previewH = 0;
	std::atomic<uint64_t> previewSeq;

	std::thread worker;
	std::atomic<bool> quit;
	std::atomic<float> sampleRate;

	dsp::SchmittTrigger freezeTrig;

	Projection() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(SENS_PARAM, 0.f, 1.f, 0.5f, "Sensitivity", "%", 0.f, 100.f);
		configParam(TILT_PARAM, 0.f, 1.f, 0.5f, "Spectral tilt", "%", 0.f, 100.f);
		std::vector<std::string> modes;
		modes.push_back("Scope (XY from L and R)");
		modes.push_back("Bars (spectrum)");
		modes.push_back("Field (warped by the bands)");
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", modes);
		configParam(SCALE_PARAM, 0.f, 1.f, 0.5f, "Scale", "%", 0.f, 100.f);
		configParam(WARP_PARAM, 0.f, 1.f, 0.3f, "Warp", "%", 0.f, 100.f);
		configParam(HUE_PARAM, 0.f, 1.f, 0.33f, "Hue", "%", 0.f, 100.f);
		configParam(TRAIL_PARAM, 0.f, 1.f, 0.4f, "Trail", "%", 0.f, 100.f);
		configParam(SAT_PARAM, 0.f, 1.f, 0.8f, "Saturation", "%", 0.f, 100.f);

		configInput(AUDIO_L_INPUT, "Audio left");
		configInput(AUDIO_R_INPUT, "Audio right (normalled to left)");
		configInput(MODE_CV_INPUT, "Mode CV");
		configInput(SCALE_CV_INPUT, "Scale CV");
		configInput(WARP_CV_INPUT, "Warp CV");
		configInput(HUE_CV_INPUT, "Hue CV");
		configInput(FLASH_INPUT, "Flash gate");
		configInput(FREEZE_INPUT, "Freeze gate (holds the last frame)");

		configOutput(LOW_OUT_OUTPUT, "Low band envelope");
		configOutput(MID_OUT_OUTPUT, "Mid band envelope");
		configOutput(HIGH_OUT_OUTPUT, "High band envelope");
		configOutput(ONSET_OUT_OUTPUT, "Onset trigger");

		std::memset(ringL, 0, sizeof(ringL));
		std::memset(ringR, 0, sizeof(ringR));
		written = 0;
		previewSeq = 0;
		bandLow = bandMid = bandHigh = 0.f;
		onsetFired = false;
		quit = false;
		sampleRate = 44100.f;

		worker = std::thread(&Projection::run, this);
	}

	~Projection() {
		// Rack unmaps the library after this, so a worker still inside it would
		// be running freed code. Join unconditionally.
		quit = true;
		if (worker.joinable())
			worker.join();
		videobus::bus().remove(id);
	}

	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		videobus::bus().add(id, rack::string::f("Projection %lld", (long long) id));
	}

	void onRemove(const RemoveEvent& e) override {
		videobus::bus().remove(id);
		Module::onRemove(e);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
	}

	/** 0..1 from a knob plus its jack, the family's usual CV shape: 10 V covers
	    the whole control, and an unpatched jack leaves the knob alone. */
	float knobCv(int param, int input) {
		float v = params[param].getValue();
		if (inputs[input].isConnected())
			v += inputs[input].getVoltage() * 0.1f;
		return clamp(v, 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		// The audio thread's whole job: write the ring and read the bands back
		// out as CV. Everything else is the worker's.
		float l = inputs[AUDIO_L_INPUT].getVoltage() * 0.2f;
		float r = inputs[AUDIO_R_INPUT].isConnected()
			? inputs[AUDIO_R_INPUT].getVoltage() * 0.2f : l;
		uint64_t w = written.load(std::memory_order_relaxed);
		ringL[w & (kRing - 1)] = l;
		ringR[w & (kRing - 1)] = r;
		written.store(w + 1, std::memory_order_release);

		outputs[LOW_OUT_OUTPUT].setVoltage(clamp(bandLow.load() * 10.f, 0.f, 10.f));
		outputs[MID_OUT_OUTPUT].setVoltage(clamp(bandMid.load() * 10.f, 0.f, 10.f));
		outputs[HIGH_OUT_OUTPUT].setVoltage(clamp(bandHigh.load() * 10.f, 0.f, 10.f));

		if (onsetFired.exchange(false))
			onsetPulse.trigger(1e-3f);
		outputs[ONSET_OUT_OUTPUT].setVoltage(onsetPulse.process(args.sampleTime) ? 10.f : 0.f);
	}

	// --- the worker ----------------------------------------------------------
	void run() {
		rack::system::setThreadName("Projection");

		dsp::RealFFT fft(kFftSize);
		std::vector<float> win(kFftSize), spec(kFftSize * 2), mag(kFftSize / 2);
		std::vector<float> xs(kHop), ys(kHop);
		float bands[pj::kBands];
		int edges[pj::kBands + 1];
		int edgeRate = 0;

		// One canvas, allocated once. 640x360 is the compromise: four times the
		// pixels of the panel preview, cheap enough for the per-pixel field at
		// 30 fps, and what a compositor is going to rescale anyway.
		const int W = 640, H = 360;
		std::vector<uint8_t> canvas((size_t) W * H * 4u, 0);
		pj::Canvas cv;
		cv.px = &canvas[0];
		cv.w = W;
		cv.h = H;
		pj::clear(cv);

		uint64_t read = 0;
		float phase = 0.f;
		float flash = 0.f;
		auto last = std::chrono::steady_clock::now();

		while (!quit) {
			uint64_t w = written.load(std::memory_order_acquire);
			if (w < read + kFftSize) {
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			// Never let the reader fall so far behind that it is analysing
			// history: if the ring has lapped, jump to the newest full window.
			if (w - read > (uint64_t) kRing - kFftSize)
				read = w - kFftSize;

			float sr = sampleRate.load();
			if (edgeRate != (int) sr) {
				pj::bandEdges(edges, pj::kBands, kFftSize, sr);
				edgeRate = (int) sr;
			}

			float sens = params[SENS_PARAM].getValue();
			float gain = 0.5f + sens * 12.f;

			for (int i = 0; i < kFftSize; i++) {
				uint64_t k = read + (uint64_t) i;
				win[i] = (ringL[k & (kRing - 1)] + ringR[k & (kRing - 1)]) * 0.5f * gain;
			}
			dsp::hannWindow(&win[0], kFftSize);
			fft.rfft(&win[0], &spec[0]);
			// pffft packs the two real-valued bins, DC and Nyquist, into the
			// first complex slot. Everything else is an ordinary pair.
			mag[0] = std::fabs(spec[0]) * (2.f / kFftSize);
			for (int k = 1; k < kFftSize / 2; k++) {
				float re = spec[2 * k], im = spec[2 * k + 1];
				mag[k] = std::sqrt(re * re + im * im) * (2.f / kFftSize);
			}

			auto now = std::chrono::steady_clock::now();
			float dt = std::chrono::duration<float>(now - last).count();
			last = now;
			if (dt > 0.5f) dt = 0.5f;      // a stall must not blow the envelopes

			pj::bandEnergies(&mag[0], kFftSize / 2, edges, pj::kBands,
			                 sr / (float) kFftSize,
			                 params[TILT_PARAM].getValue(), bands);
			follower.step(bands, pj::kBands, dt, 0.18f);
			if (onset.step(follower.v, pj::kBands, dt, 1.f - sens))
				onsetFired.store(true);

			// Three bands for the CV outputs, taken as the mean of the low,
			// middle and top thirds rather than as single bands -- one band is
			// narrow enough that a note between two of them reads as silence.
			float lo = 0.f, md = 0.f, hi = 0.f;
			for (int i = 0; i < pj::kBands / 3; i++) lo += follower.v[i];
			for (int i = pj::kBands / 3; i < 2 * pj::kBands / 3; i++) md += follower.v[i];
			for (int i = 2 * pj::kBands / 3; i < pj::kBands; i++) hi += follower.v[i];
			float third = (float) (pj::kBands / 3);
			bandLow.store(lo / third);
			bandMid.store(md / third);
			bandHigh.store(hi / third);

			bool frozen = inputs[FREEZE_INPUT].isConnected()
				&& inputs[FREEZE_INPUT].getVoltage() >= 1.f;

			if (!frozen) {
				pj::Look look;
				look.hue = knobCv(HUE_PARAM, HUE_CV_INPUT);
				look.sat = params[SAT_PARAM].getValue();
				look.scale = knobCv(SCALE_PARAM, SCALE_CV_INPUT);
				look.warp = knobCv(WARP_PARAM, WARP_CV_INPUT);
				look.trail = params[TRAIL_PARAM].getValue();

				bool flashHigh = inputs[FLASH_INPUT].isConnected()
					&& inputs[FLASH_INPUT].getVoltage() >= 1.f;
				if (flashHigh)
					flash = 1.f;
				else
					flash *= std::exp(-dt / 0.08f);
				look.flash = flash;

				int mode = (int) std::round(knobCv(MODE_PARAM, MODE_CV_INPUT) * 2.f);
				mode = clamp(mode, 0, 2);
				phase += dt * (0.2f + look.warp * 1.2f);

				pj::fade(cv, look.trail, dt);
				if (mode == 0) {
					for (int i = 0; i < kHop; i++) {
						uint64_t k = read + (uint64_t)(kFftSize - kHop + i);
						xs[i] = ringL[k & (kRing - 1)] * (0.5f + sens * 4.f);
						ys[i] = ringR[k & (kRing - 1)] * (0.5f + sens * 4.f);
					}
					pj::renderScope(cv, &xs[0], &ys[0], kHop, look);
				}
				else if (mode == 1) {
					pj::renderBars(cv, follower.v, pj::kBands, look);
				}
				else {
					pj::renderField(cv, follower.v, pj::kBands, phase, look);
				}
				pj::applyFlash(cv, look.flash);

				videobus::bus().publish(id, W, H, &canvas[0]);
				{
					std::lock_guard<std::mutex> lock(previewMu);
					if (preview.size() != canvas.size())
						preview.resize(canvas.size());
					std::memcpy(&preview[0], &canvas[0], canvas.size());
					previewW = W;
					previewH = H;
				}
				previewSeq.fetch_add(1);
			}

			read += kHop;
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and
// src/Projection/Panel.hpp, generated by tools/panels/Projection.py.

/** The preview. Shows what is going out, so the module can be set up without
    switching to whatever is receiving it. */
struct ProjectionScreen : widget::Widget {
	Projection* module = NULL;
	int image = -1;
	int imageW = 0, imageH = 0;
	uint64_t seen = 0;
	NVGcontext* imageVg = NULL;
	std::vector<uint8_t> local;

	~ProjectionScreen() override {
		if (image >= 0 && imageVg && APP->window
				&& (imageVg == APP->window->vg || imageVg == APP->window->fbVg))
			nvgDeleteImage(imageVg, image);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		if (!module) {
			panel::TextStyle st(panel::Face::Mono, 9.f,
				panel::alpha(panel::SAGE, 0.55f),
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(args.vg, st, box.size.x / 2, box.size.y / 2, "NO SIGNAL");
			return;
		}

		uint64_t now = module->previewSeq.load();
		if (now != seen) {
			std::lock_guard<std::mutex> lock(module->previewMu);
			if (module->previewW > 0 && !module->preview.empty()) {
				if (local.size() != module->preview.size())
					local.resize(module->preview.size());
				std::memcpy(&local[0], &module->preview[0], local.size());
				if (image >= 0 && imageVg == args.vg
				    && (imageW != module->previewW || imageH != module->previewH)) {
					nvgDeleteImage(args.vg, image);
					image = -1;
				}
				if (image < 0) {
					image = nvgCreateImageRGBA(args.vg, module->previewW,
						module->previewH, 0, &local[0]);
					imageVg = args.vg;
					imageW = module->previewW;
					imageH = module->previewH;
				}
				else if (imageVg == args.vg) {
					nvgUpdateImage(args.vg, image, &local[0]);
				}
			}
			seen = now;
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
};


struct ProjectionWidget : ModuleWidget {
	ProjectionWidget(Projection* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Projection.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		ProjectionScreen* s = new ProjectionScreen;
		s->module = module;
		s->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		s->box.size = math::Vec(panel::mm(panel::GLASS_W, 0.f).x,
		                        panel::mm(0.f, panel::GLASS_H).y);
		addChild(s);

		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::SENS_POS.x, panel::SENS_POS.y), module, Projection::SENS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::TILT_POS.x, panel::TILT_POS.y), module, Projection::TILT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::MODE_POS.x, panel::MODE_POS.y), module, Projection::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::SCALE_POS.x, panel::SCALE_POS.y), module, Projection::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::WARP_POS.x, panel::WARP_POS.y), module, Projection::WARP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::HUE_POS.x, panel::HUE_POS.y), module, Projection::HUE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::TRAIL_POS.x, panel::TRAIL_POS.y), module, Projection::TRAIL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::SAT_POS.x, panel::SAT_POS.y), module, Projection::SAT_PARAM));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::AUDIO_L_POS.x, panel::AUDIO_L_POS.y), module, Projection::AUDIO_L_INPUT));
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::AUDIO_R_POS.x, panel::AUDIO_R_POS.y), module, Projection::AUDIO_R_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MODE_CV_POS.x, panel::MODE_CV_POS.y), module, Projection::MODE_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SCALE_CV_POS.x, panel::SCALE_CV_POS.y), module, Projection::SCALE_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::WARP_CV_POS.x, panel::WARP_CV_POS.y), module, Projection::WARP_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::HUE_CV_POS.x, panel::HUE_CV_POS.y), module, Projection::HUE_CV_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::FLASH_POS.x, panel::FLASH_POS.y), module, Projection::FLASH_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::FREEZE_POS.x, panel::FREEZE_POS.y), module, Projection::FREEZE_INPUT));

		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::LOW_OUT_POS.x, panel::LOW_OUT_POS.y), module, Projection::LOW_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MID_OUT_POS.x, panel::MID_OUT_POS.y), module, Projection::MID_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::HIGH_OUT_POS.x, panel::HIGH_OUT_POS.y), module, Projection::HIGH_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::ONSET_OUT_POS.x, panel::ONSET_OUT_POS.y), module, Projection::ONSET_OUT_OUTPUT));
	}
};


Model* modelProjection = createModel<Projection, ProjectionWidget>("Projection");
