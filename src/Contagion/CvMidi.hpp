// Contagion's cable-level MIDI: V/Oct, gate and the rest of a keyboard's controllers, turned into
// the MIDI bytes the Virus's 80C515 reads on its serial port. Self-contained (no Rack) so
// tests/Contagion can drive it with synthetic voltages.
//
// The unit only understands MIDI, so a patch cable has to become MIDI before the firmware sees
// it: a rising gate is a note-on at the pitch and velocity read then, a falling gate its
// note-off, a pitch change under a held gate is a legato note (new on, then old off), and the
// wheel-like inputs send a controller only when it moves.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vc {

struct CvMidi {
	static constexpr int VOICES = 16;

	/** Everything read from the cables for one sample. */
	struct In {
		int voices = 0;                    // channels on the V/Oct cable; 0 when it is unpatched
		float pitch[VOICES] = {};          // volts, 0 V = middle C (note 60)
		float gate[VOICES] = {};
		bool velConnected = false;
		float vel[VOICES] = {};            // 0..10 V
		bool bendConnected = false;
		float bend = 0.f;                  // +-5 V
		bool modConnected = false;
		float mod = 0.f;                   // 0..10 V -> CC 1
		bool atConnected = false;
		float at = 0.f;                    // 0..10 V -> channel pressure
		bool susConnected = false;
		float sus = 0.f;                   // gate -> CC 64
		bool clkConnected = false;
		float clk = 0.f;
		bool runConnected = false;
		float run = 0.f;
		bool rstConnected = false;
		float rst = 0.f;
	};

	// --- settings ---------------------------------------------------------------------------
	int channel = 0;                  // MIDI channel 0-15 the notes and controllers go out on
	bool polyToChannels = false;      // cable channel n goes out on MIDI channel n instead
	int ppqn = 4;                     // clock pulses per quarter note: 1, 2, 4, 8 or 24
	int defaultVelocity = 100;        // when the velocity input is unpatched
	int minSamples = 0;               // a note sounds at least this long, however short its gate: a sequencer's
	                                  // trigger is a millisecond, and an envelope needs far longer to open

	// --- state ------------------------------------------------------------------------------
	struct Edge {
		bool high = false;
		/** Schmitt trigger: true on the rising edge (the way Rack's own triggers read a gate). */
		bool rise(float v) {
			if (!high && v >= 2.f) { high = true; return true; }
			if (high && v <= 0.1f) high = false;
			return false;
		}
		/** True on the falling edge. */
		bool fall(float v) {
			if (high && v <= 0.1f) { high = false; return true; }
			if (!high && v >= 2.f) high = true;
			return false;
		}
	};

	Edge gateHigh[VOICES];
	int held[VOICES];                 // note sounding per voice, -1 for none
	int heldCh[VOICES];               // the MIDI channel it went out on
	int pending[VOICES];              // samples until a rising gate's note is read
	int age[VOICES];                  // samples since the voice's note-on
	int lastBend = 8192, lastMod = 0, lastAt = 0;
	bool bendSent = false, modSent = false, atSent = false, susSent = false;
	bool susHigh = false;
	Edge clkEdge, runEdge, rstEdge;
	bool running = false, startSent = false;
	// The clock: pulses are spread into 24 ppqn ticks across the interval just measured.
	bool clkSeen = false;
	uint32_t sinceClk = 0, period = 0;
	int ticksOwed = 0;                // ticks of the last pulse not yet sent
	uint32_t tickGap = 0, sinceTick = 0;

	CvMidi() { reset(); }

	void reset() {
		for (int i = 0; i < VOICES; i++) { held[i] = -1; heldCh[i] = 0; pending[i] = 0; age[i] = 0; gateHigh[i] = Edge(); }
		bendSent = modSent = atSent = susSent = susHigh = false;
		lastBend = 8192;
		lastMod = lastAt = 0;
		clkEdge = runEdge = rstEdge = Edge();
		running = startSent = false;
		clkSeen = false;
		sinceClk = period = 0;
		ticksOwed = 0;
		tickGap = sinceTick = 0;
	}

	static int noteOf(float volts) { return std::min(127, std::max(0, int(std::floor(60.f + 12.f * volts + 0.5f)))); }
	static int sevenBit(float volts) { return std::min(127, std::max(0, int(std::floor(volts * 12.7f + 0.5f)))); }

	int channelOf(int voice) const { return polyToChannels ? (voice & 15) : (channel & 15); }

	/** Run one sample. send(status, data1, data2, size) is called once per MIDI message: size is
	    1 for real-time bytes, 2 for program/pressure, 3 otherwise. */
	template <class F>
	void process(const In& in, F&& send) {
		// --- notes -----------------------------------------------------------------------
		const int n = std::min(in.voices, int(VOICES));
		for (int c = 0; c < VOICES; c++) {
			if (c >= n) {                                    // no such voice: let go of what it held
				if (held[c] >= 0) { send(0x80 | heldCh[c], held[c], 0, 3); held[c] = -1; }
				gateHigh[c] = Edge();
				pending[c] = 0;
				continue;
			}
			if (held[c] >= 0 && age[c] < (1 << 30)) age[c]++;
			const bool rose = gateHigh[c].rise(in.gate[c]);
			const bool gate = gateHigh[c].high;
			if (rose) {
				if (held[c] >= 0) { send(0x80 | heldCh[c], held[c], 0, 3); held[c] = -1; }
				pending[c] = 1;                              // a sequencer moves pitch with the gate: read it a sample on
			} else if (pending[c] > 0 && --pending[c] == 0) {     // even if the gate has already gone: a trigger is a note
				const int note = noteOf(in.pitch[c]);
				const int vel = in.velConnected ? std::max(1, sevenBit(in.vel[c])) : defaultVelocity;
				heldCh[c] = channelOf(c);
				send(0x90 | heldCh[c], note, vel, 3);
				held[c] = note;
				age[c] = 0;
			} else if (gate && held[c] >= 0) {
				const int note = noteOf(in.pitch[c]);
				if (note != held[c]) {                       // legato: the new note before the old one is released
					const int vel = in.velConnected ? std::max(1, sevenBit(in.vel[c])) : defaultVelocity;
					send(0x90 | heldCh[c], note, vel, 3);
					send(0x80 | heldCh[c], held[c], 0, 3);
					held[c] = note;
					age[c] = 0;
				}
			} else if (!gate) {
				// the gate is down; the note comes off when it has sounded for its minimum
				if (held[c] >= 0 && age[c] >= minSamples) { send(0x80 | heldCh[c], held[c], 0, 3); held[c] = -1; }
			}
		}

		// --- wheels ----------------------------------------------------------------------
		const int ch = channel & 15;
		if (in.bendConnected) {
			const int v = std::min(16383, std::max(0, int(std::floor(8192.f + in.bend / 5.f * 8192.f + 0.5f))));
			if (!bendSent || v != lastBend) { send(0xE0 | ch, v & 127, v >> 7, 3); lastBend = v; bendSent = true; }
		} else if (bendSent) {
			send(0xE0 | ch, 0, 64, 3);
			bendSent = false;
			lastBend = 8192;
		}
		if (in.modConnected) {
			const int v = sevenBit(in.mod);
			if (!modSent || v != lastMod) { send(0xB0 | ch, 1, v, 3); lastMod = v; modSent = true; }
		} else if (modSent) {
			send(0xB0 | ch, 1, 0, 3);
			modSent = false;
			lastMod = 0;
		}
		if (in.atConnected) {
			const int v = sevenBit(in.at);
			if (!atSent || v != lastAt) { send(0xD0 | ch, v, 0, 2); lastAt = v; atSent = true; }
		} else if (atSent) {
			send(0xD0 | ch, 0, 0, 2);
			atSent = false;
			lastAt = 0;
		}
		if (in.susConnected) {
			const bool on = in.sus >= (susHigh ? 0.1f : 2.f);
			if (!susSent || on != susHigh) { send(0xB0 | ch, 64, on ? 127 : 0, 3); susHigh = on; susSent = true; }
		} else if (susSent) {
			send(0xB0 | ch, 64, 0, 3);
			susSent = false;
			susHigh = false;
		}

		// --- transport -------------------------------------------------------------------
		bool started = false;
		if (in.runConnected) {
			if (runEdge.rise(in.run)) { send(0xFA, 0, 0, 1); running = started = true; }
			else if (running && !runEdge.high) { send(0xFC, 0, 0, 1); running = false; }
		} else if (in.clkConnected && !startSent) {
			send(0xFA, 0, 0, 1);                             // no run cable: the first clock starts it
			startSent = running = started = true;
		}
		if (!in.runConnected && !in.clkConnected) startSent = false;
		if (in.rstConnected && rstEdge.rise(in.rst) && !started && (running || !in.runConnected))
			send(0xFA, 0, 0, 1);                             // start again from the top

		// --- clock -----------------------------------------------------------------------
		if (!in.clkConnected) { clkEdge = Edge(); clkSeen = false; period = 0; ticksOwed = 0; sinceClk = 0; return; }
		const int perPulse = std::max(1, 24 / std::max(1, ppqn));
		if (sinceClk < 1u << 30) sinceClk++;
		if (clkEdge.rise(in.clk)) {
			while (ticksOwed > 0) { send(0xF8, 0, 0, 1); ticksOwed--; }   // the last pulse's remainder, so none is lost
			if (clkSeen) period = sinceClk;                  // the interval since the last pulse
			clkSeen = true;
			sinceClk = 0;
			send(0xF8, 0, 0, 1);
			ticksOwed = perPulse - 1;
			tickGap = period ? std::max<uint32_t>(1, period / perPulse) : 0;
			sinceTick = 0;
		} else if (ticksOwed > 0 && tickGap) {
			if (++sinceTick >= tickGap) { send(0xF8, 0, 0, 1); ticksOwed--; sinceTick = 0; }
		}
		if (period && sinceClk > period * 4) { ticksOwed = 0; period = 0; clkSeen = false; }   // the clock stopped
	}
};

} // namespace vc
