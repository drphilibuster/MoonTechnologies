// Ledger: an eight-track sequencer whose generator is Shoal (Ormer Modular, MIT).
//
// The parts:
//   Shoal.hpp          the generator: Shoal's sequencing code, held frame-identical to
//                      the original by tests/Ledger/test_golden
//   Modulation.hpp     CV and the mod matrix, pitch standards, poly hubs
//   Pattern.hpp        written patterns, slots, capture, the pattern generator
//   Player.hpp         the event sources: a pattern's notes, a generator read back as notes
//   Events.hpp         what moves along a track: notes with ids, lane values
//   Effects.hpp        the nineteen effects and the chain they sit in
//   Voices.hpp         notes to gates and pitches
//   Midi.hpp           MIDI in and out, clock out, where a recorded note lands
//   LedgerModule.hpp   the Module: books, slots, launches, outputs
//   Song.hpp           rows, launch syncs in ticks, the song
//   LedgerDisplay.hpp  the display's five pages
//   LedgerUndo.hpp     undo for the display's edits, through Rack's history
//   this file          the panel's widgets and the context menu
#include "../plugin.hpp"
#include "Panel.hpp"
#include "LedgerDisplay.hpp"

struct LedgerWidget : ModuleWidget {
	LedgerWidget(Ledger* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ledger.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		LedgerDisplay* display = new LedgerDisplay;
		display->module = module;
		display->box = panel::mmRect(panel::GLASS_X, panel::GLASS_Y, panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		const Vec pots[Ledger::NUM_POTS] = { panel::CHANCE_POS, panel::NOTE_POS, panel::OCTAVE_POS,
			panel::LENG_POS, panel::RATE_POS, panel::DIRN_POS, panel::TRNS_POS, panel::SHFT_POS, panel::OCTA_POS,
			panel::EVOLVE_POS, panel::BREATHE_POS, panel::GATE_POS, panel::TIE_POS, panel::SLOP_POS };
		const Vec potCv[Ledger::NUM_POTS] = { panel::CHANCE_CV_POS, panel::NOTE_CV_POS, panel::OCTAVE_CV_POS,
			panel::LENG_CV_POS, panel::RATE_CV_POS, panel::DIRN_CV_POS, panel::TRNS_CV_POS, panel::SHFT_CV_POS,
			panel::OCTA_CV_POS, panel::EVOLVE_CV_POS, panel::BREATHE_CV_POS, panel::GATE_CV_POS, panel::TIE_CV_POS,
			panel::SLOP_CV_POS };
		for (int i = 0; i < Ledger::NUM_POTS; i++) {
			addParam(createParamCentered<RoundBlackKnob>(panel::mm(pots[i].x, pots[i].y), module, Ledger::POT_PARAM + i));
			addInput(createInputCentered<panel::PortIn>(panel::mm(potCv[i].x, potCv[i].y), module, Ledger::POT_CV_INPUT + i));
		}
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::SCALE_POS.x, panel::SCALE_POS.y), module, Ledger::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::ROOT_POS.x, panel::ROOT_POS.y), module, Ledger::ROOT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::WEIGHT_POS.x, panel::WEIGHT_POS.y), module, Ledger::WEIGHT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::BPM_POS.x, panel::BPM_POS.y), module, Ledger::BPM_PARAM));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SCALE_CV_POS.x, panel::SCALE_CV_POS.y), module, Ledger::SCALE_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::ROOT_CV_POS.x, panel::ROOT_CV_POS.y), module, Ledger::ROOT_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::WEIGHT_CV_POS.x, panel::WEIGHT_CV_POS.y), module, Ledger::WEIGHT_CV_INPUT));

		const Vec trk[S::kNumTracks] = { panel::TRK1_POS, panel::TRK2_POS, panel::TRK3_POS, panel::TRK4_POS,
			panel::TRK5_POS, panel::TRK6_POS, panel::TRK7_POS, panel::TRK8_POS };
		for (int t = 0; t < S::kNumTracks; t++)
			addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(panel::mm(trk[t].x, trk[t].y),
				module, Ledger::TRK_PARAM + t, Ledger::TRK_LIGHT + t));

		struct Btn { int param; Vec pos; int light; Vec lpos; };
		const Btn btns[] = {
			{ Ledger::MUTE_PARAM, panel::MUTE_POS, Ledger::MUTE_LIGHT, panel::MUTE_LED_POS },
			{ Ledger::SOLO_PARAM, panel::SOLO_POS, Ledger::SOLO_LIGHT, panel::SOLO_LED_POS },
			{ Ledger::RSED_PARAM, panel::RSED_POS, Ledger::RSED_LIGHT, panel::RSED_LED_POS },
			{ Ledger::RUN_PARAM, panel::RUN_POS, Ledger::RUN_LIGHT, panel::RUN_LED_POS },
			{ Ledger::FRZE_PARAM, panel::FRZE_POS, Ledger::FRZE_LIGHT, panel::FRZE_LED_POS },
			{ Ledger::RSET_PARAM, panel::RSET_POS, -1, Vec() },
			{ Ledger::PAGE_PARAM + kPageTank, panel::PG_TANK_POS, Ledger::PAGE_LIGHT + kPageTank, panel::PG_TANK_LED_POS },
			{ Ledger::PAGE_PARAM + kPageRoll, panel::PG_ROLL_POS, Ledger::PAGE_LIGHT + kPageRoll, panel::PG_ROLL_LED_POS },
			{ Ledger::PAGE_PARAM + kPageFx, panel::PG_FX_POS, Ledger::PAGE_LIGHT + kPageFx, panel::PG_FX_LED_POS },
			{ Ledger::PAGE_PARAM + kPageSeq, panel::PG_SEQ_POS, Ledger::PAGE_LIGHT + kPageSeq, panel::PG_SEQ_LED_POS },
			{ Ledger::PAGE_PARAM + kPageSong, panel::PG_SONG_POS, Ledger::PAGE_LIGHT + kPageSong, panel::PG_SONG_LED_POS },
			{ Ledger::CAPT_PARAM, panel::CAPT_POS, Ledger::CAPT_LIGHT, panel::CAPT_LED_POS },
			{ Ledger::REC_PARAM, panel::REC_POS, Ledger::REC_LIGHT, panel::REC_LED_POS },
		};
		for (const Btn& b : btns) {
			addParam(createParamCentered<VCVButton>(panel::mm(b.pos.x, b.pos.y), module, b.param));
			if (b.light >= 0)
				addChild(createLightCentered<SmallLight<panel::LimeLight> >(panel::mm(b.lpos.x, b.lpos.y), module, b.light));
		}

		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::CLK_IN_POS.x, panel::CLK_IN_POS.y), module, Ledger::CLK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RST_IN_POS.x, panel::RST_IN_POS.y), module, Ledger::RST_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RUN_IN_POS.x, panel::RUN_IN_POS.y), module, Ledger::RUN_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RESEED_IN_POS.x, panel::RESEED_IN_POS.y), module, Ledger::RESEED_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::FREEZE_IN_POS.x, panel::FREEZE_IN_POS.y), module, Ledger::FREEZE_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SEED_IN_POS.x, panel::SEED_IN_POS.y), module, Ledger::SEED_INPUT));
		const Vec cvs[L::kNumCvSources] = { panel::CV_A_POS, panel::CV_B_POS, panel::CV_C_POS, panel::CV_D_POS };
		for (int i = 0; i < L::kNumCvSources; i++)
			addInput(createInputCentered<panel::PortIn>(panel::mm(cvs[i].x, cvs[i].y), module, Ledger::CV_INPUT + i));

		const Vec pitch[S::kNumTracks] = { panel::PITCH1_POS, panel::PITCH2_POS, panel::PITCH3_POS, panel::PITCH4_POS,
			panel::PITCH5_POS, panel::PITCH6_POS, panel::PITCH7_POS, panel::PITCH8_POS };
		const Vec gate[S::kNumTracks] = { panel::GATE1_POS, panel::GATE2_POS, panel::GATE3_POS, panel::GATE4_POS,
			panel::GATE5_POS, panel::GATE6_POS, panel::GATE7_POS, panel::GATE8_POS };
		for (int t = 0; t < S::kNumTracks; t++) {
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(pitch[t].x, pitch[t].y), module, Ledger::PITCH_OUTPUT + t));
			addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(gate[t].x, gate[t].y), module, Ledger::GATE_OUTPUT + t));
		}
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::VEL_POS.x, panel::VEL_POS.y), module, Ledger::VEL_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MOD_POS.x, panel::MOD_POS.y), module, Ledger::MOD_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::CURRENT_POS.x, panel::CURRENT_POS.y), module, Ledger::CURRENT_OUTPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::EOS_POS.x, panel::EOS_POS.y), module, Ledger::EOS_OUTPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::CLK_OUT_POS.x, panel::CLK_OUT_POS.y), module, Ledger::CLK_OUTPUT));
	}

	// The menu runs on the UI thread; a change lands at the next sample. Matrix
	// slots, standards and the voice settings are plain ints the audio thread reads whole.
	void appendContextMenu(Menu* menu) override {
		Ledger* m = dynamic_cast<Ledger*>(module);
		if (!m) return;
		int t = m->sel;

		auto valueItem = [=](Menu* menu, const std::string& label, int p, const std::string& hint) {
			menu->addChild(valueSubmenu(label, string::f("%d", m->base[p]), hint, m->base[p],
				[=](int v) { m->setBase(p, v); }));
		};

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Track %d (slot %d)", t + 1, m->active[t] + 1)));
		valueItem(menu, "Seed", S::TP(t, S::kTSeed), "0-999 (lands at the loop origin)");
		{
			std::vector<std::string> follow = { "Off" };
			for (int i = 1; i <= S::kNumTracks; i++) follow.push_back(string::f("Track %d", i));
			menu->addChild(createIndexSubmenuItem("Follow (share the seed of)", follow,
				[=]() { return (size_t)m->base[S::TP(t, S::kTSource)]; },
				[=](size_t i) { m->setBase(S::TP(t, S::kTSource), (int)i); }));
		}
		{
			std::vector<std::string> vs;
			for (int i = 1; i <= L::kMaxVoices; i++) vs.push_back(string::f("%d", i));
			menu->addChild(createIndexSubmenuItem("Voices (a pattern's polyphony)", vs,
				[=]() { return (size_t)(m->voices[t].voices - 1); },
				[=](size_t i) { m->voices[t].setVoices((int)i + 1); }));
			std::vector<std::string> al(L::allocNames, L::allocNames + L::kNumAllocs);
			menu->addChild(createIndexSubmenuItem("Voice allocation", al,
				[=]() { return (size_t)m->voices[t].alloc; },
				[=](size_t i) { m->voices[t].alloc = (int)i; }));
			menu->addChild(createBoolMenuItem("Retrigger overlapping notes (otherwise legato)", "",
				[=]() { return m->voices[t].retrig; },
				[=](bool b) { m->voices[t].retrig = b; }));
		}
		valueItem(menu, "Gate level (V)", S::XP(t, S::kXGateVolts), "1-10");
		valueItem(menu, "Pitch scale (%)", S::XP(t, S::kXPitchScale), "5-200");
		valueItem(menu, "Pitch offset (0.1 V)", S::XP(t, S::kXPitchOffset), "-100 to 100");
		{
			std::vector<std::string> stds(L::outStdNames, L::outStdNames + L::kNumOutStds);
			menu->addChild(createIndexSubmenuItem("Pitch standard", stds,
				[=]() { return (size_t)m->outStd[t]; },
				[=](size_t i) { m->outStd[t] = (int)i; }));
		}
		menu->addChild(createSubmenuItem("Mod matrix", "", [=](Menu* sub) {
			for (int i = 0; i < L::kNumMatrixSlots; i++)
				sub->addChild(slotItem(m, t, i));
		}));
		menu->addChild(createSubmenuItem("MIDI", trackMidiSummary(m, t), [=](Menu* sub) { trackMidiMenu(sub, m, t); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Books"));
		menu->addChild(createMenuItem("Reseed all tracks", "", [=]() { m->requestReseedAll = true; }));
		{
			std::vector<std::string> lq(launchNames, launchNames + kNumLaunchQ);
			menu->addChild(createIndexSubmenuItem("Launch slots", lq,
				[=]() { return (size_t)m->launchQ; },
				[=](size_t i) { m->launchQ = (int)i; }));
			std::vector<std::string> tracks;
			for (int i = 1; i <= S::kNumTracks; i++) tracks.push_back(string::f("Track %d", i));
			menu->addChild(createIndexSubmenuItem("...the track whose loop it waits for", tracks,
				[=]() { return (size_t)m->launchTrack; },
				[=](size_t i) { m->launchTrack = (int)i; }));
			menu->addChild(createBoolMenuItem("A launched slot starts at its first step", "",
				[=]() { return m->launchRestart; },
				[=](bool b) { m->launchRestart = b; }));
		}
		menu->addChild(createBoolMenuItem("Play the song", "",
			[=]() { return m->songOn.load(); },
			[=](bool b) { m->songOn = b; }));
		menu->addChild(createIndexSubmenuItem("Frozen clock out", { "Stops", "Runs" },
			[=]() { return (size_t)m->base[S::kGFreezeClock]; },
			[=](size_t i) { m->setBase(S::kGFreezeClock, (int)i); }));
		{
			std::vector<std::string> hubs(L::hubNames, L::hubNames + L::kNumHubs);
			menu->addChild(createIndexSubmenuItem("Poly output routing", hubs,
				[=]() { return (size_t)m->hubMode; },
				[=](size_t i) { m->hubMode = (int)i; }));
		}
		menu->addChild(createBoolMenuItem("Currents ±5 V (otherwise 0-10 V)", "",
			[=]() { return m->currentsBipolar; },
			[=](bool b) { m->currentsBipolar = b; }));
		menu->addChild(createSubmenuItem("MIDI", "", [=](Menu* sub) { midiMenu(sub, m); }));
		menu->addChild(createSubmenuItem("Recording", m->recOn ? "REC" : "", [=](Menu* sub) {
			sub->addChild(createBoolMenuItem("Record (the REC button)", "",
				[=]() { return m->recOn.load(); },
				[=](bool b) { m->recOn = b; }));
			std::vector<std::string> modes(L::recModeNames, L::recModeNames + L::kNumRecModes);
			sub->addChild(createIndexSubmenuItem("Mode", modes,
				[=]() { return (size_t)m->recMode; },
				[=](size_t i) { m->recMode = (int)i; }));
			sub->addChild(createBoolMenuItem("Punch in (start at the first note played)", "",
				[=]() { return m->punch; },
				[=](bool b) { m->punch = b; }));
			sub->addChild(createMenuLabel("Records the tracks MIDI in plays into, while the clock runs."));
			sub->addChild(createMenuLabel("A generator slot is not written into: CAPTURE it first."));
		}));
	}

	// ---- MIDI menus ----

	static std::string channelName(int c) {
		return c == Ledger::CH_OFF ? "Off" : c == Ledger::CH_ANY ? "Any" : string::f("%d", c);
	}

	static std::string trackMidiSummary(Ledger* m, int t) {
		std::string in = m->inMode == Ledger::IN_SELECTED ? "in when selected" : "in ch " + channelName(m->inCh[t]);
		std::string out = m->outPort[t] < 0 ? "no out" : string::f("%s ch %d", L::midiPortNames[m->outPort[t]], m->outCh[t]);
		return in + ", " + out;
	}

	static void trackMidiMenu(Menu* sub, Ledger* m, int t) {
		std::vector<std::string> chans;
		for (int c = 0; c <= Ledger::CH_ANY; c++) chans.push_back(channelName(c));
		sub->addChild(createIndexSubmenuItem("Input channel (when input goes by channel)", chans,
			[=]() { return (size_t)m->inCh[t]; },
			[=](size_t i) { m->inCh[t] = (int)i; }));
		sub->addChild(valueSubmenu("CC that plays the MOD 1 lane", string::f("%d", m->live[t].modCC), "0-119",
			m->live[t].modCC, [=](int v) { m->live[t].modCC = clamp(v, 0, 119); }));
		sub->addChild(createBoolMenuItem("Follows the transpose leader", "",
			[=]() { return m->followTrans[t]; },
			[=](bool b) { m->followTrans[t] = b; }));
		sub->addChild(new MenuSeparator);
		sub->addChild(createIndexSubmenuItem("Output", { "None", L::midiPortNames[0], L::midiPortNames[1] },
			[=]() { return (size_t)(m->outPort[t] + 1); },
			[=](size_t i) { m->outPort[t] = (int)i - 1; }));
		std::vector<std::string> outs;
		for (int c = 1; c <= 16; c++) outs.push_back(string::f("%d", c));
		sub->addChild(createIndexSubmenuItem("Output channel", outs,
			[=]() { return (size_t)(m->outCh[t] - 1); },
			[=](size_t i) { m->outCh[t] = (int)i + 1; }));
		for (int l = 0; l < L::kModCCs; l++) {
			int cc = m->mmap[t].modCC[l];
			sub->addChild(valueSubmenu(string::f("MOD %d lane sends CC", l + 1), cc < 0 ? "off" : string::f("%d", cc),
				"0-119, or -1 for none", cc, [=](int v) { m->mmap[t].modCC[l] = (int8_t)clamp(v, -1, 119); }));
		}
		sub->addChild(createMenuLabel("BEND sends pitch bend, TOUCH channel pressure"));
	}

	// Driver and device for one port. Changing an output's ends what it was sounding first.
	static void portMenu(Menu* menu, Ledger* m, midi::Port* port, int out) {
		menu->addChild(createMenuLabel("Driver"));
		for (int id : midi::getDriverIds()) {
			midi::Driver* d = midi::getDriver(id);
			if (!d) continue;
			menu->addChild(createCheckMenuItem(d->getName(), "",
				[=]() { return port->getDriverId() == id; },
				[=]() {
					if (out >= 0) m->portFlushWait(out);
					port->setDriverId(id);
					port->channel = -1;
				}));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Device"));
		menu->addChild(createCheckMenuItem("(No device)", "",
			[=]() { return port->getDeviceId() < 0; },
			[=]() { if (out >= 0) m->portFlushWait(out); port->setDeviceId(-1); }));
		for (int id : port->getDeviceIds()) {
			menu->addChild(createCheckMenuItem(port->getDeviceName(id), "",
				[=]() { return port->getDeviceId() == id; },
				[=]() {
					if (out >= 0) m->portFlushWait(out);
					port->setDeviceId(id);
					port->channel = -1;
				}));
		}
	}

	static std::string deviceName(midi::Port* port) {
		int id = port->getDeviceId();
		return id < 0 ? "none" : port->getDeviceName(id);
	}

	static void midiMenu(Menu* sub, Ledger* m) {
		sub->addChild(createSubmenuItem("Input device", deviceName(&m->midiIn), [=](Menu* pm) {
			portMenu(pm, m, &m->midiIn, -1);
		}));
		sub->addChild(createIndexSubmenuItem("Input plays", { "The selected track", "Tracks by their input channel" },
			[=]() { return (size_t)m->inMode; },
			[=](size_t i) { m->inMode = (int)i; }));
		sub->addChild(createBoolMenuItem("Clock from MIDI in (while CLOCK is unpatched)", "",
			[=]() { return m->clockFromMidi; },
			[=](bool b) { m->clockFromMidi = b; }));
		sub->addChild(createBoolMenuItem("Program change launches that row", "",
			[=]() { return m->pcLaunches; },
			[=](bool b) { m->pcLaunches = b; }));
		{
			std::vector<std::string> tc = { "Off" };
			for (int c = 1; c <= 16; c++) tc.push_back(string::f("Channel %d", c));
			sub->addChild(createIndexSubmenuItem("Transpose leader", tc,
				[=]() { return (size_t)m->transCh; },
				[=](size_t i) { m->transCh = (int)i; }));
			sub->addChild(createIndexSubmenuItem("The leader's note", { "Transposes (C4 = none)", "Sets the books' root" },
				[=]() { return (size_t)m->transMode; },
				[=](size_t i) { m->transMode = (int)i; }));
		}
		std::vector<std::string> clk(L::clkOutNames, L::clkOutNames + L::kNumClkOut);
		for (int p = 0; p < L::kMidiPorts; p++) {
			sub->addChild(new MenuSeparator);
			sub->addChild(createSubmenuItem(string::f("%s device", L::midiPortNames[p]), deviceName(&m->midiOut[p]),
				[=](Menu* pm) { portMenu(pm, m, &m->midiOut[p], p); }));
			sub->addChild(createIndexSubmenuItem(string::f("%s clock", L::midiPortNames[p]), clk,
				[=]() { return (size_t)m->clkOut[p]; },
				[=](size_t i) { m->clkOut[p] = (int)i; }));
		}
		sub->addChild(new MenuSeparator);
		sub->addChild(createMenuItem("All notes off", "", [=]() {
			for (int p = 0; p < L::kMidiPorts; p++) m->portFlushWait(p);
		}));
	}

	// A destination's name: one of the track's settings, or an effect's parameter.
	static std::string destName(Ledger* m, int t, int d) {
		if (!L::destIsFx(d)) return d < L::kNumDests ? L::destNames[d] : "?";
		int f = L::destFxSlot(d), p = L::destFxParam(d);
		if (f >= L::kChainSlots) return "?";
		const L::Fx& fx = m->chains[t].fx[f];
		if (p >= fx.desc().nParams) return string::f("FX %d: (none)", f + 1);
		return string::f("FX %d %s: %s", f + 1, fx.desc().shortName, fx.desc().p[p].name);
	}

	static std::string slotSummary(Ledger* m, int t, const L::MatrixSlot& s) {
		if (!s.live()) return "off";
		std::string src = s.source == L::kSrcCC ? string::f("CC %d", s.cc) : L::sourceNames[s.source];
		return string::f("%s > %s %+d%%", src.c_str(), destName(m, t, s.dest).c_str(), s.amount);
	}

	static MenuItem* slotItem(Ledger* m, int t, int i) {
		return createSubmenuItem(string::f("Slot %d", i + 1), slotSummary(m, t, m->matrix[t][i]), [=](Menu* sub) {
			L::MatrixSlot* s = &m->matrix[t][i];
			std::vector<std::string> sources(L::sourceNames, L::sourceNames + L::kNumMatrixSources + 1);
			sub->addChild(createIndexSubmenuItem("Source", sources,
				[=]() { return (size_t)s->source; },
				[=](size_t k) { s->source = (int8_t)k; }));
			if (s->source == L::kSrcCC) {
				sub->addChild(valueSubmenu("CC number", string::f("%d", s->cc), "0-119", s->cc,
					[=](int v) { s->cc = (int8_t)clamp(v, 0, 119); }));
				int me = t * L::kNumMatrixSlots + i;
				sub->addChild(createCheckMenuItem("Learn: the next CC this track receives", "",
					[=]() { return m->learn.load() == me; },
					[=]() { m->learn = m->learn.load() == me ? -1 : me; }));
			}
			sub->addChild(createSubmenuItem("Destination", destName(m, t, s->dest), [=](Menu* dm) {
				for (int d = 0; d < L::kNumDests; d++)
					dm->addChild(createCheckMenuItem(L::destNames[d], "",
						[=]() { return s->dest == d; }, [=]() { s->dest = (int8_t)d; }));
				for (int f = 0; f < L::kChainSlots; f++) {
					const L::Fx& fx = m->chains[t].fx[f];
					if (fx.type == L::kFxNone) continue;
					dm->addChild(new MenuSeparator);
					for (int p = 0; p < fx.desc().nParams; p++) {
						int d = L::fxDest(f, p);
						dm->addChild(createCheckMenuItem(destName(m, t, d), "",
							[=]() { return s->dest == d; }, [=]() { s->dest = (int8_t)d; }));
					}
				}
			}));
			sub->addChild(valueSubmenu("Amount", string::f("%+d%%", s->amount), "-100 to 100 %", s->amount,
				[=](int v) { s->amount = (int16_t)clamp(v, -100, 100); }));
			sub->addChild(createIndexSubmenuItem("Polarity", { "Bipolar (±5 V)", "Increase only (0-10 V)" },
				[=]() { return (size_t)s->unipolar; },
				[=](size_t k) { s->unipolar = k == 1; }));
			sub->addChild(valueSubmenu("Offset", string::f("%+d%%", s->offset), "-100 to 100 % of the range", s->offset,
				[=](int v) { s->offset = (int16_t)clamp(v, -100, 100); }));
			sub->addChild(createMenuItem("Clear slot", "", [=]() { *s = L::MatrixSlot(); }));
		});
	}
};

Model* modelLedger = createModel<Ledger, LedgerWidget>("Ledger");
