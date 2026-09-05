#pragma once
#include <rack.hpp>
// Not reachable through rack.hpp, which is the documented entry point -- but the
// header ships in the SDK's public include directory and tag::getTag/tagAliases
// are exported from libRack, which the plugin already links. The exposure is
// that Rack could move the file, and that would be a loud compile error rather
// than a silent misbehaviour. Worth it: this is the only route to a module's
// declared function.
#include <tag.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


namespace upol {


/** What a module is *for*, as a bitmask.

    Rack ships a taxonomy of modular synthesis and it is worth reading: the tag
    list in Rack/src/tag.cpp is the classic voice architecture written down --
    sources feed processors feed a mix, a timing spine drives the whole thing and
    a modulation layer bends it. Knowing which of those a module is turns "pick a
    cable at random" into "pick a cable that means something".

    A mask rather than an enum because the taxonomy genuinely overlaps, and the
    overlaps are the useful part: a low-pass gate is a processor AND an envelope,
    a sequencer is timing AND pitch, a synth voice is all three of source,
    processor and envelope. */
using RoleMask = uint32_t;

enum Role : RoleMask {
	ROLE_NONE       = 0,
	ROLE_SOURCE     = 1u << 0,   // makes sound from nothing
	ROLE_PROCESSOR  = 1u << 1,   // takes sound and changes it
	ROLE_MIX        = 1u << 2,   // sums, pans, attenuates
	ROLE_TIMING     = 1u << 3,   // decides when things happen
	ROLE_CLOCKROOT  = 1u << 4,   // ... and is where time itself starts
	ROLE_ENVELOPE   = 1u << 5,   // shapes an event into a contour
	ROLE_MODULATION = 1u << 6,   // moves on its own, unasked
	ROLE_PITCH      = 1u << 7,   // deals in notes
	ROLE_UTILITY    = 1u << 8,   // plumbing
	ROLE_INERT      = 1u << 9,   // nothing to patch here
	ROLE_IO         = 1u << 10,  // the world outside the rack
	/** A control surface: PatchMaster, a mapper, a bank of manual knobs.

	    Worth its own bit rather than being filed under IO, because it is the
	    opposite case. IO is the edge of the rack and its knobs are settings --
	    a MIDI channel, a device. A surface's knobs are the patch's front panel:
	    they are what the person who built it decided you should be allowed to
	    touch, and they are the single most rewarding thing in the rack to
	    randomise. */
	ROLE_SURFACE    = 1u << 11,
};


/** The roles that actually answer "what does this module do?".

    A module tagged only "Polyphonic, Digital" has told us nothing, and that is
    the case the text fallback exists for -- so the test for "did the tags teach
    us anything" has to be this mask, not merely a non-empty tag list. */
static const RoleMask ROLE_STRUCTURAL =
        ROLE_SOURCE | ROLE_PROCESSOR | ROLE_MIX | ROLE_TIMING | ROLE_ENVELOPE |
        ROLE_MODULATION | ROLE_PITCH | ROLE_INERT | ROLE_IO | ROLE_SURFACE;


// String helpers live here rather than in Signal.hpp because this is the lowest
// header in the plugin and the tag matching below needs them first.
inline std::string lower(std::string s) {
	for (char& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}


inline bool mentions(const std::string& hay, std::initializer_list<const char*> needles) {
	for (const char* n : needles) {
		if (hay.find(n) != std::string::npos)
			return true;
	}
	return false;
}


/** Roles for one canonical tag name, already lower-cased.

    Keyed on the canonical alias -- tag::getTag() returns exactly these strings.
    Digital, Dual, Quad, Polyphonic and Hardware clone describe how a module is
    built rather than what it does, so they map to nothing on purpose. */
inline RoleMask roleFromTagName(const std::string& canonical) {
	struct Entry { const char* tag; RoleMask roles; };
	static const Entry TABLE[] = {
		{"arpeggiator",                 ROLE_TIMING},
		{"attenuator",                  ROLE_MIX},
		{"blank",                       ROLE_INERT},
		{"chorus",                      ROLE_PROCESSOR},
		{"clock generator",             ROLE_TIMING | ROLE_CLOCKROOT},
		{"clock modulator",             ROLE_TIMING},
		{"compressor",                  ROLE_PROCESSOR},
		{"controller",                  ROLE_SURFACE},
		{"delay",                       ROLE_PROCESSOR},
		{"distortion",                  ROLE_PROCESSOR},
		{"drum",                        ROLE_SOURCE},
		{"dynamics",                    ROLE_PROCESSOR},
		{"effect",                      ROLE_PROCESSOR},
		{"envelope follower",           ROLE_ENVELOPE},
		{"envelope generator",          ROLE_ENVELOPE},
		{"equalizer",                   ROLE_PROCESSOR},
		{"expander",                    ROLE_INERT},
		{"external",                    ROLE_IO},
		{"filter",                      ROLE_PROCESSOR},
		{"flanger",                     ROLE_PROCESSOR},
		{"function generator",          ROLE_ENVELOPE},
		{"granular",                    ROLE_SOURCE},
		{"limiter",                     ROLE_PROCESSOR},
		{"logic",                       ROLE_TIMING},
		{"low-frequency oscillator",    ROLE_MODULATION},
		{"low-pass gate",               ROLE_PROCESSOR | ROLE_ENVELOPE},
		{"midi",                        ROLE_IO},
		{"mixer",                       ROLE_MIX},
		{"multiple",                    ROLE_UTILITY},
		{"noise",                       ROLE_SOURCE},
		{"oscillator",                  ROLE_SOURCE},
		{"panning",                     ROLE_MIX},
		{"phaser",                      ROLE_PROCESSOR},
		{"physical modeling",           ROLE_SOURCE},
		{"quantizer",                   ROLE_PITCH},
		{"random",                      ROLE_MODULATION},
		{"recording",                   ROLE_IO},
		{"reverb",                      ROLE_PROCESSOR},
		{"ring modulator",              ROLE_PROCESSOR},
		{"sample and hold",             ROLE_MODULATION},
		{"sampler",                     ROLE_SOURCE},
		{"sequencer",                   ROLE_TIMING | ROLE_PITCH},
		{"slew limiter",                ROLE_ENVELOPE},
		{"speech",                      ROLE_SOURCE},
		{"switch",                      ROLE_UTILITY},
		{"synth voice",                 ROLE_SOURCE | ROLE_PROCESSOR | ROLE_ENVELOPE},
		{"tuner",                       ROLE_INERT},
		{"utility",                     ROLE_UTILITY},
		{"visual",                      ROLE_INERT},
		{"vocoder",                     ROLE_PROCESSOR},
		{"voltage-controlled amplifier", ROLE_PROCESSOR},
		{"waveshaper",                  ROLE_PROCESSOR},
	};
	for (const Entry& e : TABLE) {
		if (canonical == e.tag)
			return e.roles;
	}
	return ROLE_NONE;
}


/** Roles from a Model's declared tags.

    Model.hpp warns that tag IDs are not part of the ABI and may change at any
    time, and that is true -- so an ID is never compared against a literal here.
    It is bounds-checked (tag::getTag asserts out of range, and these IDs arrive
    from foreign plugins) and resolved to its canonical name, which is the
    published manifest vocabulary and is as stable as anything in Rack. */
inline RoleMask rolesFromTags(const rack::plugin::Model* model) {
	if (!model)
		return ROLE_NONE;
	const int tagCount = (int) rack::tag::tagAliases.size();
	RoleMask roles = ROLE_NONE;
	for (int id : model->tagIds) {
		if (id < 0 || id >= tagCount)
			continue;
		roles |= roleFromTagName(lower(rack::tag::getTag(id)));
	}
	return roles;
}


/** Roles guessed from a module's name and one-line description.

    The weaker path, for the many modules that declare no useful tags. Same
    vocabulary, matched against prose -- so it fires on any module whose blurb
    happens to say "delay" and misses every filter that does not say so. Good
    enough to be worth having, never good enough to prefer. */
inline RoleMask rolesFromText(const std::string& textRaw) {
	const std::string t = lower(textRaw);
	RoleMask roles = ROLE_NONE;
	if (mentions(t, {"oscillator", "vco", "noise", "sampler", "drum", "granular",
	                 "physical model", "synth voice"}))
		roles |= ROLE_SOURCE;
	if (mentions(t, {"filter", "vcf", "vca", "amplifier", "low-pass gate", "lowpass gate",
	                 "waveshaper", "wavefolder", "distortion", "overdrive", "saturat",
	                 "ring mod", "effect", "reverb", "delay", "chorus", "phaser",
	                 "flanger", "equalizer", "compressor", "limiter", "vocoder"}))
		roles |= ROLE_PROCESSOR;
	if (mentions(t, {"mixer", "attenuator", "attenuvert", "panner", "panning", "crossfad"}))
		roles |= ROLE_MIX;
	if (mentions(t, {"clock", "sequencer", "arpeggi", "logic", "trigger sequence"}))
		roles |= ROLE_TIMING;
	if (mentions(t, {"clock generator", "master clock"}))
		roles |= ROLE_CLOCKROOT;
	if (mentions(t, {"envelope", "adsr", "function generator", "slew"}))
		roles |= ROLE_ENVELOPE;
	if (mentions(t, {"lfo", "low-frequency", "low frequency osc", "random",
	                 "sample and hold", "sample & hold"}))
		roles |= ROLE_MODULATION;
	if (mentions(t, {"quantizer", "quantiser"}))
		roles |= ROLE_PITCH;
	if (mentions(t, {"blank", "spacer", "tuner", "scope", "oscilloscope", "expander"}))
		roles |= ROLE_INERT;
	if (mentions(t, {"midi", "audio interface", "recorder", "cv-gate"}))
		roles |= ROLE_IO;
	if (mentions(t, {"controller", "control surface", "mapper", "remote"}))
		roles |= ROLE_SURFACE;
	return roles;
}


/** Tags first, prose only where the tags said nothing about function. */
inline RoleMask rolesOf(rack::engine::Module* m) {
	if (!m || !m->model)
		return ROLE_NONE;
	RoleMask roles = rolesFromTags(m->model);
	if (!(roles & ROLE_STRUCTURAL))
		roles |= rolesFromText(m->model->name + " " + m->model->description);
	return roles;
}


/** How freely a knob on a module of this kind may be moved, 0..1.

    The minimum over every role the module holds, rather than a single "dominant"
    role: a sequencer is timing AND pitch, and the honest answer for a knob on it
    is the more careful of the two. A module we have no read on gets 1.0 --
    absence of knowledge is not a reason to be timid, only a reason not to claim
    otherwise. */
inline float roleScale(RoleMask roles) {
	struct Entry { RoleMask bit; float scale; };
	static const Entry TABLE[] = {
		{ROLE_INERT,      0.00f},
		{ROLE_IO,         0.00f},
		{ROLE_PITCH,      0.30f},   // retuning the note stream leaves the key
		{ROLE_TIMING,     0.40f},   // and retiming it leaves the pulse
		{ROLE_MIX,        0.50f},
		{ROLE_ENVELOPE,   0.70f},
		{ROLE_SOURCE,     0.80f},
		{ROLE_UTILITY,    0.80f},
		{ROLE_MODULATION, 1.00f},
		{ROLE_PROCESSOR,  1.00f},   // where the interesting knobs live
		{ROLE_SURFACE,    1.00f},   // ... and where the intended ones do
	};
	float scale = 1.f;
	for (const Entry& e : TABLE) {
		if (roles & e.bit)
			scale = std::min(scale, e.scale);
	}
	return scale;
}


/** Human-readable, for the roles report and tooltips. */
inline std::string roleNames(RoleMask roles) {
	struct Entry { RoleMask bit; const char* name; };
	static const Entry TABLE[] = {
		{ROLE_SOURCE, "source"},        {ROLE_PROCESSOR, "processor"},
		{ROLE_MIX, "mix"},              {ROLE_TIMING, "timing"},
		{ROLE_CLOCKROOT, "clock-root"}, {ROLE_ENVELOPE, "envelope"},
		{ROLE_MODULATION, "modulation"},{ROLE_PITCH, "pitch"},
		{ROLE_UTILITY, "utility"},      {ROLE_INERT, "inert"},
		{ROLE_IO, "io"},               {ROLE_SURFACE, "surface"},
	};
	std::string out;
	for (const Entry& e : TABLE) {
		if (!(roles & e.bit))
			continue;
		if (!out.empty())
			out += "|";
		out += e.name;
	}
	return out.empty() ? "-" : out;
}


/** Roles resolved once per module per roll.

    rollCables rebuilds its destination-slot list once per cable operation, so
    without this the tag resolution would repeat up to eight times per filing --
    the same reason makeSlot() hoists its string parsing out of the inner loop. */
struct RoleCache {
	std::unordered_map<int64_t, RoleMask> byId;

	RoleMask get(rack::engine::Module* m) {
		if (!m)
			return ROLE_NONE;
		auto it = byId.find(m->id);
		if (it != byId.end())
			return it->second;
		const RoleMask roles = rolesOf(m);
		byId[m->id] = roles;
		return roles;
	}

	void clear() {
		byId.clear();
	}
};


} // namespace upol
