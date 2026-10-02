#pragma once
// Ledger's generator engine: a transliteration of Shoal by Ormer Modular.
//
//   Shoal - an 8-track generative melody sequencer plug-in for the Expert
//   Sleepers disting NT. Copyright (c) 2026 Ormer Modular. MIT License.
//   https://github.com/ormermodular/shoal  (vendored: vendor/shoal/shoal.cpp)
//
// Everything below the parameter tables is shoal.cpp's sequencing code with the
// disting NT host calls swapped for a Host interface and the bus frames for an Io
// block. It is kept a transliteration on purpose: the same parameters and seeds
// must produce the same gates, pitches, Currents, EOS pulses and MIDI bytes as
// the original, and tests/Ledger/test_golden.cpp builds the original next to this
// and compares them frame by frame. Do not refactor the sequencing code; anything
// Ledger adds goes outside it, or behind a hook that leaves the outputs unchanged.
//
// What differs from the NT plug-in, and why:
//  - Outputs are Io pointers (null = not patched), always Replace: Rack has no
//    shared busses to sum onto. The routing params keep their indices but are unused.
//  - Clock out and EOS pulses are `pulseVolts` high (Shoal-for-VCV uses 10 V; the
//    NT 5 V). The golden test sets 5.
//  - NT_globals.sampleRate is Shoal::sampleRate; NT_setParameterFromAudio and
//    NT_sendMidi3ByteMessage are Host calls. The UI (pots, encoders, draw) is
//    Ledger's own and is not here.
// Pure C++11, no Rack types, no allocation.

#include <stdint.h>

namespace ledger {
namespace shoal {

enum
{
	kNumTracks = 8,
	kMaxSteps = 64,
};

// ---------------------------------------------------------------------------
// Parameters (indices identical to shoal.cpp, so a v[] means the same thing)
// ---------------------------------------------------------------------------

enum
{
	kGClockSource,
	kGBPM,
	kGRun,
	kGFreeze,
	kGScale,
	kGRoot,
	kGWeight,
	kGSaver,
	kGClockIn,
	kGResetIn,
	kGReseedAll,
	kNumGlobalParams,
};

enum
{
	kTLength,
	kTRate,
	kTDirection,
	kTChance,
	kTBreathe,
	kTNote,
	kTOct,
	kTEvolve,
	kTGate,
	kTTie,
	kTSlop,
	kTOctave,
	kTTrans,
	kTSource,
	kTMute,
	kTSeed,
	kNumTrackParams,
};

enum
{
	kTrackBase = kNumGlobalParams,
	kRoutingBase = kTrackBase + kNumTracks * kNumTrackParams,
	kNumRoutingParams = 4,
	kGClockOut = kRoutingBase + kNumTracks * kNumRoutingParams,
	kGClockOutMode,
	kGReseedIn,
	kGFreezeClock,
	kExtBase,
	kNumExtParams = 4,
	kGMidiVelocity = kExtBase + kNumTracks * kNumExtParams,
	kGMidiDest,
	kMidiBase,
	kNumMidiParams = 1,
	kCurrentBase = kMidiBase + kNumTracks * kNumMidiParams,
	kNumCurrentParams = 1,
	kEosBase = kCurrentBase + kNumTracks * kNumCurrentParams,
	kNumEosParams = 1,
	kNumParameters = kEosBase + kNumTracks * kNumEosParams,
};

enum
{
	kXShift,
	kXGateVolts,
	kXPitchScale,
	kXPitchOffset,
};

static inline int TP( int t, int off ) { return kTrackBase + t * kNumTrackParams + off; }
static inline int RP( int t, int off ) { return kRoutingBase + t * kNumRoutingParams + off; }
static inline int XP( int t, int off ) { return kExtBase + t * kNumExtParams + off; }
static inline int MP( int t ) { return kMidiBase + t; }
static inline int CP( int t ) { return kCurrentBase + t; }
static inline int EP( int t ) { return kEosBase + t; }

// Rates in true ascending order; num/den = rateMult/rateDiv (shoal.cpp's table).
static const char* const rateNames[] = {
	"/64", "/32", "/16", "/8", "/7", "/6", "/5.3", "/5", "/4", "/3",
	"/2.6", "/2", "/1.5",
	"x1", "x1.25", "x1.3", "x1.5", "x2", "x2.6", "x3", "x4", "x5",
	"x5.3", "x6", "x7", "x8", "x16", "x32", "x64",
};
static const uint8_t rateDiv[]  = { 64, 32, 16, 8, 7, 6, 53, 5, 4, 3, 13, 2, 3, 1, 4, 10, 2, 1, 5, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1 };
static const uint8_t rateMult[] = { 1, 1, 1, 1, 1, 1, 10, 1, 1, 1, 5, 1, 2, 1, 5, 13, 3, 2, 13, 3, 4, 5, 53, 6, 7, 8, 16, 32, 64 };
enum { kNumRates = 29, kRateX1 = 13 };

enum
{
	kDirForwards,
	kDirReverse,
	kDirPendulum,		// endpoints play twice on the turn
	kDirRandom,
	kDirDrunk,			// 50% advance, 25% repeat, 25% back up
	kDirPong,			// endpoints play once on the turn
	kDirTide,			// forwards, rotating the pitch stream one step per pass
	kDirShuffle,		// every step once per pass, new seeded order each pass
	kDirPools,			// dwell in a small pocket of steps, then hop to another
	kDirStride,			// hopscotch: 1,3,2,4,3,5...
	kDirGravity,		// seed-derived chance each step to snap back to step 1
	kDirConverge,		// folds inward from both ends toward the middle
	kDirDiverge,		// unfolds outward from the middle toward both ends
	kDirSkitter,		// like Random, but never repeats the same step twice in a row
	kDirAnchor,			// alternates step 1 with each other step in turn
	kNumDirections,
};

static const char* const directionNames[] = {
	"Forwards", "Reverse", "Pendulum", "Random", "Drunk", "Pong",
	"Tide", "Shuffle", "Pools", "Stride", "Gravity", "Converge", "Diverge",
	"Skitter", "Anchor",
};
static const char* const directionShort[] = {
	"FWD", "REV", "PND", "RND", "DRK", "PNG", "TID", "SHF", "POL",
	"STR", "GRV", "CNV", "DIV", "SKT", "ANC",
};

static const char* const scaleNames[] = {
	"Chromatic", "Major", "Natural minor", "Harmonic minor", "Dorian", "Phrygian",
	"Lydian", "Mixolydian", "Major pentatonic", "Minor pentatonic", "Blues",
	"Hirajoshi", "In-Sen",
};
static const char* const scaleShort[] = {
	"CHROM", "MAJ", "NMIN", "HMIN", "DOR", "PHRYG", "LYD",
	"MIXO", "PENT+", "PENT-", "BLUES", "HIRA", "INSEN",
};

// Bitmasks of allowed pitch classes, relative to the root note.
static const uint16_t scaleMasks[] = {
	0xFFF,
	(1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),		// Major
	(1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),		// Natural minor
	(1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),		// Harmonic minor
	(1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),		// Dorian
	(1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),		// Phrygian
	(1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<7)|(1<<9)|(1<<11),		// Lydian
	(1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),		// Mixolydian
	(1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),						// Major pentatonic
	(1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),					// Minor pentatonic
	(1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10),				// Blues
	(1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<8),						// Hirajoshi
	(1<<0)|(1<<1)|(1<<5)|(1<<7)|(1<<10),					// In-Sen
};
enum { kNumScales = 13 };

// Range and default of every parameter, from shoal.cpp's PARAM_TABLE. Ledger
// widens two ranges the way Shoal-for-VCV does (Gate length to 100%, Gate volts
// default 10 V); `nt` selects the disting NT's own values for the golden test.
struct ParamRange { int16_t min, max, def; };

static inline ParamRange paramRange( int p, bool nt = false )
{
	static const int16_t seedDefs[kNumTracks] = { 1, 98, 195, 292, 389, 486, 583, 680 };
	ParamRange r = { 0, 0, 0 };
	if ( p < kNumGlobalParams )
	{
		static const ParamRange g[kNumGlobalParams] = {
			{ 0, 2, 1 },		// Clock source: External / Internal / MIDI
			{ 20, 300, 120 },	// BPM
			{ 0, 1, 1 },		// Run
			{ 0, 1, 0 },		// Freeze
			{ 0, kNumScales - 1, 0 },
			{ 0, 127, 48 },		// Root note
			{ 0, 100, 0 },		// Weight
			{ 0, 2, 1 },		// Screensaver
			{ 0, 28, 1 },		// Clock input bus
			{ 0, 28, 2 },		// Reset input bus
			{ 0, 999, 0 },		// Reseed all
		};
		return g[p];
	}
	if ( p < kRoutingBase )
	{
		int t = ( p - kTrackBase ) / kNumTrackParams;
		static const ParamRange tr[kNumTrackParams] = {
			{ 1, kMaxSteps, 16 },
			{ 0, kNumRates - 1, kRateX1 },
			{ 0, kNumDirections - 1, 0 },
			{ 0, 100, 100 },
			{ 0, 100, 0 },
			{ -100, 100, 0 },
			{ -100, 100, 0 },
			{ 0, 100, 0 },
			{ 5, 100, 50 },		// Gate length (NT: 5..95)
			{ 0, 100, 0 },
			{ 0, 100, 0 },
			{ -3, 3, 0 },
			{ -7, 7, 0 },
			{ 0, 8, 0 },		// Sample source (Follow)
			{ 0, 1, 0 },
			{ 0, 999, 0 },		// Seed (default per track)
		};
		int off = ( p - kTrackBase ) % kNumTrackParams;
		r = tr[off];
		if ( off == kTGate && nt ) r.max = 95;
		if ( off == kTSeed ) r.def = seedDefs[t];
		return r;
	}
	if ( p < kGClockOut )
	{
		int off = ( p - kRoutingBase ) % kNumRoutingParams;
		r.min = 0; r.max = ( off & 1 ) ? 1 : 28; r.def = ( off & 1 ) ? 1 : 0;
		return r;
	}
	if ( p == kGClockOut ) { r.min = 0; r.max = 28; r.def = 0; return r; }
	if ( p == kGClockOutMode ) { r.min = 0; r.max = 1; r.def = 1; return r; }
	if ( p == kGReseedIn ) { r.min = 0; r.max = 28; r.def = 0; return r; }
	if ( p == kGFreezeClock ) { r.min = 0; r.max = 1; r.def = 1; return r; }
	if ( p < kGMidiVelocity )
	{
		int off = ( p - kExtBase ) % kNumExtParams;
		static const ParamRange x[kNumExtParams] = {
			{ -( kMaxSteps - 1 ), kMaxSteps - 1, 0 },	// Shift
			{ 1, 10, 10 },		// Gate volts (NT default 5)
			{ 5, 200, 100 },	// Pitch scale %
			{ -100, 100, 0 },	// Pitch offset, 0.1 V steps
		};
		r = x[off];
		if ( off == kXGateVolts && nt ) r.def = 5;
		return r;
	}
	if ( p == kGMidiVelocity ) { r.min = 1; r.max = 127; r.def = 100; return r; }
	if ( p == kGMidiDest ) { r.min = 0; r.max = 15; r.def = 1; return r; }
	if ( p < kCurrentBase ) { r.min = 0; r.max = 16; r.def = 0; return r; }	// MIDI channel
	r.min = 0; r.max = 28; r.def = 0;											// Current / EOS out bus
	return r;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct TrackState
{
	uint32_t	activeSeed;
	int32_t		pendingSeed;		// -1 = none armed
	int32_t		pos;				// -1 = not started
	int8_t		dir;				// +1 / -1, for Pendulum and Pong
	uint8_t		phase;				// walk counter within a pass (Tide, Shuffle)
	uint8_t		rot;				// Tide's rotation offset
	uint16_t	pass;				// completed-pass counter (Shuffle)
	uint8_t		poolStart, poolLen, poolPos, dwell;		// Pools state
	uint8_t		order[kMaxSteps];	// Shuffle's permutation for the current pass
	uint8_t		epoch[kMaxSteps];	// Evolve: per-step re-roll counters
	bool		resting;			// Breathe: this whole pass is silent
	uint32_t	extraPending;		// remaining multiplied sub-steps this master tick
	uint32_t	subPeriod;
	uint32_t	subCountdown;
	uint32_t	stepPeriod;			// samples per step, for gate length
	uint32_t	gateRemaining;
	uint32_t	slopCountdown;		// samples until a slop-delayed step fires, 0 = none
	uint32_t	slopPeriod;			// step period for the delayed step
	bool		tieHeld;
	float		pitchVolts;
	bool		midiHeld;
	uint8_t		midiHeldNote;
	uint8_t		midiHeldChan;		// 1-16 (never 0 while midiHeld)
	uint32_t	midiHeldDest;
	bool		midiSounding;		// previous sample's gate-high state, for edge detection
	float		currentPrev;
	float		currentTarget;
	uint32_t	currentPeriod;
	uint32_t	currentElapsed;
	float		currentVolts;
	uint32_t	eosCount;
	uint32_t	eosRemaining;
	// Ledger: advances since construct, so the host can see that one happened.
	uint32_t	advances;
};

struct Dtc
{
	TrackState	tracks[kNumTracks];
	bool		clockHigh;
	bool		resetHigh;
	bool		reseedInHigh;		// edge state for the Reseed trigger input
	bool		resetPrime;			// next master tick restarts all tracks at step 1
	bool		seedsInited;
	int32_t		lastReseedAll;		// -1 until first seen; detects real changes
	int32_t		reseedAllPending;	// -1 none; else the base value to scatter
	uint32_t	tickCount;			// global master tick counter - all dividers phase-lock to it
	uint32_t	uiClock;			// sample counter
	uint32_t	rng;				// audio-context RNG for Random/Drunk directions
	uint32_t	samplesSinceClock;
	uint32_t	extPeriod;
	uint32_t	intPeriod;
	uint32_t	intCountdown;
	uint32_t	clockPulse;			// samples left of the current Clock out high phase
	bool		midiRunning;		// Start/Continue seen, no Stop since
	uint32_t	midiClockCount;		// raw 0xF8 pulses since the last Shoal tick (24 PPQN)
	bool		midiTickPending;	// a full 24 pulses have arrived, not yet consumed
	uint32_t	midiSamplesSinceTick;
	uint32_t	midiPeriod;			// last measured inter-tick period, samples
	uint8_t		scalePcs[12];
	uint8_t		scaleCount;
	uint8_t		stableIdx[3];		// Weight: scale indices of root / third / fifth
	uint8_t		stableCount;
	int16_t		scaleCached;		// scale index the table was built for, -1 = dirty
	uint32_t	sampleRate;			// NT_globals.sampleRate
};

struct Shoal;

// What the disting NT host did for the plug-in.
struct Host
{
	virtual ~Host() {}
	// NT_setParameterFromAudio: write the parameter and call parameterChanged().
	virtual void setParameterFromAudio( Shoal* s, int p, int16_t value ) = 0;
	// NT_sendMidi3ByteMessage
	virtual void sendMidi3( uint32_t dest, uint8_t b0, uint8_t b1, uint8_t b2 ) = 0;
	// Ledger: an external track (Shoal::external[t]) reached step `pos` of its walk.
	// Shoal has done everything up to choosing the note -- the walk, the loop origin,
	// Evolve, Breathe, Currents, EOS -- and the host plays the step instead. `silent`
	// is Shoal's own verdict: muted, soloed out or breathing.
	virtual void externalStep( Shoal*, int t, int pos, uint32_t stepPeriod, bool silent ) { (void)t; (void)pos; (void)stepPeriod; (void)silent; }
};

struct Shoal
{
	Dtc			dtcStore;
	Dtc*		dtc;
	int16_t		v[kNumParameters];
	bool		solo[kNumTracks];
	Host*		host;
	float		pulseVolts;			// Clock out and EOS high level
	// Ledger: tracks whose steps are played by the host (Host::externalStep) rather
	// than by evalStep. All false -- as the golden test runs it -- is Shoal exactly.
	bool		external[kNumTracks];

	Shoal() : dtc( &dtcStore ), host( 0 ), pulseVolts( 10.0f )
	{
		for ( int t = 0; t < kNumTracks; ++t ) external[t] = false;
	}
};

// Bus frames, one pointer per signal, numFrames long; null = not patched.
struct Io
{
	const float*	clockIn;
	const float*	resetIn;
	const float*	reseedIn;
	float*			clkOut;
	float*			pitchOut[kNumTracks];
	float*			gateOut[kNumTracks];
	float*			currentOut[kNumTracks];
	float*			eosOut[kNumTracks];
};

// ---------------------------------------------------------------------------
// Pattern generation - a pure function of (seed, step, parameters)
// ---------------------------------------------------------------------------

static inline uint32_t hash3( uint32_t a, uint32_t b, uint32_t c )
{
	uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
	h ^= h >> 16; h *= 0x7FEB352Du;
	h ^= h >> 15; h *= 0x846CA68Bu;
	h ^= h >> 16;
	return h;
}

static inline void rebuildScale( Dtc* dtc, int scaleIdx )
{
	uint16_t mask = scaleMasks[scaleIdx];
	uint8_t n = 0;
	for ( int i = 0; i < 12; ++i )
		if ( mask & ( 1 << i ) )
			dtc->scalePcs[n++] = i;
	dtc->scaleCount = n;

	// stable degrees for Weight: the root, plus whichever scale degrees
	// sit nearest a third (3-4 semitones) and a fifth (7)
	dtc->stableCount = 0;
	dtc->stableIdx[dtc->stableCount++] = 0;
	int bestT = -1, bt = 99, bestF = -1, bf = 99;
	for ( int i = 1; i < n; ++i )
	{
		int pc = dtc->scalePcs[i];
		int d3 = pc - 3; if ( d3 < 0 ) d3 = -d3;
		int d4 = pc - 4; if ( d4 < 0 ) d4 = -d4;
		if ( d4 < d3 ) d3 = d4;
		if ( d3 < bt ) { bt = d3; bestT = i; }
		int d7 = pc - 7; if ( d7 < 0 ) d7 = -d7;
		if ( d7 < bf ) { bf = d7; bestF = i; }
	}
	if ( bestT > 0 )
		dtc->stableIdx[dtc->stableCount++] = (uint8_t)bestT;
	if ( bestF > 0 && bestF != bestT )
		dtc->stableIdx[dtc->stableCount++] = (uint8_t)bestF;

	dtc->scaleCached = scaleIdx;
}

// Follow the sample-source chain to the terminal track (linked follow).
// Followers share the source's seed AND its evolution epochs.
static inline int effectiveTrack( const Shoal* pThis, int t )
{
	const int16_t* v = pThis->v;
	int cur = t;
	for ( int depth = 0; depth < kNumTracks; ++depth )
	{
		int src = v[ TP( cur, kTSource ) ];
		if ( src == 0 || src - 1 == cur )
			break;
		cur = src - 1;
	}
	return cur;
}

static inline uint32_t effectiveSeed( const Shoal* pThis, int t )
{
	return pThis->dtc->tracks[ effectiveTrack( pThis, t ) ].activeSeed;
}

struct StepEval
{
	bool	fires;
	bool	tie;
	int16_t	note;		// MIDI note number
	int8_t	barH;		// 0 (rest) or 1..5, for the display
};

static inline void evalStep( const Shoal* pThis, int t, int step, StepEval& out )
{
	const int16_t* v = pThis->v;
	const Dtc* dtc = pThis->dtc;

	int eff = effectiveTrack( pThis, t );
	uint32_t seed = dtc->tracks[eff].activeSeed;
	const uint8_t* ep = dtc->tracks[eff].epoch;		// Evolve counters ride the follow chain

	// Shift: rotate the whole pattern lookup, non-destructively
	int shift = v[ XP( t, kXShift ) ];
	if ( shift )
	{
		int length = v[ TP( t, kTLength ) ];
		step = ( step + shift ) % length;
		if ( step < 0 )
			step += length;
	}

	// Tide: the rhythm stays anchored while the pitch stream drifts one
	// step through it per pass (rot). Other modes: pitchStep == step.
	int pitchStep = step;
	if ( v[ TP( t, kTDirection ) ] == kDirTide )
	{
		int length = v[ TP( t, kTLength ) ];
		if ( length > 1 )
			pitchStep = ( step + dtc->tracks[t].rot ) % length;
	}

	// evolved steps hash differently: the epoch mixes into the roll input
	uint32_t esR = (uint32_t)step | ( (uint32_t)ep[step] << 8 );
	uint32_t esP = (uint32_t)pitchStep | ( (uint32_t)ep[pitchStep] << 8 );

	out.fires = ( hash3( seed, esR, 0xF17Eu ) % 100 ) < (uint32_t)v[ TP( t, kTChance ) ];
	out.tie = ( hash3( seed, esR, 0x071Eu ) % 100 ) < (uint32_t)v[ TP( t, kTTie ) ];
	if ( !out.fires )
	{
		out.note = 0;
		out.barH = 0;
		return;
	}

	int k = dtc->scaleCount ? dtc->scaleCount : 12;

	// The base melody: two octaves of the scale centred on the root
	uint32_t rb = hash3( seed, esP, 0xBA5Eu ) & 1023;
	int degree = (int)( ( rb * (uint32_t)( 2 * k ) ) >> 10 ) - k;

	// Note / Oct amounts add random deviation; magnitude = how often and how
	// far, sign = direction
	int noteAmt = v[ TP( t, kTNote ) ];
	int octAmt = v[ TP( t, kTOct ) ];
	int nMag = noteAmt < 0 ? -noteAmt : noteAmt;
	int oMag = octAmt < 0 ? -octAmt : octAmt;

	int o = 0;
	if ( nMag > 0 && ( hash3( seed, esP, 0xA221u ) % 100 ) < (uint32_t)nMag )
	{
		int reach = 1 + ( nMag * 11 ) / 100;						// 1..12 scale steps
		int d = 1 + (int)( hash3( seed, esP, 0x0007u ) % (uint32_t)reach );
		degree += ( noteAmt > 0 ) ? d : -d;
	}
	if ( oMag > 0 && ( hash3( seed, esP, 0x0C7Au ) % 100 ) < (uint32_t)oMag )
	{
		int reachO = 1 + ( oMag * 4 ) / 100;						// 1..5 octaves
		o = 1 + (int)( hash3( seed, esP, 0x0C7Bu ) % (uint32_t)reachO );
		if ( octAmt < 0 )
			o = -o;
	}

	// Weight: consonance gravity - snap to the nearest stable degree
	int w = v[kGWeight];
	if ( w > 0 && ( hash3( seed, esP, 0x0337u ) % 100 ) < (uint32_t)w )
	{
		int q2 = degree / k, r2 = degree - q2 * k;
		if ( r2 < 0 ) { r2 += k; q2 -= 1; }
		int best = 0, bd = 99;
		for ( int i2 = 0; i2 < dtc->stableCount; ++i2 )
		{
			int cand = dtc->stableIdx[i2];
			for ( int oc = 0; oc < 2; ++oc, cand += k )		// this octave's, and the root above
			{
				int d2 = r2 - cand; if ( d2 < 0 ) d2 = -d2;
				if ( d2 < bd ) { bd = d2; best = cand; }
			}
		}
		degree = q2 * k + best;
	}

	degree += v[ TP( t, kTTrans ) ];

	// scale-degree walk from the root
	int q = degree / k, r = degree - q * k;
	if ( r < 0 ) { r += k; q -= 1; }
	int note = v[kGRoot] + 12 * ( v[ TP( t, kTOctave ) ] + q + o ) + dtc->scalePcs[r];
	// fold out-of-range notes back by octaves, preserving the pitch class
	while ( note < 0 ) note += 12;
	while ( note > 127 ) note -= 12;
	out.note = note;

	int maxReach = nMag > 0 ? 1 + ( nMag * 11 ) / 100 : 0;
	int lo = -k - ( noteAmt < 0 ? maxReach : 0 );
	int hi = k - 1 + ( noteAmt > 0 ? maxReach : 0 );
	out.barH = (int8_t)( 1 + ( ( degree - lo ) * 4 ) / ( hi > lo ? hi - lo : 1 ) );
	if ( out.barH < 1 ) out.barH = 1;
	if ( out.barH > 5 ) out.barH = 5;
}

// ---------------------------------------------------------------------------
// Construction and parameter changes
// ---------------------------------------------------------------------------

// shoal.cpp's construct(), minus the UI state. v[] is the caller's.
static inline void construct( Shoal* pThis, uint32_t sampleRate )
{
	Dtc* dtc = pThis->dtc;
	for ( int t = 0; t < kNumTracks; ++t )
	{
		TrackState& tr = dtc->tracks[t];
		tr.activeSeed = 0;
		tr.pendingSeed = -1;
		tr.pos = -1;
		tr.dir = 1;
		tr.phase = 0;
		tr.rot = 0;
		tr.pass = 0;
		tr.poolStart = 0; tr.poolLen = 0; tr.poolPos = 0; tr.dwell = 0;
		for ( int s = 0; s < kMaxSteps; ++s )
		{
			tr.epoch[s] = 0;
			tr.order[s] = 0;
		}
		tr.resting = false;
		tr.extraPending = 0;
		tr.subPeriod = 0;
		tr.subCountdown = 0;
		tr.stepPeriod = 0;
		tr.gateRemaining = 0;
		tr.slopCountdown = 0;
		tr.slopPeriod = 0;
		tr.tieHeld = false;
		tr.pitchVolts = 0.0f;
		tr.midiHeld = false;
		tr.midiHeldNote = 0;
		tr.midiHeldChan = 0;
		tr.midiHeldDest = 0;
		tr.midiSounding = false;
		tr.currentPrev = 0.0f;
		tr.currentTarget = 0.0f;
		tr.currentPeriod = 0;
		tr.currentElapsed = 0;
		tr.currentVolts = 0.0f;
		tr.eosCount = 0;
		tr.eosRemaining = 0;
		tr.advances = 0;
		pThis->solo[t] = false;
	}
	dtc->clockHigh = false;
	dtc->resetHigh = false;
	dtc->reseedInHigh = false;
	dtc->resetPrime = false;
	dtc->seedsInited = false;
	dtc->lastReseedAll = -1;
	dtc->reseedAllPending = -1;
	dtc->tickCount = 0;
	dtc->uiClock = 0;
	dtc->rng = 0x7A31C0DEu;
	dtc->samplesSinceClock = 0;
	dtc->extPeriod = 0;
	dtc->sampleRate = sampleRate;
	dtc->intPeriod = sampleRate / 8;
	dtc->intCountdown = 1;
	dtc->clockPulse = 0;
	dtc->midiRunning = false;
	dtc->midiClockCount = 0;
	dtc->midiTickPending = false;
	dtc->midiSamplesSinceTick = 0;
	dtc->midiPeriod = 0;
	dtc->scaleCached = -1;
	dtc->scaleCount = 0;
	dtc->stableCount = 0;
}

static inline void sendNoteOff( Shoal* pThis, TrackState& tr )
{
	if ( pThis->host )
		pThis->host->sendMidi3( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
}

static inline void parameterChanged( Shoal* pThis, int p )
{
	Dtc* dtc = pThis->dtc;

	if ( p == kGScale )
	{
		dtc->scaleCached = -1;
		return;
	}
	if ( p == kGBPM )
	{
		int bpm = pThis->v[kGBPM];
		if ( bpm > 0 )
			dtc->intPeriod = ( dtc->sampleRate * 60 ) / (uint32_t)bpm;	// quarter notes: x1 = one step per beat
		return;
	}
	if ( p == kGReseedAll )
	{
		// Just note the request; step() performs the scatter. The initial
		// value (add / preset load) is ignored so saved seeds survive.
		int32_t value = pThis->v[kGReseedAll];
		if ( dtc->lastReseedAll >= 0 && value != dtc->lastReseedAll )
			dtc->reseedAllPending = value;
		dtc->lastReseedAll = value;
		return;
	}
	if ( p >= kTrackBase && p < kRoutingBase )
	{
		int t = ( p - kTrackBase ) / kNumTrackParams;
		int off = ( p - kTrackBase ) % kNumTrackParams;
		if ( off == kTSeed )
		{
			// arm: the new seed lands when the track wraps to step 1
			dtc->tracks[t].pendingSeed = pThis->v[p];
		}
		return;
	}
	if ( p == kGMidiDest )
	{
		for ( int t = 0; t < kNumTracks; ++t )
		{
			TrackState& tr = dtc->tracks[t];
			if ( tr.midiHeld )
			{
				sendNoteOff( pThis, tr );
				tr.midiHeld = false;
				tr.midiSounding = false;
			}
		}
		return;
	}
	if ( p >= kMidiBase && p < kMidiBase + kNumTracks * kNumMidiParams )
	{
		int t = p - kMidiBase;
		TrackState& tr = dtc->tracks[t];
		if ( tr.midiHeld )
		{
			sendNoteOff( pThis, tr );
			tr.midiHeld = false;
			tr.midiSounding = false;
		}
		return;
	}
}

// The sample rate changed: re-derive what parameterChanged(kGBPM) computed.
static inline void setSampleRate( Shoal* pThis, uint32_t sampleRate )
{
	pThis->dtc->sampleRate = sampleRate;
	parameterChanged( pThis, kGBPM );
}

// ---------------------------------------------------------------------------
// Sequencing
// ---------------------------------------------------------------------------

// Seeded Fisher-Yates permutation for Shuffle: every step exactly once per
// pass, in an order that changes each pass but is fully reproducible.
static inline void buildShuffle( TrackState& tr, int length, uint32_t seed, uint32_t pass )
{
	for ( int i = 0; i < length; ++i )
		tr.order[i] = (uint8_t)i;
	uint32_t r = hash3( seed, pass, 0x5FFEu );
	for ( int i = length - 1; i > 0; --i )
	{
		r = r * 1664525u + 1013904223u;
		int j = (int)( ( r >> 16 ) % (uint32_t)( i + 1 ) );
		uint8_t tmp = tr.order[i]; tr.order[i] = tr.order[j]; tr.order[j] = tmp;
	}
}

static inline void advanceTrack( Shoal* pThis, int t, uint32_t stepPeriod, bool anySolo )
{
	const int16_t* v = pThis->v;
	Dtc* dtc = pThis->dtc;
	TrackState& tr = dtc->tracks[t];

	int length = v[ TP( t, kTLength ) ];
	int mode = v[ TP( t, kTDirection ) ];

	if ( tr.pos >= length )
		tr.pos = length - 1;			// length was shortened mid-flight

	if ( tr.pos < 0 )
	{
		// first step after start/reset: the mode's origin
		tr.dir = 1;
		tr.phase = 0;
		tr.rot = 0;
		tr.pass = 0;
		tr.dwell = 0;
		tr.poolPos = 0;
		if ( mode == kDirShuffle )
		{
			buildShuffle( tr, length, effectiveSeed( pThis, t ), 0 );
			tr.pos = tr.order[0] % length;
		}
		else if ( mode == kDirDiverge )
			tr.pos = ( length - 1 ) / 2;
		else
			tr.pos = ( mode == kDirReverse ) ? length - 1 : 0;
	}
	else switch ( mode )
	{
	default:
	case kDirForwards:
		tr.pos = ( tr.pos + 1 ) % length;
		break;
	case kDirReverse:
		tr.pos = ( tr.pos + length - 1 ) % length;
		break;
	case kDirPendulum:					// endpoints play twice on the turn
		if ( length > 1 )
		{
			if ( tr.dir > 0 )
			{
				if ( tr.pos >= length - 1 ) tr.dir = -1;	// repeat the end
				else tr.pos += 1;
			}
			else
			{
				if ( tr.pos <= 0 ) tr.dir = 1;				// repeat the start
				else tr.pos -= 1;
			}
		}
		break;
	case kDirPong:						// endpoints play once on the turn
		if ( length > 1 )
		{
			if ( tr.dir > 0 )
			{
				if ( tr.pos >= length - 1 ) { tr.dir = -1; tr.pos -= 1; }
				else tr.pos += 1;
			}
			else
			{
				if ( tr.pos <= 0 ) { tr.dir = 1; tr.pos += 1; }
				else tr.pos -= 1;
			}
		}
		break;
	case kDirRandom:
		dtc->rng = dtc->rng * 1664525u + 1013904223u;
		tr.pos = (int)( ( dtc->rng >> 16 ) % (uint32_t)length );
		break;
	case kDirDrunk:
	{
		dtc->rng = dtc->rng * 1664525u + 1013904223u;
		uint32_t r = ( dtc->rng >> 16 ) % 100;
		if ( r < 50 )
			tr.pos = ( tr.pos + 1 ) % length;
		else if ( r < 75 )
			;								// repeat the step
		else
			tr.pos = ( tr.pos + length - 1 ) % length;
		break;
	}
	case kDirTide:						// pitch stream drifts one step per pass
		tr.pos = ( tr.pos + 1 ) % length;
		if ( tr.pos == 0 && length > 1 )
			tr.rot = (uint8_t)( ( tr.rot + 1 ) % length );
		break;
	case kDirShuffle:					// every step once per pass, reshuffled
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % length );
		tr.pos = tr.order[ tr.phase ] % length;		// re-dealt below at the pass boundary
		break;
	case kDirPools:						// dwell in a pocket, then hop
	{
		if ( tr.dwell == 0 )
		{
			dtc->rng = dtc->rng * 1664525u + 1013904223u;
			tr.poolLen = (uint8_t)( 3 + ( ( dtc->rng >> 16 ) % 2 ) );		// 3-4 steps
			dtc->rng = dtc->rng * 1664525u + 1013904223u;
			tr.poolStart = (uint8_t)( ( dtc->rng >> 16 ) % (uint32_t)length );
			dtc->rng = dtc->rng * 1664525u + 1013904223u;
			tr.dwell = (uint8_t)( tr.poolLen * ( 2 + ( ( dtc->rng >> 16 ) % 3 ) ) );	// 2-4 laps
			tr.poolPos = 0;
		}
		tr.pos = ( tr.poolStart + tr.poolPos ) % length;
		tr.poolPos = (uint8_t)( ( tr.poolPos + 1 ) % tr.poolLen );
		tr.dwell -= 1;
		break;
	}
	case kDirStride:
	{
		// Hopscotch: pairs (p, p+2) for p = 0..length-1, period 2*Length
		uint32_t period = (uint32_t)length * 2;
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % period );
		uint32_t n = tr.phase;
		uint32_t p = n / 2;
		tr.pos = (int)( ( n % 2 == 0 ) ? p : ( p + 2 ) % (uint32_t)length );
		break;
	}
	case kDirGravity:
	{
		// strength fixed per seed (20-60%); the roll varies with (pass, pos)
		uint32_t strength = 20 + ( hash3( tr.activeSeed, 0x6E01u, 0 ) % 41 );
		uint32_t key = ( (uint32_t)tr.pass << 8 ) | (uint32_t)tr.pos;
		uint32_t roll = hash3( tr.activeSeed, key, 0x6EA1u ) % 100;
		if ( roll < strength && tr.pos != 0 )
			tr.pos = 0;
		else
			tr.pos = ( tr.pos + 1 ) % length;
		break;
	}
	case kDirConverge:
	{
		// 0, length-1, 1, length-2, ...
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % length );
		uint32_t n = tr.phase;
		tr.pos = (int)( ( n % 2 == 0 ) ? ( n / 2 ) : ( (uint32_t)length - 1 - n / 2 ) );
		break;
	}
	case kDirDiverge:
	{
		// from the middle, offsets 0, +1, -1, +2, -2, ...
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % length );
		uint32_t n = tr.phase;
		int k = (int)( ( n + 1 ) / 2 );
		int offset = ( n & 1 ) ? k : -k;
		tr.pos = ( ( length - 1 ) + 2 * offset ) / 2;
		break;
	}
	case kDirSkitter:
	{
		// like Random, but re-rolls rather than repeat the same step
		if ( length > 1 )
		{
			int next;
			do
			{
				dtc->rng = dtc->rng * 1664525u + 1013904223u;
				next = (int)( ( dtc->rng >> 16 ) % (uint32_t)length );
			} while ( next == tr.pos );
			tr.pos = next;
		}
		break;
	}
	case kDirAnchor:
	{
		// 1,2,1,3,1,4... period 2*(length-1)
		if ( length <= 1 )
		{
			tr.pos = 0;
			break;
		}
		uint32_t period = (uint32_t)( length - 1 ) * 2;
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % period );
		uint32_t n = tr.phase;
		tr.pos = (int)( ( n % 2 == 0 ) ? 0 : ( 1 + n / 2 ) );
		break;
	}
	}

	// armed reseeds land at the mode's loop origin; the unordered modes
	// have none, so they take the new seed on the next step
	bool atOrigin;
	switch ( mode )
	{
	case kDirReverse:	atOrigin = ( tr.pos == length - 1 ); break;
	case kDirShuffle:	atOrigin = ( tr.phase == 0 ); break;
	case kDirConverge:
	case kDirDiverge:
	case kDirStride:
	case kDirAnchor:	atOrigin = ( tr.phase == 0 ); break;
	case kDirRandom:
	case kDirDrunk:
	case kDirPools:
	case kDirSkitter:	atOrigin = true; break;
	default:			atOrigin = ( tr.pos == 0 ); break;
	}
	if ( atOrigin )
	{
		if ( tr.pendingSeed >= 0 )
		{
			// a reseed starts a fresh lineage: clear evolution and breathing
			tr.activeSeed = (uint32_t)tr.pendingSeed;
			tr.pendingSeed = -1;
			tr.pass = 0;
			tr.resting = false;
			for ( int s = 0; s < kMaxSteps; ++s )
				tr.epoch[s] = 0;
			if ( mode == kDirShuffle )
			{
				buildShuffle( tr, length, effectiveSeed( pThis, t ), 0 );
				tr.pos = tr.order[ tr.phase ] % length;
			}
		}
		else
		{
			tr.pass += 1;
			if ( mode == kDirShuffle )
			{
				buildShuffle( tr, length, effectiveSeed( pThis, t ), tr.pass );
				tr.pos = tr.order[ tr.phase ] % length;
			}
			// Evolve: this pass, some steps quietly re-roll themselves
			int evAmt = v[ TP( t, kTEvolve ) ];
			if ( evAmt > 0 )
			{
				for ( int s = 0; s < length; ++s )
					if ( ( hash3( tr.activeSeed, ( (uint32_t)tr.pass << 8 ) | (uint32_t)s, 0xE01Fu ) % 100 ) < (uint32_t)evAmt )
						tr.epoch[s] += 1;
			}
			// Breathe: maybe this whole pass rests
			int br = v[ TP( t, kTBreathe ) ];
			tr.resting = br > 0
				&& ( hash3( tr.activeSeed, tr.pass, 0xB4EAu ) % 100 ) < (uint32_t)br;
		}
	}

	tr.stepPeriod = stepPeriod;
	tr.advances += 1;

	// Currents: a fresh target every advance, from this track's OWN seed
	{
		uint32_t cs = (uint32_t)tr.pos | ( (uint32_t)tr.epoch[tr.pos] << 8 );
		tr.currentPrev = tr.currentVolts;
		tr.currentTarget = ( hash3( tr.activeSeed, cs, 0xC4E7u ) % 1001 ) * 0.01f;	// 0.00-10.00V
		tr.currentPeriod = stepPeriod;
		tr.currentElapsed = 0;
	}

	// End-of-sequence: every Length advances, whatever the direction
	tr.eosCount += 1;
	if ( tr.eosCount >= (uint32_t)length )
	{
		tr.eosCount = 0;
		uint32_t pw = stepPeriod / 2;
		tr.eosRemaining = pw ? pw : 1;
	}

	bool muted = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );

	// Ledger: an external track's step is the host's to play.
	if ( pThis->external[t] )
	{
		tr.gateRemaining = 0;
		tr.tieHeld = false;
		if ( pThis->host )
			pThis->host->externalStep( pThis, t, tr.pos, stepPeriod, muted || tr.resting );
		return;
	}

	StepEval ev;
	evalStep( pThis, t, tr.pos, ev );

	if ( muted || tr.resting || !ev.fires )
	{
		if ( tr.tieHeld )
			tr.gateRemaining = 0;		// a tie ending in a rest closes the gate
		tr.tieHeld = false;
		if ( muted )
			tr.gateRemaining = 0;
		return;
	}

	tr.pitchVolts = ( ev.note - 48 ) * ( 1.0f / 12.0f );

	// MIDI note out: monophonic, so any held note is closed first
	if ( tr.midiHeld )
	{
		sendNoteOff( pThis, tr );
		tr.midiHeld = false;
	}
	int midiCh = v[ MP( t ) ];
	if ( midiCh > 0 )
	{
		int note = ev.note;
		if ( note < 0 ) note = 0;
		else if ( note > 127 ) note = 127;
		uint32_t dest = (uint32_t)v[ kGMidiDest ];
		uint8_t vel = (uint8_t)v[ kGMidiVelocity ];
		if ( pThis->host )
			pThis->host->sendMidi3( dest, (uint8_t)( 0x90 | ( midiCh - 1 ) ), (uint8_t)note, vel );
		tr.midiHeld = true;
		tr.midiHeldNote = (uint8_t)note;
		tr.midiHeldChan = (uint8_t)midiCh;
		tr.midiHeldDest = dest;
	}

	if ( ev.tie )
	{
		// hold the gate through this step and into the next
		tr.gateRemaining = stepPeriod + stepPeriod / 2;
	}
	else
	{
		uint32_t g = ( stepPeriod * (uint32_t)v[ TP( t, kTGate ) ] ) / 100;
		tr.gateRemaining = g ? g : 1;
	}
	tr.tieHeld = ev.tie;
}

// Fire a step now, or - with Slop - after a seeded per-note delay of up to
// half a step, so the same notes drift by the same amount every loop.
static inline void scheduleAdvance( Shoal* pThis, int t, uint32_t stepPeriod, bool anySolo )
{
	const int16_t* v = pThis->v;
	TrackState& tr = pThis->dtc->tracks[t];

	if ( tr.slopCountdown )
	{
		// a delayed step is still pending (clock sped up) - fire it first
		tr.slopCountdown = 0;
		advanceTrack( pThis, t, tr.slopPeriod, anySolo );
	}

	int slop = v[ TP( t, kTSlop ) ];
	if ( slop > 0 )
	{
		int length = v[ TP( t, kTLength ) ];
		int nextPos = ( tr.pos < 0 ) ? 0 : ( tr.pos + 1 ) % length;
		nextPos = ( nextPos + v[ XP( t, kXShift ) ] ) % length;
		if ( nextPos < 0 )
			nextPos += length;
		uint32_t r10 = hash3( effectiveSeed( pThis, t ), nextPos, 0x5107u ) & 1023;
		uint32_t maxDelay = stepPeriod >> 1;
		if ( maxDelay > 0xFFFFFF )
			maxDelay = 0xFFFFFF;
		uint32_t num = (uint32_t)( ( (uint64_t)maxDelay * ( (uint32_t)slop * r10 ) ) >> 10 );
		uint32_t delay = num / 100u;
		if ( delay > 0 )
		{
			tr.slopCountdown = delay;
			tr.slopPeriod = stepPeriod;
			return;
		}
	}
	advanceTrack( pThis, t, stepPeriod, anySolo );
}

// One sample of master-clock housekeeping. clockMode: 0 external, 1 internal,
// 2 MIDI - external with no clock input falls through to the internal free-run.
static inline bool masterClockTick( Dtc* dtc, const float* clockIn,
									int clockMode, bool run, int i, uint32_t& period )
{
	bool tick = false;
	if ( clockMode == 0 && clockIn )
	{
		float c = clockIn[i];
		if ( !dtc->clockHigh && c > 1.0f )
		{
			dtc->clockHigh = true;
			if ( dtc->samplesSinceClock > 0 )
				dtc->extPeriod = dtc->samplesSinceClock;
			dtc->samplesSinceClock = 0;
			tick = run;
		}
		else if ( dtc->clockHigh && c < 0.1f )
			dtc->clockHigh = false;
		if ( dtc->samplesSinceClock < 0x7FFFFFFF )
			dtc->samplesSinceClock += 1;
		period = dtc->extPeriod ? dtc->extPeriod : dtc->sampleRate / 4;
	}
	else if ( clockMode == 2 )
	{
		if ( dtc->midiSamplesSinceTick < 0x7FFFFFFF )
			dtc->midiSamplesSinceTick += 1;
		if ( run && dtc->midiTickPending )
		{
			dtc->midiTickPending = false;
			if ( dtc->midiSamplesSinceTick > 0 )
				dtc->midiPeriod = dtc->midiSamplesSinceTick;
			dtc->midiSamplesSinceTick = 0;
			tick = true;
		}
		else if ( !run )
			dtc->midiTickPending = false;	// stopped: don't queue up a tick for later
		period = dtc->midiPeriod ? dtc->midiPeriod : dtc->sampleRate / 4;
	}
	else
	{
		if ( run )
		{
			if ( dtc->intCountdown <= 1 )
			{
				tick = true;
				dtc->intCountdown = dtc->intPeriod;
			}
			else
				dtc->intCountdown -= 1;
		}
		period = dtc->intPeriod;
	}
	return tick;
}

// MIDI clock in (Clock source = MIDI): 24 PPQN, x1 = one step per quarter.
static inline void midiRealtime( Shoal* pThis, uint8_t byte )
{
	Dtc* dtc = pThis->dtc;
	if ( pThis->v[kGClockSource] != 2 )
		return;
	switch ( byte )
	{
	case 0xF8:
		dtc->midiClockCount += 1;
		if ( dtc->midiClockCount >= 24 )
		{
			dtc->midiClockCount -= 24;
			dtc->midiTickPending = true;
		}
		break;
	case 0xFA:		// start - also a reset, so the pattern lands on 1 at bar 1
		dtc->midiRunning = true;
		dtc->midiClockCount = 0;
		dtc->midiTickPending = false;
		dtc->resetPrime = true;
		break;
	case 0xFB:		// continue - resumes in place, no reset
		dtc->midiRunning = true;
		break;
	case 0xFC:		// stop
		dtc->midiRunning = false;
		break;
	}
}

static inline bool anySoloOf( const Shoal* pThis )
{
	for ( int t = 0; t < kNumTracks; ++t )
		if ( pThis->solo[t] ) return true;
	return false;
}

// shoal.cpp's step(): numFrames samples of everything.
static inline void step( Shoal* pThis, const Io& io, int numFrames )
{
	Dtc* dtc = pThis->dtc;
	const int16_t* v = pThis->v;

	dtc->uiClock += (uint32_t)numFrames;

	if ( !dtc->seedsInited )
	{
		for ( int t = 0; t < kNumTracks; ++t )
		{
			dtc->tracks[t].activeSeed = (uint32_t)v[ TP( t, kTSeed ) ];
			dtc->tracks[t].pendingSeed = -1;
		}
		dtc->seedsInited = true;
	}
	if ( dtc->scaleCached != v[kGScale] )
		rebuildScale( dtc, v[kGScale] );

	if ( dtc->reseedAllPending >= 0 )
	{
		// deferred "reseed all": scatter seeds into the per-track Seed params
		uint32_t base = (uint32_t)dtc->reseedAllPending;
		dtc->reseedAllPending = -1;
		if ( pThis->host )
		{
			for ( int t = 0; t < kNumTracks; ++t )
			{
				int16_t s = (int16_t)( hash3( base, t, 0x5EEDu ) % 1000 );
				pThis->host->setParameterFromAudio( pThis, TP( t, kTSeed ), s );
			}
		}
	}

	const float* clockIn = io.clockIn;
	const float* resetIn = io.resetIn;
	const float* reseedIn = io.reseedIn;
	float* clkOut = io.clkOut;
	const float pulse = pThis->pulseVolts;

	// Reseed trigger: each rising edge is the reseed-all gesture. Scanned
	// before Freeze: a trigger during a freeze still arms.
	if ( reseedIn )
	{
		for ( int i = 0; i < numFrames; ++i )
		{
			float r = reseedIn[i];
			if ( !dtc->reseedInHigh && r > 1.0f )
			{
				dtc->reseedInHigh = true;
				dtc->rng = dtc->rng * 1664525u + 1013904223u;
				int16_t base = (int16_t)( ( dtc->rng >> 16 ) % 1000 );
				if ( (int32_t)base == dtc->lastReseedAll )
					base = (int16_t)( ( base + 1 ) % 1000 );	// an unchanged value would be ignored
				if ( pThis->host )
					pThis->host->setParameterFromAudio( pThis, kGReseedAll, base );
			}
			else if ( dtc->reseedInHigh && r < 0.1f )
				dtc->reseedInHigh = false;
		}
	}

	bool mutedNow[kNumTracks];
	bool anySolo = anySoloOf( pThis );
	for ( int t = 0; t < kNumTracks; ++t )
		mutedNow[t] = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );

	int clockMode = v[kGClockSource];		// 0 external, 1 internal, 2 MIDI
	bool run = v[kGRun] && ( clockMode != 2 || dtc->midiRunning );

	float gLevel[kNumTracks], pScaleF[kNumTracks], pOffF[kNumTracks];
	for ( int t = 0; t < kNumTracks; ++t )
	{
		gLevel[t] = (float)v[ XP( t, kXGateVolts ) ];
		pScaleF[t] = (float)v[ XP( t, kXPitchScale ) ] * 0.01f;
		pOffF[t] = (float)v[ XP( t, kXPitchOffset ) ] * 0.1f;
	}

	// Freeze: nothing advances, every started track's gate is held high, the
	// current notes hang as a chord. Clock out Stops or Runs (kGFreezeClock).
	if ( v[kGFreeze] )
	{
		bool clockRuns = v[kGFreezeClock];
		if ( !clockRuns )
		{
			dtc->clockPulse = 0;
			if ( clkOut )
				for ( int i = 0; i < numFrames; ++i )
					clkOut[i] = 0.0f;
		}
		for ( int t = 0; t < kNumTracks; ++t )
			dtc->tracks[t].eosRemaining = 0;
		for ( int i = 0; i < numFrames; ++i )
		{
			if ( clockRuns )
			{
				uint32_t period;
				if ( masterClockTick( dtc, clockIn, clockMode, run, i, period ) )
				{
					// the grid advances too, so divided tracks resume in phase
					dtc->tickCount += 1;
					dtc->clockPulse = period / 2;
				}
				if ( clkOut )
					clkOut[i] = dtc->clockPulse ? pulse : 0.0f;
				if ( dtc->clockPulse )
					dtc->clockPulse -= 1;
			}
			for ( int t = 0; t < kNumTracks; ++t )
			{
				TrackState& tr = dtc->tracks[t];
				float gate = ( tr.pos >= 0 && !mutedNow[t] ) ? gLevel[t] : 0.0f;
				bool midiSoundingNow = gate > 0.0f;
				if ( tr.midiSounding && !midiSoundingNow && tr.midiHeld )
				{
					sendNoteOff( pThis, tr );
					tr.midiHeld = false;
				}
				tr.midiSounding = midiSoundingNow;
				if ( io.pitchOut[t] )
					io.pitchOut[t][i] = tr.pitchVolts * pScaleF[t] + pOffF[t];
				if ( io.gateOut[t] )
					io.gateOut[t][i] = gate;
				if ( io.currentOut[t] )
					io.currentOut[t][i] = tr.currentVolts;
				if ( io.eosOut[t] )
					io.eosOut[t][i] = 0.0f;
			}
		}
		return;
	}

	for ( int i = 0; i < numFrames; ++i )
	{
		if ( resetIn )
		{
			float r = resetIn[i];
			if ( !dtc->resetHigh && r > 1.0f )
			{
				dtc->resetHigh = true;
				dtc->resetPrime = true;
			}
			else if ( dtc->resetHigh && r < 0.1f )
				dtc->resetHigh = false;
		}

		// master clock tick
		uint32_t period;
		bool tick = masterClockTick( dtc, clockIn, clockMode, run, i, period );

		if ( tick )
		{
			if ( dtc->resetPrime )
				dtc->tickCount = 0;		// the reset tick is a grid origin for every divider

			for ( int t = 0; t < kNumTracks; ++t )
			{
				TrackState& tr = dtc->tracks[t];
				int rate = v[ TP( t, kTRate ) ];
				uint32_t num = rateMult[rate], den = rateDiv[rate];

				if ( dtc->resetPrime )
				{
					tr.pos = -1;
					tr.extraPending = 0;
					tr.slopCountdown = 0;
					tr.tieHeld = false;
					tr.eosCount = 0;
				}

				// Bresenham/Euclidean scheduling, phase-locked to tickCount
				uint32_t before = ( dtc->tickCount * num ) / den;
				uint32_t after = ( ( dtc->tickCount + 1 ) * num ) / den;
				uint32_t count = after - before;
				uint32_t stepPeriod = ( period * den ) / num;

				if ( dtc->resetPrime )
				{
					// reset is always a grid origin: land tight, then resume
					advanceTrack( pThis, t, stepPeriod, anySolo );
					tr.extraPending = ( count > 0 ) ? count - 1 : 0;
					tr.subPeriod = stepPeriod;
					tr.subCountdown = stepPeriod;
				}
				else if ( count == 0 )
				{
					tr.extraPending = 0;
				}
				else
				{
					scheduleAdvance( pThis, t, stepPeriod, anySolo );
					tr.extraPending = count - 1;
					tr.subPeriod = stepPeriod;
					tr.subCountdown = stepPeriod;
				}
			}
			dtc->resetPrime = false;
			dtc->tickCount += 1;
		}

		// Clock out: high for half the master (x1) period
		if ( tick )
			dtc->clockPulse = period / 2;
		if ( clkOut )
			clkOut[i] = dtc->clockPulse ? pulse : 0.0f;
		if ( dtc->clockPulse )
			dtc->clockPulse -= 1;

		for ( int t = 0; t < kNumTracks; ++t )
		{
			TrackState& tr = dtc->tracks[t];
			if ( tr.extraPending )
			{
				if ( tr.subCountdown <= 1 )
				{
					tr.extraPending -= 1;
					tr.subCountdown = tr.subPeriod;
					scheduleAdvance( pThis, t, tr.subPeriod, anySolo );
				}
				else
					tr.subCountdown -= 1;
			}

			if ( tr.slopCountdown )
			{
				if ( tr.slopCountdown <= 1 )
				{
					tr.slopCountdown = 0;
					advanceTrack( pThis, t, tr.slopPeriod, anySolo );
				}
				else
					tr.slopCountdown -= 1;
			}

			float gate = 0.0f;
			if ( tr.gateRemaining > 0 && !mutedNow[t] )
			{
				gate = gLevel[t];
				tr.gateRemaining -= 1;
			}
			else if ( tr.gateRemaining > 0 )
				tr.gateRemaining -= 1;

			// MIDI note-off on the same signal that silences the CV gate
			bool midiSoundingNow = gate > 0.0f;
			if ( tr.midiSounding && !midiSoundingNow && tr.midiHeld )
			{
				sendNoteOff( pThis, tr );
				tr.midiHeld = false;
			}
			tr.midiSounding = midiSoundingNow;

			if ( io.pitchOut[t] )
				io.pitchOut[t][i] = tr.pitchVolts * pScaleF[t] + pOffF[t];
			if ( io.gateOut[t] )
				io.gateOut[t][i] = gate;

			// Currents: smoothstep towards the target over one step period
			if ( tr.currentElapsed < tr.currentPeriod )
				tr.currentElapsed += 1;
			float ct = ( tr.currentPeriod > 0 )
					? (float)tr.currentElapsed / (float)tr.currentPeriod : 1.0f;
			float ease = ct * ct * ( 3.0f - 2.0f * ct );
			tr.currentVolts = tr.currentPrev + ( tr.currentTarget - tr.currentPrev ) * ease;
			if ( io.currentOut[t] )
				io.currentOut[t][i] = tr.currentVolts;

			// End-of-sequence trigger
			bool eosHigh = tr.eosRemaining > 0;
			if ( eosHigh )
				tr.eosRemaining -= 1;
			if ( io.eosOut[t] )
				io.eosOut[t][i] = eosHigh ? pulse : 0.0f;
		}
	}
}

} // namespace shoal
} // namespace ledger
