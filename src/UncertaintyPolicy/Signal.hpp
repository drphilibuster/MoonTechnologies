#pragma once
#include <rack.hpp>

#include "Roles.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>


namespace upol {


enum class Kind {
	Unknown,
	Silent,   // nothing there
	Static,   // steady DC, parked
	Audio,    // audio-rate, bipolar
	Pitch,    // 1V/oct-ish control voltage
	SlowCV,   // envelopes, LFOs, drifting modulation
	Gate,     // two-state, sustained
	Trigger,  // two-state, brief
};


inline const char* kindName(Kind k) {
	switch (k) {
		case Kind::Silent: return "silent";
		case Kind::Static: return "dc";
		case Kind::Audio: return "audio";
		case Kind::Pitch: return "pitch";
		case Kind::SlowCV: return "cv";
		case Kind::Gate: return "gate";
		case Kind::Trigger: return "trig";
		default: return "?";
	}
}


/** Running statistics for one output port.
    Written by the audio thread inside process(), read by the UI thread once the
    window closes. Plain floats rather than atomics: the UI thread only reads
    after observing `done`, which is released by the audio thread. */
struct PortStats {
	float minV = 0.f;
	float maxV = 0.f;
	double sum = 0.0;
	double sumSq = 0.0;
	int64_t samples = 0;
	int64_t crossings = 0;   // transitions across the midpoint
	int64_t highSamples = 0; // samples above +1 V, i.e. a gate's "on" time
	float prev = 0.f;
	bool first = true;

	void reset() {
		*this = PortStats();
	}

	inline void push(float v) {
		if (first) {
			minV = maxV = prev = v;
			first = false;
		}
		minV = std::min(minV, v);
		maxV = std::max(maxV, v);
		sum += v;
		sumSq += (double) v * (double) v;
		// Midpoint crossing measured against a fixed 0 V, which is where audio
		// and bipolar LFOs sit; unipolar gates are caught by the two-state test.
		if ((prev < 0.f) != (v < 0.f))
			crossings++;
		// Absolute threshold rather than a share of the range, so duty cycle is
		// meaningful on the first pass without knowing the range up front.
		if (v > 1.f)
			highSamples++;
		prev = v;
		samples++;
	}
};


/** Everything the classifier learned about one output port. */
struct PortProfile {
	Kind kind = Kind::Unknown;
	float rangeV = 0.f;
	float rms = 0.f;
	float freqHz = 0.f;
	float duty = 0.f;
};


/** Turn a window of samples into a signal kind.
    `nameHint` is the port's configOutput() label, used only where measurement is
    genuinely ambiguous -- telling 1V/oct pitch from any other slow CV is not
    something a voltage window can settle on its own. */
inline PortProfile classify(const PortStats& st, float sampleRate, const std::string& nameHint) {
	PortProfile p;
	if (st.samples <= 0 || sampleRate <= 0.f)
		return p;

	const double n = (double) st.samples;
	const double mean = st.sum / n;
	const double meanSq = st.sumSq / n;

	p.rangeV = st.maxV - st.minV;
	p.rms = (float) std::sqrt(meanSq);
	const float windowSec = (float) (n / (double) sampleRate);
	p.freqHz = windowSec > 0.f ? (float) st.crossings / (2.f * windowSec) : 0.f;
	p.duty = (float) ((double) st.highSamples / n);

	const std::string nm = lower(nameHint);

	// Nothing moving at all.
	if (p.rangeV < 1e-3f) {
		p.kind = (std::fabs(mean) < 1e-3) ? Kind::Silent : Kind::Static;
		return p;
	}

	if (p.freqHz > 30.f && p.rangeV > 0.5f) {
		p.kind = Kind::Audio;
		return p;
	}

	// Gate/trigger family: unipolar, large swing, spends its time at the rails.
	if (st.minV > -0.5f && p.rangeV > 1.5f && (p.duty > 0.9f || p.duty < 0.6f)) {
		if (p.duty > 0.f && p.duty < 0.12f) {
			p.kind = Kind::Trigger;
			return p;
		}
		if (mentions(nm, {"gate", "trig", "clock", "clk", "eoc", "eof", "sync", "reset"})) {
			p.kind = (p.duty < 0.12f) ? Kind::Trigger : Kind::Gate;
			return p;
		}
	}

	if (mentions(nm, {"v/oct", "v/o", "1v/oct", "pitch", "note", "cv out"})) {
		p.kind = Kind::Pitch;
		return p;
	}

	p.kind = Kind::SlowCV;
	return p;
}


/** An input port with its name already parsed.

    Scoring every source against every input is quadratic, so the string work is
    hoisted out of the inner loop and done once per input per roll: on a large
    patch this is the difference between a visible frame hitch and no cost worth
    measuring. */
struct InputSlot {
	rack::engine::Module* mod = nullptr;
	int64_t modId = -1;
	int port = 0;
	bool isPitch = false;
	bool isClock = false;
	bool isTrig = false;
	bool isAudio = false;
	bool isMod = false;
	/** No flag above fired: the port's name told us nothing. configInput() is
	    optional and plenty of modules skip it, so this is common rather than
	    exotic -- and a port we cannot read should not be scored as though we
	    could. */
	bool unlabelled = false;
	RoleMask destRoles = ROLE_NONE;
};


inline InputSlot makeSlot(rack::engine::Module* m, int64_t modId, int port,
                          const std::string& inNameRaw, RoleMask destRoles) {
	InputSlot s;
	s.mod = m;
	s.modId = modId;
	s.port = port;
	s.destRoles = destRoles;
	const std::string in = lower(inNameRaw);
	s.isPitch = mentions(in, {"v/oct", "v/o", "pitch", "note"});
	s.isClock = mentions(in, {"clock", "clk", "tempo"});
	s.isTrig = mentions(in, {"trig", "gate", "reset", "rst", "start", "stop",
	                         "step", "ping", "sync", "strike", "run"});
	s.isAudio = mentions(in, {"audio", "signal", "sig ", "in l", "in r", "left",
	                          "right", " in", "input", "carrier", "sidechain"});
	s.isMod = mentions(in, {"cv", "mod", "fm", "am", "pm", "cutoff", "freq",
	                        "res", "depth", "amount", "amt", "level", "gain",
	                        "pan", "shape", "wave", "index", "morph", "time",
	                        "feedback", "mix"});
	s.unlabelled = !(s.isPitch || s.isClock || s.isTrig || s.isAudio || s.isMod);
	return s;
}


/** How sensible is it to send a signal of `kind` into this input?
    0 means never, 1 means an obvious fit.

    `blindWeight` is what an unreadable port scores. It is a parameter rather
    than a field on the slot because it is policy, not a property of the port --
    the slot describes what is there, this decides what to do about it. The
    caller rejects at or below 0.05, so the setting has a genuine "never" at the
    bottom of its range rather than a merely small number. */
inline float affinity(Kind kind, const InputSlot& s, float blindWeight) {
	// Nothing in the name to go on: one number, not the middling default of
	// whichever arm below happened to fall through.
	if (s.unlabelled)
		return blindWeight;

	switch (kind) {
		case Kind::Audio:
			if (s.isPitch) return 0.f;      // audio into V/Oct is a scream, not a patch
			if (s.isTrig || s.isClock) return 0.05f;
			if (s.isAudio) return 1.f;
			if (s.destRoles & (ROLE_PROCESSOR | ROLE_MIX)) return 0.8f;
			if (s.isMod) return 0.5f;       // audio-rate modulation is legitimate
			return 0.2f;

		case Kind::Pitch:
			if (s.isPitch) return 1.f;
			if (s.isTrig || s.isClock) return 0.f;
			if (s.isMod) return 0.4f;
			return 0.15f;

		case Kind::Gate:
		case Kind::Trigger:
			if (s.isClock) return kind == Kind::Trigger ? 1.f : 0.7f;
			if (s.isTrig) return 1.f;
			if (s.isPitch) return 0.f;
			if (s.isAudio) return 0.1f;
			if (s.isMod) return 0.35f;
			return 0.2f;

		case Kind::SlowCV:
			if (s.isPitch) return 0.25f;    // slow drift into pitch is musical
			if (s.isTrig || s.isClock) return 0.05f;
			if (s.isMod) return 1.f;
			if (s.isAudio) return 0.2f;
			return 0.4f;

		case Kind::Static:
			// A flat score here used to make DC a live candidate on every input
			// in the patch. Into a gate it latches an envelope open and the
			// patch drones, permanently and silently -- the audition could not
			// see it either, because a drone is loud and unclipped.
			if (s.isTrig || s.isClock) return 0.f;
			if (s.isAudio) return 0.f;      // an offset is not a signal
			if (s.isPitch) return 0.5f;     // a constant transpose is a patch
			if (s.isMod) return 0.45f;      // what an attenuverter is for
			return 0.1f;

		case Kind::Silent:
			return 0.f;                     // patching silence achieves nothing
		default:
			return 0.15f;
	}
}


} // namespace upol
