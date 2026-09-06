#pragma once
/** The message Repossession and its SCHEDULE A expander pass across the seam.

One struct, both directions, because the two modules are one instrument and
splitting it into two would only mean keeping two things in step:

    expander -> host    a per-step CV value, and whether that jack is patched
    host -> expander    that step's audio, and the colour its light should wear

Rack gives each side two buffers and flips them once a frame, so neither module
ever reads a buffer the other is writing. Nothing here allocates, and nothing
here is a pointer into the other module -- an expander can be deleted between
one frame and the next, and the only correct assumption is that it might have
been.

`has[]` is the point of the design. The expander's discrete jack takes priority
over the host's polyphonic one *only where a cable is actually in it*, so the
two can be used together: a poly LFO into the host's SPEED for all eight steps,
and one hand-patched envelope into step 5 on the expander, without the empty
jacks of either silently overriding the other with zero. */
#include "Media.hpp"        // for rp::NUM_SLOTS

namespace rp {


/** Which per-step control a value belongs to. Indices into SchedMessage's
    arrays, and the order the expander's columns are in. */
enum StepCv {
	CV_SPEED = 0,
	CV_GAIN,
	CV_START,
	CV_LEN,
	NUM_STEP_CV
};


struct SchedMessage {
	// --- expander -> host ---------------------------------------------------
	float cv[NUM_STEP_CV][NUM_SLOTS];
	bool has[NUM_STEP_CV][NUM_SLOTS];

	// --- host -> expander ---------------------------------------------------
	/** That step's audio, in volts, silent unless it is the one sounding. */
	float stepOut[NUM_SLOTS];
	/** The colour the step is wearing on the host, so the expander's row lights
	    match the host's buttons and timeline spans without knowing the palette
	    or which steps are disabled. */
	float ink[NUM_SLOTS][3];
	/** False until a host has written a frame, so a lone expander shows dark
	    lights and silent outputs rather than stale ones. */
	bool hostPresent;

	SchedMessage() { clear(); }

	void clear() {
		for (int c = 0; c < NUM_STEP_CV; c++) {
			for (int i = 0; i < NUM_SLOTS; i++) {
				cv[c][i] = 0.f;
				has[c][i] = false;
			}
		}
		for (int i = 0; i < NUM_SLOTS; i++) {
			stepOut[i] = 0.f;
			ink[i][0] = ink[i][1] = ink[i][2] = 0.f;
		}
		hostPresent = false;
	}
};


} // namespace rp
