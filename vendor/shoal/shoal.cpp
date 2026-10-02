/*
 * Shoal - an 8-track generative melody sequencer plug-in for the
 * Expert Sleepers disting NT.
 *
 * An Ormer Modular instrument. Made in Guernsey.
 *
 * Eight voices moving as one: tracks generate seeded random melodies and
 * can follow one another like fish in a shoal; a reseed is the shoal
 * turning.
 *
 *  - 8 tracks, each generating a seeded random melody: 64 steps max,
 *    per-track length (1-64) and clock rate (/64 ... x64)
 *  - Patterns are a pure function of the track's Seed parameter, so they
 *    loop identically and are recalled exactly by presets
 *  - Per-track CHANCE, NOTE-/NOTE+ (scale steps), OCTAVE-/OCTAVE+,
 *    NOTE CHANCE, GATE length, TIE chance, fixed octave offset
 *  - Linked-follow sampling: a track can mirror another track's material
 *    (reseeds propagate) filtered through its own settings
 *  - Global scale quantisation (all tracks), external or internal clock,
 *    reset input, per-track and global reseed (armed, land at loop start),
 *    reseed trigger input, per-track pattern Shift
 *  - Pitch CV at 1V/oct (0V = C3) with per-track scale/offset, gates
 *    1-10V (default 5V); 16 individually routable outputs
 *  - Custom UI on pots and encoders; buttons 1-4 left to the OS
 *  - Idle screensaver: the shoal itself swims the screen, one fish per
 *    track, darting on every note - an ambient view of the patch
 *
 * MIT License. Built against the distingNT_API (kNT_apiVersionCurrent).
 */

#include <stddef.h>
#include <new>
#include <distingnt/api.h>

enum
{
	kNumTracks = 8,
	kMaxSteps = 64,
};

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

enum
{
	kGClockSource,
	kGBPM,
	kGRun,
	kGFreeze,			// hold every sounding note as a chord; nothing advances
	kGScale,
	kGRoot,
	kGWeight,			// consonance gravity: % of notes snapped to root/3rd/5th
	kGSaver,			// idle screensaver: the shoal swims the screen
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
	kTBreathe,			// % chance a whole pass rests
	kTNote,				// bipolar: magnitude = probability + reach, sign = direction
	kTOct,				// bipolar: magnitude = probability + reach, sign = direction
	kTEvolve,			// % of steps that re-roll themselves each pass
	kTGate,
	kTTie,
	kTSlop,
	kTOctave,
	kTTrans,			// fixed transpose, in scale degrees - stays in key
	kTSource,
	kTMute,
	kTSeed,
	kNumTrackParams,
};

enum
{
	kTrackBase = kNumGlobalParams,
	kRoutingBase = kTrackBase + kNumTracks * kNumTrackParams,
	kNumRoutingParams = 4,				// gate out, gate mode, pitch out, pitch mode (gate first - NT convention)
	// Clock out is appended AFTER the routing block so every earlier
	// parameter keeps its index - beta.8 presets load unchanged
	kGClockOut = kRoutingBase + kNumTracks * kNumRoutingParams,
	kGClockOutMode,
	// v1.1: everything below is APPENDED - v1.0.0 preset indices unchanged
	kGReseedIn,			// trigger input: each rising edge is a reseed-all gesture
	kGFreezeClock,		// what Clock out does while frozen: Stops / Runs
	kExtBase,			// per-track extras, one block of kNumExtParams per track
	kNumExtParams = 4,
	// v1.2: MIDI velocity and destination are GLOBAL (one shared setting
	// for every track), not per-track. Originally per-track; changed
	// 24 Aug 2026 after the real-hardware parameter ceiling was found
	// (~241 params, empirically bisected - see PROJECT-NOTES.md "the
	// real ceiling") forced v1.2's total back down from 255. Channel
	// stays per-track - that's the one MIDI setting testers specifically
	// asked to vary per track; velocity/destination didn't have an
	// equivalent request, so they're the ones that gave way.
	kGMidiVelocity = kExtBase + kNumTracks * kNumExtParams,
	kGMidiDest,
	// v1.2: MIDI channel, one param per track (0 = off).
	kMidiBase,
	kNumMidiParams = 1,
	// v1.2: Currents - a per-track seeded drift CV output, bus only (no
	// per-track Add/Replace mode - dropped in the same pass as the MIDI
	// change above, for the same reason; a modulation CV summing onto an
	// existing bus is a narrow use case, same call already made for EOS).
	kCurrentBase = kMidiBase + kNumTracks * kNumMidiParams,
	kNumCurrentParams = 1,	// current out
	// v1.2: End-of-sequence gate/trigger - fires a short 5V pulse every
	// time a track completes Length advances. Bus only, unchanged.
	kEosBase = kCurrentBase + kNumTracks * kNumCurrentParams,
	kNumEosParams = 1,	// EOS out
	kNumParameters = kEosBase + kNumTracks * kNumEosParams,
};

enum
{
	kXShift,			// rotate the pattern lookup, bipolar, non-destructive
	kXGateVolts,		// gate high level (was hard-coded 5V)
	kXPitchScale,		// pitch CV scale %, 100 = 1V/oct
	kXPitchOffset,		// pitch CV offset, 0.1V steps
};

#define TP( t, off )	( kTrackBase + (t) * kNumTrackParams + (off) )
#define RP( t, off )	( kRoutingBase + (t) * kNumRoutingParams + (off) )
#define XP( t, off )	( kExtBase + (t) * kNumExtParams + (off) )
// MIDI channel is the only per-track MIDI param now (velocity/dest are
// global - see kGMidiVelocity/kGMidiDest above), and Current out is the
// only per-track Currents param (no mode) - both single-param-per-track,
// same shape as EOS, hence the single-arg macros matching EP() below.
#define MP( t )			( kMidiBase + (t) )
#define CP( t )			( kCurrentBase + (t) )
// EOS is bus-only (no mode param - see TRACK_EOS() below): the disting NT
// API addresses page parameters via a uint8_t (api.h's _NT_parameterPage::
// params), which caps a plugin at 256 total parameters - real, but NOT the
// binding ceiling in practice; the real one is ~241, found empirically on
// real hardware (see PROJECT-NOTES.md "the real ceiling"). A trigger bus
// is a poor fit for Add mode anyway - always Replace, one param, done.
#define EP( t )			( kEosBase + (t) )

// v1.2: "MIDI" appended (never inserted - Clock source is a preset value,
// same append-only discipline as the rate table). External/Internal keep
// their existing meaning and index.
static char const * const enumStringsClockSource[] = { "External", "Internal", "MIDI" };
// v1.2: MIDI destination is a literal _NT_midiDestination bitmask (bit0
// breakout, bit1 select bus, bit2 USB, bit3 internal - see api.h), so the
// enum index IS the value passed straight to NT_sendMidi3ByteMessage, no
// lookup table needed. "Internal" (same-NT routing to another algorithm,
// e.g. Poly FM) was a real, tester-validated use case, not a hypothetical -
// see PROJECT-NOTES.md.
static char const * const enumStringsMidiDest[] = {
	"None", "Breakout", "Bus", "Breakout+Bus",
	"USB", "Breakout+USB", "Bus+USB", "Brk+Bus+USB",
	"Internal", "Breakout+Int", "Bus+Int", "Brk+Bus+Int",
	"USB+Int", "Brk+USB+Int", "Bus+USB+Int", "All",
};
static char const * const enumStringsOffOn[] = { "Off", "On" };
static char const * const enumStringsFrozenClock[] = { "Stops", "Runs" };
static char const * const enumStringsSaver[] = { "Off", "1 min", "5 min" };
// v1.2: all 29 rates kept in true ascending numeric order (Oliver's
// call, 24 Aug 2026 - a deliberate, one-time break of the "always
// append, never reorder" rule that held through v1.1.0 and the first
// two v1.2 rate appends, made because v1.2.0-beta.1 has never shipped
// or been hardware-tested, so no preset in the wild references indices
// 21-28 yet; every index below is free to move exactly once). Non-
// integer rates don't fit the plain divide-or-multiply table on their
// own; num/den express them as a ratio instead (e.g. num=3,den=2 for
// x1.5, num=2,den=3 for /1.5), the same representation every integer
// entry already uses (a divide is just den=D,num=1; a multiply is
// num=M,den=1) - see the Bresenham/Euclidean scheduler in the
// master-tick loop. The six non-integer ratios (/5.3, /2.6, x1.25,
// x1.3, x2.6, x5.3) are literal decimals, confirmed with Oliver, not
// rounded to a cleaner fraction (e.g. x1.3 is exactly 13/10, not 4/3).
// IMPORTANT: from this point on the
// append-only rule is back in force - any future rate goes at the end.
static char const * const enumStringsRate[] = {
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
	kDirTide,			// forwards, rotating the loop start one step per pass
	kDirShuffle,		// every step once per pass, new seeded order each pass
	kDirPools,			// dwell in a small pocket of steps, then hop to another
	// v1.2: New Directions, appended (never inserted - existing preset
	// indices must never move). Zero new parameters - all three read only
	// existing per-track state (activeSeed, pos, pass, kTSource), same as
	// every mode above them.
	kDirStride,			// skip by a seed-derived amount instead of ±1
	kDirGravity,		// seed-derived chance each step to snap back to step 1
	kDirConverge,		// folds inward from both ends toward the middle
	kDirDiverge,		// unfolds outward from the middle toward both ends
	// v1.2.0-beta.1, appended same day (25 Aug 2026): two more, again
	// zero new parameters.
	kDirSkitter,		// like Random, but never repeats the same step twice in a row
	kDirAnchor,			// alternates step 1 with each other step in turn
};

static char const * const enumStringsDirection[] = {
	"Forwards", "Reverse", "Pendulum", "Random", "Drunk", "Pong",
	"Tide", "Shuffle", "Pools", "Stride", "Gravity", "Converge", "Diverge",
	"Skitter", "Anchor",
};

static char const * const enumStringsScale[] = {
	"Chromatic",
	"Major",
	"Natural minor",
	"Harmonic minor",
	"Dorian",
	"Phrygian",
	"Lydian",
	"Mixolydian",
	"Major pentatonic",
	"Minor pentatonic",
	"Blues",
	"Hirajoshi",
	"In-Sen",
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

static char const * const enumStringsSource[] = {
	"Off", "Track 1", "Track 2", "Track 3", "Track 4",
	"Track 5", "Track 6", "Track 7", "Track 8",
};

#define TRACK_PARAMS( seedDef ) \
	{ .name = "Length", .min = 1, .max = kMaxSteps, .def = 16, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Rate", .min = 0, .max = kNumRates - 1, .def = kRateX1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsRate }, \
	{ .name = "Direction", .min = 0, .max = 14, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsDirection }, \
	{ .name = "Chance", .min = 0, .max = 100, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Breathe", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Note", .min = -100, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Oct", .min = -100, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Evolve", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Gate length", .min = 5, .max = 95, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Tie", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Slop", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Octave", .min = -3, .max = 3, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Transpose", .min = -7, .max = 7, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Sample source", .min = 0, .max = 8, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsSource }, \
	{ .name = "Mute", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
	{ .name = "Seed", .min = 0, .max = 999, .def = seedDef, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },

// Like NT_PARAMETER_CV_OUTPUT_WITH_MODE, but: outputs default to None (a
// track touches no bus until you assign it), and the mode defaults to
// Replace - a CV sequencer should own its busses, not sum onto them.
#define ORMER_CV_OUTPUT_REPLACE( n ) \
	NT_PARAMETER_IO( n, 0, 0, kNT_unitCvOutput ) \
	{ .name = n " mode", .min = 0, .max = 1, .def = 1, .unit = kNT_unitOutputMode, .scaling = 0, .enumStrings = NULL },

#define TRACK_ROUTING() \
	ORMER_CV_OUTPUT_REPLACE( "Gate out" ) \
	ORMER_CV_OUTPUT_REPLACE( "Pitch out" )

// v1.1 per-track extras. Appended as their own block AFTER everything the
// v1.0.0 table had, so every released index is unchanged. Defaults are the
// old hard-coded behaviour: shift 0, gates 5V, pitch 100% + 0V offset.
#define TRACK_EXTRAS() \
	{ .name = "Shift", .min = -( kMaxSteps - 1 ), .max = kMaxSteps - 1, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Gate volts", .min = 1, .max = 10, .def = 5, .unit = kNT_unitVolts, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Pitch scale", .min = 5, .max = 200, .def = 100, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Pitch offset", .min = -100, .max = 100, .def = 0, .unit = kNT_unitVolts, .scaling = kNT_scaling10, .enumStrings = NULL },

// v1.2 MIDI note out. Channel is per-track (0 = off is the default -
// updating firmware must never start sending MIDI a patch wasn't already
// asking for); velocity and destination are global - see kGMidiVelocity/
// kGMidiDest in PARAM_TABLE below.
#define TRACK_MIDI() \
	{ .name = "MIDI channel", .min = 0, .max = 16, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL },

// v1.2 Currents: a per-track seeded drift CV output - "the water the
// melody swims in". Bus only (no Add/Replace mode - see the enum comment
// above kGMidiVelocity for why). Defaults to None like every other output,
// so a track sends nothing until you assign it.
#define TRACK_CURRENT() \
	NT_PARAMETER_IO( "Current out", 0, 0, kNT_unitCvOutput )

// v1.2 End-of-sequence: a short 5V trigger, fixed width (half the
// track's own step period), fired from a pure "every Length advances"
// counter - independent of Direction mode, so Random/Drunk/Pools (whose
// loop-origin flag is true on every step) still get a meaningful, evenly
// spaced pulse rather than firing continuously. See PROJECT-NOTES.md.
// Bus only, no Add/Replace mode param - see EP() above for why.
#define TRACK_EOS() \
	NT_PARAMETER_IO( "EOS out", 0, 0, kNT_unitCvOutput )

#define PARAM_TABLE( bpmName ) \
	{ .name = "Clock source", .min = 0, .max = 2, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsClockSource }, \
	{ .name = bpmName, .min = 20, .max = 300, .def = 120, .unit = kNT_unitBPM, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Run", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
	{ .name = "Freeze", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsOffOn }, \
	{ .name = "Scale", .min = 0, .max = (int16_t)(ARRAY_SIZE(scaleMasks)-1), .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsScale }, \
	{ .name = "Root note", .min = 0, .max = 127, .def = 48, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Weight", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "Screensaver", .min = 0, .max = 2, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsSaver }, \
	NT_PARAMETER_CV_INPUT( "Clock input", 1, 1 ) \
	NT_PARAMETER_CV_INPUT( "Reset input", 0, 2 ) \
	{ .name = "Reseed all", .min = 0, .max = 999, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	TRACK_PARAMS( 1 )   TRACK_PARAMS( 98 )  TRACK_PARAMS( 195 ) TRACK_PARAMS( 292 ) \
	TRACK_PARAMS( 389 ) TRACK_PARAMS( 486 ) TRACK_PARAMS( 583 ) TRACK_PARAMS( 680 ) \
	TRACK_ROUTING() TRACK_ROUTING() TRACK_ROUTING() TRACK_ROUTING() \
	TRACK_ROUTING() TRACK_ROUTING() TRACK_ROUTING() TRACK_ROUTING() \
	ORMER_CV_OUTPUT_REPLACE( "Clock out" ) \
	NT_PARAMETER_CV_INPUT( "Reseed input", 0, 0 ) \
	{ .name = "Frozen clock out", .min = 0, .max = 1, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsFrozenClock }, \
	TRACK_EXTRAS() TRACK_EXTRAS() TRACK_EXTRAS() TRACK_EXTRAS() \
	TRACK_EXTRAS() TRACK_EXTRAS() TRACK_EXTRAS() TRACK_EXTRAS() \
	{ .name = "MIDI velocity", .min = 1, .max = 127, .def = 100, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL }, \
	{ .name = "MIDI dest", .min = 0, .max = 15, .def = 1, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsMidiDest }, \
	TRACK_MIDI() TRACK_MIDI() TRACK_MIDI() TRACK_MIDI() \
	TRACK_MIDI() TRACK_MIDI() TRACK_MIDI() TRACK_MIDI() \
	TRACK_CURRENT() TRACK_CURRENT() TRACK_CURRENT() TRACK_CURRENT() \
	TRACK_CURRENT() TRACK_CURRENT() TRACK_CURRENT() TRACK_CURRENT() \
	TRACK_EOS() TRACK_EOS() TRACK_EOS() TRACK_EOS() \
	TRACK_EOS() TRACK_EOS() TRACK_EOS() TRACK_EOS()

static const _NT_parameter parameters[] = { PARAM_TABLE( "BPM" ) };

static_assert( ARRAY_SIZE(parameters) == kNumParameters, "parameter count mismatch" );

static const uint8_t pageGlobal[] = {
	kGClockSource, kGBPM, kGRun, kGFreeze, kGScale, kGRoot, kGWeight,
	kGSaver, kGClockIn, kGResetIn, kGReseedIn, kGClockOut, kGClockOutMode,
	kGFreezeClock, kGReseedAll, kGMidiVelocity, kGMidiDest,
};

#define TRACK_PAGE( t ) \
	{ TP(t,0), TP(t,1), TP(t,2), XP(t,kXShift), TP(t,3), TP(t,4), TP(t,5), TP(t,6), TP(t,7), \
	  TP(t,8), TP(t,9), TP(t,10), TP(t,11), TP(t,12), TP(t,13), TP(t,14), TP(t,15) }
#define ROUT_PAGE( t ) \
	{ RP(t,0), RP(t,1), XP(t,kXGateVolts), RP(t,2), RP(t,3), XP(t,kXPitchScale), XP(t,kXPitchOffset), \
	  MP(t), CP(t), EP(t) }

static const uint8_t pageT0[] = TRACK_PAGE(0);
static const uint8_t pageT1[] = TRACK_PAGE(1);
static const uint8_t pageT2[] = TRACK_PAGE(2);
static const uint8_t pageT3[] = TRACK_PAGE(3);
static const uint8_t pageT4[] = TRACK_PAGE(4);
static const uint8_t pageT5[] = TRACK_PAGE(5);
static const uint8_t pageT6[] = TRACK_PAGE(6);
static const uint8_t pageT7[] = TRACK_PAGE(7);
static const uint8_t pageR0[] = ROUT_PAGE(0);
static const uint8_t pageR1[] = ROUT_PAGE(1);
static const uint8_t pageR2[] = ROUT_PAGE(2);
static const uint8_t pageR3[] = ROUT_PAGE(3);
static const uint8_t pageR4[] = ROUT_PAGE(4);
static const uint8_t pageR5[] = ROUT_PAGE(5);
static const uint8_t pageR6[] = ROUT_PAGE(6);
static const uint8_t pageR7[] = ROUT_PAGE(7);

static const _NT_parameterPage pages[] = {
	{ .name = "Global",    .numParams = ARRAY_SIZE(pageGlobal), .group = 1, .params = pageGlobal },
	{ .name = "Track 1",   .numParams = ARRAY_SIZE(pageT0), .group = 2, .params = pageT0 },
	{ .name = "Track 2",   .numParams = ARRAY_SIZE(pageT1), .group = 2, .params = pageT1 },
	{ .name = "Track 3",   .numParams = ARRAY_SIZE(pageT2), .group = 2, .params = pageT2 },
	{ .name = "Track 4",   .numParams = ARRAY_SIZE(pageT3), .group = 2, .params = pageT3 },
	{ .name = "Track 5",   .numParams = ARRAY_SIZE(pageT4), .group = 2, .params = pageT4 },
	{ .name = "Track 6",   .numParams = ARRAY_SIZE(pageT5), .group = 2, .params = pageT5 },
	{ .name = "Track 7",   .numParams = ARRAY_SIZE(pageT6), .group = 2, .params = pageT6 },
	{ .name = "Track 8",   .numParams = ARRAY_SIZE(pageT7), .group = 2, .params = pageT7 },
	{ .name = "Routing 1", .numParams = ARRAY_SIZE(pageR0), .group = 3, .params = pageR0 },
	{ .name = "Routing 2", .numParams = ARRAY_SIZE(pageR1), .group = 3, .params = pageR1 },
	{ .name = "Routing 3", .numParams = ARRAY_SIZE(pageR2), .group = 3, .params = pageR2 },
	{ .name = "Routing 4", .numParams = ARRAY_SIZE(pageR3), .group = 3, .params = pageR3 },
	{ .name = "Routing 5", .numParams = ARRAY_SIZE(pageR4), .group = 3, .params = pageR4 },
	{ .name = "Routing 6", .numParams = ARRAY_SIZE(pageR5), .group = 3, .params = pageR5 },
	{ .name = "Routing 7", .numParams = ARRAY_SIZE(pageR6), .group = 3, .params = pageR6 },
	{ .name = "Routing 8", .numParams = ARRAY_SIZE(pageR7), .group = 3, .params = pageR7 },
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};

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
	// v1.2 MIDI note out. A track is monophonic, so at most one MIDI note
	// is ever held per track - these cache exactly what was sent, so the
	// matching note-off always targets the channel/destination/note it was
	// opened with, even if the track's MIDI params changed while it rang.
	bool		midiHeld;
	uint8_t		midiHeldNote;
	uint8_t		midiHeldChan;		// 1-16 (never 0 while midiHeld)
	uint32_t	midiHeldDest;
	bool		midiSounding;		// previous sample's gate-high state, for edge detection
	// v1.2 Currents: a slow seeded drift CV, independent of the melody. A
	// fresh target lands each time this track advances (fired or not);
	// currentVolts smoothsteps from currentPrev to currentTarget over
	// currentPeriod samples so it never jumps.
	float		currentPrev;
	float		currentTarget;
	uint32_t	currentPeriod;
	uint32_t	currentElapsed;
	float		currentVolts;
	// v1.2 End-of-sequence: a pure "every Length advances" counter,
	// independent of Direction/atOrigin (see the EOS design note above
	// TRACK_EOS()). eosRemaining is a countdown, same pattern as
	// gateRemaining/clockPulse elsewhere in this file.
	uint32_t	eosCount;
	uint32_t	eosRemaining;
};

struct _melodySeq_DTC
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
	uint32_t	uiClock;			// sample counter for real-time UI gestures (holds)
	uint32_t	rng;				// audio-context RNG for Random/Drunk directions
	uint32_t	samplesSinceClock;
	uint32_t	extPeriod;
	uint32_t	intPeriod;
	uint32_t	intCountdown;
	uint32_t	clockPulse;			// samples left of the current Clock out high phase
	// v1.2 MIDI clock in (Clock source = MIDI). midiRealtime() sets these;
	// step() consumes them - same cross-context discipline the reseed
	// trigger input already uses elsewhere in this file (no locks anywhere
	// in this codebase; the API gives none, and none has been needed).
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
};

struct _melodySeqAlgorithm : public _NT_algorithm
{
	_melodySeqAlgorithm( _melodySeq_DTC* dtc_ ) : dtc( dtc_ ) {}

	_melodySeq_DTC*	dtc;

	// custom UI state (runtime only)
	int32_t		sel;
	bool		potCaptured[3];
	float		potPrev[3];
	uint32_t	potQuiet[3];		// uiClock until which a pushed pot's value is ignored
	int16_t		potStash[kNumTracks][3];	// dialled values stashed by push-to-home
	bool		potStashValid[kNumTracks][3];
	bool		solo[kNumTracks];
	uint32_t	encLDown, encRDown;	// uiClock at press
	bool		encLHeldPrev, encRHeldPrev;
	bool		encLTurned, encRTurned;
	bool		encLFired, encRFired;	// hold action already fired this press
	uint32_t	uiRng;
	uint32_t	drawTick;

	// screensaver (runtime only)
	uint32_t	lastTouch;			// uiClock at the last control touch / edit
	bool		saverActive;		// the next touch only wakes, nothing else
	bool		selfWrite;			// parameterChanged came from our own UI write
	float		fishX[kNumTracks];
	int16_t		bubX[4];
	int16_t		bubY[4];
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

static void rebuildScale( _melodySeq_DTC* dtc, int scaleIdx )
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
static int effectiveTrack( const _melodySeqAlgorithm* pThis, int t )
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

static uint32_t effectiveSeed( const _melodySeqAlgorithm* pThis, int t )
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

static void evalStep( const _melodySeqAlgorithm* pThis, int t, int step, StepEval& out )
{
	const int16_t* v = pThis->v;
	_melodySeq_DTC* dtc = pThis->dtc;

	int eff = effectiveTrack( pThis, t );
	uint32_t seed = dtc->tracks[eff].activeSeed;
	const uint8_t* ep = dtc->tracks[eff].epoch;		// Evolve counters ride the follow chain

	// Shift (v1.1): rotate the whole pattern lookup, non-destructively -
	// the walk (and the playhead) stay put, the material slides under
	// them. The follower's own Shift filters a followed source, so
	// Follow + Shift = a canon. Applied here so every consumer - engine,
	// display, Tide - rotates identically.
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

	// The base melody: two octaves of the scale centred on the root, so
	// lines orbit the root rather than hanging above it. Rolled as a
	// fraction so the contour survives scale changes.
	uint32_t rb = hash3( seed, esP, 0xBA5Eu ) & 1023;
	int degree = (int)( ( rb * (uint32_t)( 2 * k ) ) >> 10 ) - k;

	// Note / Oct amounts ADD random deviation on top, non-destructively -
	// at 0 the pure base melody returns. Magnitude sets both how OFTEN a
	// note is varied and how FAR it may reach; sign sets the direction.
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

	// Weight: consonance gravity - snap this note to the nearest stable
	// degree (root / third / fifth), Weight% of the time
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
// Factory callbacks
// ---------------------------------------------------------------------------

void calculateRequirements( _NT_algorithmRequirements& req, const int32_t* specifications )
{
	req.numParameters = kNumParameters;
	req.sram = sizeof(_melodySeqAlgorithm);
	req.dram = 0;
	req.dtc = sizeof(_melodySeq_DTC);
	req.itc = 0;
}

_NT_algorithm* construct( const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications )
{
	_melodySeq_DTC* dtc = (_melodySeq_DTC*)ptrs.dtc;
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
			tr.epoch[s] = 0;
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
	dtc->intPeriod = NT_globals.sampleRate / 8;
	dtc->intCountdown = 1;
	dtc->clockPulse = 0;
	dtc->midiRunning = false;
	dtc->midiClockCount = 0;
	dtc->midiTickPending = false;
	dtc->midiSamplesSinceTick = 0;
	dtc->midiPeriod = 0;
	dtc->scaleCached = -1;
	dtc->scaleCount = 0;

	_melodySeqAlgorithm* alg = new (ptrs.sram) _melodySeqAlgorithm( dtc );
	alg->parameters = parameters;
	alg->parameterPages = &parameterPages;
	alg->sel = 0;
	for ( int i = 0; i < 3; ++i )
	{
		alg->potCaptured[i] = false;
		alg->potPrev[i] = 0.0f;
		alg->potQuiet[i] = 0;
	}
	for ( int t2 = 0; t2 < kNumTracks; ++t2 )
		for ( int i = 0; i < 3; ++i )
		{
			alg->potStash[t2][i] = 0;
			alg->potStashValid[t2][i] = false;
		}
	for ( int t = 0; t < kNumTracks; ++t )
		alg->solo[t] = false;
	alg->encLDown = 0; alg->encRDown = 0;
	alg->encLHeldPrev = false; alg->encRHeldPrev = false;
	alg->encLTurned = false; alg->encRTurned = false;
	alg->encLFired = false; alg->encRFired = false;
	alg->uiRng = 0x1D872B41u;
	alg->drawTick = 0;
	alg->lastTouch = 0;
	alg->saverActive = false;
	alg->selfWrite = false;
	for ( int t = 0; t < kNumTracks; ++t )
		alg->fishX[t] = 24.0f + 30.0f * t;
	for ( int b = 0; b < 4; ++b )
	{
		alg->bubX[b] = (int16_t)( 40 + 60 * b );
		alg->bubY[b] = (int16_t)( 12 + 14 * b );
	}
	return alg;
}

void parameterChanged( _NT_algorithm* self, int p )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	_melodySeq_DTC* dtc = pThis->dtc;

	// parameter edits postpone the screensaver - except our own UI writes
	// (their gestures already counted, and pot jitter can flicker a value)
	// and the performance parameters commonly driven by CV mappings
	// (Seed, Reseed all, Freeze), so a generative patch can still fall
	// asleep and show the shoal
	bool isSeed = p >= kTrackBase && p < kRoutingBase
			&& ( ( p - kTrackBase ) % kNumTrackParams ) == kTSeed;
	if ( !pThis->selfWrite && !isSeed && p != kGReseedAll && p != kGFreeze )
		pThis->lastTouch = dtc->uiClock;

	if ( p == kGScale )
	{
		dtc->scaleCached = -1;
		return;
	}
	if ( p == kGBPM )
	{
		int bpm = pThis->v[kGBPM];
		if ( bpm > 0 )
			dtc->intPeriod = ( NT_globals.sampleRate * 60 ) / (uint32_t)bpm;	// quarter notes: x1 = one step per beat
		return;
	}
	if ( p == kGReseedAll )
	{
		// Never touch other parameters from inside parameterChanged() - the
		// host may be mid-initialisation or mid-preset-load. Just note the
		// request; step() performs the scatter. Also ignore the initial
		// value (algorithm add / preset load) so saved seeds survive.
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
		// destination is global now (v1.2, moved off per-track for the
		// real-hardware parameter budget - see PROJECT-NOTES.md). Changing
		// it affects every track's already-open note, not just one, so
		// close all of them - each on the destination it was opened with,
		// rather than leaving any stuck until that track's next advance.
		for ( int t = 0; t < kNumTracks; ++t )
		{
			TrackState& tr = dtc->tracks[t];
			if ( tr.midiHeld )
			{
				NT_sendMidi3ByteMessage( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
				tr.midiHeld = false;
				tr.midiSounding = false;
			}
		}
		return;
	}
	if ( p >= kMidiBase && p < kMidiBase + kNumTracks * kNumMidiParams )
	{
		// channel is the only per-track MIDI param left (kNumMidiParams
		// == 1), so p - kMidiBase is directly the track index.
		int t = p - kMidiBase;
		TrackState& tr = dtc->tracks[t];
		if ( tr.midiHeld )
		{
			// channel changed while a note was sounding - let go of it
			// now, on the channel it was opened with, rather than leaving
			// it stuck until the track's next advance
			NT_sendMidi3ByteMessage( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
			tr.midiHeld = false;
			tr.midiSounding = false;
		}
		return;
	}
}

// ---------------------------------------------------------------------------
// Audio / sequencing
// ---------------------------------------------------------------------------

// Seeded Fisher-Yates permutation for Shuffle: every step exactly once per
// pass, in an order that changes each pass but is fully reproducible.
static void buildShuffle( TrackState& tr, int length, uint32_t seed, uint32_t pass )
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

static void advanceTrack( _melodySeqAlgorithm* pThis, int t, uint32_t stepPeriod, bool anySolo )
{
	const int16_t* v = pThis->v;
	_melodySeq_DTC* dtc = pThis->dtc;
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
			tr.pos = ( length - 1 ) / 2;	// Diverge's own phase-0 start, not 0 - see its case above
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
		// Hopscotch (Metropolix-inspired): an overlapping "two ahead,
		// one back" crawl - pairs (p, p+2) for p = 0..length-1, giving
		// a period of 2*Length rather than Length. tr.phase counts
		// steps within that doubled cycle (fits uint8_t up to Length
		// 64 -> period 128). Replaces the original seed-derived jump
		// version - tester feedback (25 Aug 2026) found the seeded
		// jump too abstract/unpredictable by ear; this reads as a
		// distinct, recognisable rhythmic hop instead, and nothing
		// else in the Direction list has this overlapping-repeat
		// texture.
		uint32_t period = (uint32_t)length * 2;
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % period );
		uint32_t n = tr.phase;
		uint32_t p = n / 2;
		tr.pos = (int)( ( n % 2 == 0 ) ? p : ( p + 2 ) % (uint32_t)length );
		break;
	}
	case kDirGravity:
	{
		// a seed-derived chance each step to snap back to step 1 instead
		// of continuing forward - the same "pull towards home" idea
		// Weight already applies to pitch (gravity towards the root),
		// applied here to the walk instead. Strength is fixed per seed
		// (20-60%); the roll varies with (pass, pos) so it isn't the
		// same outcome from the same position forever.
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
		// Metropolix-style fold: alternates from both ends of the
		// pattern inward toward the middle (0, length-1, 1, length-2,
		// ...). Fixed and self-contained - no Sample source needed.
		// Replaces the original "chase the Sample source's position"
		// version - tester feedback (25 Aug 2026): that read as one
		// track chasing another, not as the pattern folding in on
		// itself the name promises, and did nothing at all without a
		// source set.
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % length );
		uint32_t n = tr.phase;
		tr.pos = (int)( ( n % 2 == 0 ) ? ( n / 2 ) : ( (uint32_t)length - 1 - n / 2 ) );
		break;
	}
	case kDirDiverge:
	{
		// Metropolix-style unfold: the mirror of Converge above - starts
		// near the pattern's middle and alternates outward to both
		// ends. tr.pos = floor( (length-1)/2 + offset(phase) ), where
		// offset zigzags 0, +1, -1, +2, -2, ... - verified by a host
		// harness across Length 1..64 that this always visits every
		// step exactly once per pass, for both odd and even Length
		// (see PROJECT-NOTES for the derivation). The starting position
		// itself is seeded in the reset block above (this case's phase
		// 0 isn't 0, unlike every other mode) - see the kDirDiverge
		// special case there.
		tr.phase = (uint8_t)( ( tr.phase + 1 ) % length );
		uint32_t n = tr.phase;
		int k = (int)( ( n + 1 ) / 2 );
		int offset = ( n & 1 ) ? k : -k;
		tr.pos = ( ( length - 1 ) + 2 * offset ) / 2;
		break;
	}
	case kDirSkitter:
	{
		// "Quasi Random": like Random, but re-rolls rather than repeat
		// the same step twice in a row. Same generative RNG stream as
		// Random/Drunk/Pools (dtc->rng, not seed-derived) - Metropolix's
		// own manual groups Quasi Random alongside Random and Brownian
		// as "Generative", not a fixed/reproducible pattern, and this
		// matches that: no atOrigin below either, same as those three.
		// Bounded re-roll: expected iterations is length/(length-1),
		// negligible even for small Length; guarded against length<=1
		// where it would otherwise loop forever.
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
		// "Pedal Point": alternates step 1 with each other step in turn -
		// 1,2,1,3,1,4,1,5... - a drone/pedal note punctuated by melodic
		// excursions. Fixed, deterministic, period 2*(length-1); trivial
		// (always step 1) for length<=1, where that period is 0.
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
	// Converge/Diverge/Stride(Hopscotch): pos==0 can recur mid-pattern
	// (Diverge never even starts there; Hopscotch's doubled 2*Length
	// period passes through 0 twice), so origin means "start of this
	// mode's own cycle" (phase==0), not "position 0" - the default
	// below would land a reseed at the wrong point in the cycle.
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

	// Currents (v1.2): roll a fresh target every time this track advances -
	// fired or not, muted or not, the current isn't gated by Chance or
	// Mute, it just keeps flowing at the track's own pace. Purely a
	// function of (this track's OWN seed, this step, its evolve epoch), so
	// it's deterministic and repeats with the pass like the melody itself -
	// seed 347 always comes with the same current. Deliberately the
	// track's OWN activeSeed, never effectiveSeed()/effectiveTrack(): a
	// follower borrows its source's melody but generates its own current.
	// Same (seed, step | epoch<<8) keying evalStep already uses below, so
	// an Evolve re-roll on a step also refreshes that step's current.
	{
		uint32_t cs = (uint32_t)tr.pos | ( (uint32_t)tr.epoch[tr.pos] << 8 );
		tr.currentPrev = tr.currentVolts;
		tr.currentTarget = ( hash3( tr.activeSeed, cs, 0xC4E7u ) % 1001 ) * 0.01f;	// 0.00-10.00V
		tr.currentPeriod = stepPeriod;
		tr.currentElapsed = 0;
	}

	// End-of-sequence (v1.2): a pure "every Length advances" counter,
	// deliberately NOT the atOrigin flag used above for reseed/Evolve/
	// Breathe - atOrigin is unconditionally true on every single step
	// for Random, Drunk and Pools (they have no fixed loop shape), which
	// would make EOS pulse continuously on those three modes instead of
	// marking anything meaningful. Counting raw advances instead gives
	// every direction mode the same, evenly-spaced "every Length steps"
	// pulse - Oliver's call (24 Aug 2026) over suppressing EOS on the
	// three unordered modes. Unconditional like Currents above: fires
	// regardless of Mute/Chance, since it's about the sequence's
	// structure, not which steps happen to sound.
	tr.eosCount += 1;
	if ( tr.eosCount >= (uint32_t)length )
	{
		tr.eosCount = 0;
		uint32_t pw = stepPeriod / 2;
		tr.eosRemaining = pw ? pw : 1;
	}

	StepEval ev;
	evalStep( pThis, t, tr.pos, ev );

	bool muted = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );

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

	// MIDI note out (v1.2). A track is monophonic, so any note it was
	// already holding is closed first - covers a tie/legato retrigger and
	// a channel/destination that changed since the last note, with one
	// rule: never more than one held note per track. Channel 0 means MIDI
	// is off for this track. Gate-end and mute note-offs are handled where
	// the CV gate itself goes low, further down in step() - same signal,
	// so MIDI note timing always matches the audible CV gate exactly.
	if ( tr.midiHeld )
	{
		NT_sendMidi3ByteMessage( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
		tr.midiHeld = false;
	}
	int midiCh = v[ MP( t ) ];
	if ( midiCh > 0 )
	{
		int note = ev.note;
		if ( note < 0 ) note = 0;
		else if ( note > 127 ) note = 127;
		// v1.2: destination and velocity are global (shared by every
		// track), not per-track - see kGMidiVelocity/kGMidiDest.
		uint32_t dest = (uint32_t)v[ kGMidiDest ];
		uint8_t vel = (uint8_t)v[ kGMidiVelocity ];
		NT_sendMidi3ByteMessage( dest, (uint8_t)( 0x90 | ( midiCh - 1 ) ), (uint8_t)note, vel );
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
static void scheduleAdvance( _melodySeqAlgorithm* pThis, int t, uint32_t stepPeriod, bool anySolo )
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
		// Shift rotates the slop key with the note, so a shifted pattern
		// keeps each note's timing lean, not the grid position's
		nextPos = ( nextPos + v[ XP( t, kXShift ) ] ) % length;
		if ( nextPos < 0 )
			nextPos += length;
		// 32-bit-safe: no 64-bit division (the NT does not provide libgcc's
		// __aeabi_uldivmod to plug-ins). 64-bit multiply is a native umull.
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

// One sample of master-clock housekeeping, shared by the running and the
// frozen-with-clock paths: external edge detection (which keeps extPeriod
// fresh even while not running), the MIDI clock path (v1.2), or the
// internal countdown. Returns true on a tick sample and always yields the
// current step period. clockMode: 0 external, 1 internal, 2 MIDI - external
// with no clockIn bus assigned falls through to the internal free-run,
// same as before v1.2 added the third mode.
static bool masterClockTick( _melodySeq_DTC* dtc, const float* clockIn,
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
		period = dtc->extPeriod ? dtc->extPeriod : NT_globals.sampleRate / 4;
	}
	else if ( clockMode == 2 )
	{
		// midiRealtime() sets midiTickPending once every 24 clock bytes (24
		// PPQN - Shoal's x1 is one step per quarter note, so 24 clocks is
		// exactly one x1 tick). samplesSinceTick free-runs like
		// samplesSinceClock above, so the period estimate stays fresh
		// across a Stop/Continue rather than reporting a stale value.
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
		period = dtc->midiPeriod ? dtc->midiPeriod : NT_globals.sampleRate / 4;
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

// v1.2 MIDI clock in (Clock source = MIDI). Only 0xF8/0xFA/0xFB/0xFC are
// meaningful to a clock follower; everything else (Active Sensing, undefined
// bytes) is ignored. Only listens while MIDI is actually the selected clock
// source, so an incoming clock on an unrelated MIDI cable does nothing.
void midiRealtime( _NT_algorithm* self, uint8_t byte )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	_melodySeq_DTC* dtc = pThis->dtc;
	if ( pThis->v[kGClockSource] != 2 )
		return;
	switch ( byte )
	{
	case 0xF8:		// clock: 24 PPQN, and Shoal's x1 is one step per quarter
					// note, so 24 clocks is exactly one x1 tick
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

void step( _NT_algorithm* self, float* busFrames, int numFramesBy4 )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	_melodySeq_DTC* dtc = pThis->dtc;
	const int16_t* v = pThis->v;

	int numFrames = numFramesBy4 * 4;
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
		// deferred "reseed all": scatter seeds into the per-track Seed
		// parameters (so presets capture them) from the audio context,
		// where NT_setParameterFromAudio is documented as safe
		uint32_t base = (uint32_t)dtc->reseedAllPending;
		dtc->reseedAllPending = -1;
		int algIdx = NT_algorithmIndex( self );
		if ( algIdx >= 0 )
		{
			for ( int t = 0; t < kNumTracks; ++t )
			{
				int16_t s = (int16_t)( hash3( base, t, 0x5EEDu ) % 1000 );
				NT_setParameterFromAudio( (uint32_t)algIdx, TP( t, kTSeed ) + NT_parameterOffset(), s );
			}
		}
	}

	const float* clockIn = ( v[kGClockIn] > 0 )
			? busFrames + ( v[kGClockIn] - 1 ) * numFrames : NULL;
	const float* resetIn = ( v[kGResetIn] > 0 )
			? busFrames + ( v[kGResetIn] - 1 ) * numFrames : NULL;
	const float* reseedIn = ( v[kGReseedIn] > 0 )
			? busFrames + ( v[kGReseedIn] - 1 ) * numFrames : NULL;
	float* clkOut = ( v[kGClockOut] > 0 )
			? busFrames + ( v[kGClockOut] - 1 ) * numFrames : NULL;
	bool clkRep = v[kGClockOutMode];

	// Reseed trigger input (v1.1): each rising edge is the reseed-all
	// gesture - a fresh random base written through the Reseed all
	// parameter (from the audio context, where that is documented safe), so
	// the mechanism stays single-pathed and the base lands in presets.
	// Scanned before the Freeze branch: a trigger during a freeze still
	// arms, and the reseed lands at each loop origin after release.
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
				int algIdx = NT_algorithmIndex( self );
				if ( algIdx >= 0 )
					NT_setParameterFromAudio( (uint32_t)algIdx,
							kGReseedAll + NT_parameterOffset(), base );
			}
			else if ( dtc->reseedInHigh && r < 0.1f )
				dtc->reseedInHigh = false;
		}
	}

	float* pitchOut[kNumTracks];
	float* gateOut[kNumTracks];
	float* currentOut[kNumTracks];	// bus-only, always Replace - see CP()
	float* eosOut[kNumTracks];		// bus-only, always Replace - see EP()
	bool pitchRep[kNumTracks], gateRep[kNumTracks];
	bool mutedNow[kNumTracks];
	bool anySolo = false;
	for ( int t = 0; t < kNumTracks; ++t )
		if ( pThis->solo[t] ) { anySolo = true; break; }
	for ( int t = 0; t < kNumTracks; ++t )
	{
		int gb = v[ RP( t, 0 ) ], pb = v[ RP( t, 2 ) ];
		gateOut[t] = gb > 0 ? busFrames + ( gb - 1 ) * numFrames : NULL;
		gateRep[t] = v[ RP( t, 1 ) ];
		pitchOut[t] = pb > 0 ? busFrames + ( pb - 1 ) * numFrames : NULL;
		pitchRep[t] = v[ RP( t, 3 ) ];
		int cb = v[ CP( t ) ];
		currentOut[t] = cb > 0 ? busFrames + ( cb - 1 ) * numFrames : NULL;
		int eb = v[ EP( t ) ];
		eosOut[t] = eb > 0 ? busFrames + ( eb - 1 ) * numFrames : NULL;
		mutedNow[t] = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );
	}

	int clockMode = v[kGClockSource];		// 0 external, 1 internal, 2 MIDI
	// v1.2: in MIDI mode, running also requires a Start/Continue with no
	// Stop since - the DAW's transport gates the sequence, same as the
	// Run switch does for the other two clock sources.
	bool run = v[kGRun] && ( clockMode != 2 || dtc->midiRunning );

	// v1.1 output voltage scaling, cached per block. Defaults reproduce the
	// v1.0.0 hard-coded behaviour exactly: 5V gates, 1V/oct, no offset.
	float gLevel[kNumTracks], pScaleF[kNumTracks], pOffF[kNumTracks];
	for ( int t = 0; t < kNumTracks; ++t )
	{
		gLevel[t] = (float)v[ XP( t, kXGateVolts ) ];
		pScaleF[t] = (float)v[ XP( t, kXPitchScale ) ] * 0.01f;
		pOffF[t] = (float)v[ XP( t, kXPitchOffset ) ] * 0.1f;
	}

	// Freeze: the shoal holds its breath - nothing advances, every sounding
	// track's gate is held high, the current notes hang as a chord. What
	// Clock out does meanwhile is the player's choice (v1.1): Stops silences
	// it with everything else; Runs (the default) keeps the master clock -
	// grid, pulses, external-period tracking - alive so delays and anything
	// else riding Shoal's clock stay in time while the melody holds still.
	if ( v[kGFreeze] )
	{
		bool clockRuns = v[kGFreezeClock];
		if ( !clockRuns )
		{
			dtc->clockPulse = 0;		// the clock holds its breath too
			if ( clkOut && clkRep )
				for ( int i = 0; i < numFrames; ++i )
					clkOut[i] = 0.0f;
		}
		// EOS (v1.2): nothing advances during a Freeze, so no EOS pulse
		// should be mid-flight either - cut it short rather than pausing
		// it, so unfreezing always starts from silence, never resuming a
		// stale pulse. (Cheap and idempotent to repeat every block.)
		for ( int t = 0; t < kNumTracks; ++t )
			dtc->tracks[t].eosRemaining = 0;
		for ( int i = 0; i < numFrames; ++i )
		{
			if ( clockRuns )
			{
				uint32_t period;
				if ( masterClockTick( dtc, clockIn, clockMode, run, i, period ) )
				{
					// the grid advances too, so divided tracks resume in
					// phase with the world that kept clocking; what freeze
					// suspends is only the fan-out to the tracks
					dtc->tickCount += 1;
					dtc->clockPulse = period / 2;
				}
				if ( clkOut )
				{
					float cv = dtc->clockPulse ? 5.0f : 0.0f;
					if ( clkRep ) clkOut[i] = cv;
					else clkOut[i] += cv;
				}
				if ( dtc->clockPulse )
					dtc->clockPulse -= 1;
			}
			for ( int t = 0; t < kNumTracks; ++t )
			{
				TrackState& tr = dtc->tracks[t];
				float gate = ( tr.pos >= 0 && !mutedNow[t] ) ? gLevel[t] : 0.0f;
				// MIDI note-off (v1.2): fires on the exact same signal that
				// silences the CV gate, so a mute engaged mid-freeze (or the
				// chord's gate otherwise dropping) closes the MIDI note too.
				bool midiSoundingNow = gate > 0.0f;
				if ( tr.midiSounding && !midiSoundingNow && tr.midiHeld )
				{
					NT_sendMidi3ByteMessage( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
					tr.midiHeld = false;
				}
				tr.midiSounding = midiSoundingNow;
				if ( pitchOut[t] )
				{
					float pv = tr.pitchVolts * pScaleF[t] + pOffF[t];
					if ( pitchRep[t] ) pitchOut[t][i] = pv;
					else pitchOut[t][i] += pv;
				}
				if ( gateOut[t] )
				{
					if ( gateRep[t] ) gateOut[t][i] = gate;
					else gateOut[t][i] += gate;
				}
				// Currents (v1.2) hold their last value through a Freeze,
				// same as the melody - nothing advances, so no fresh
				// target is rolled here, just the held value re-output.
				// Always Replace (no mode param - see CP()).
				if ( currentOut[t] )
					currentOut[t][i] = tr.currentVolts;
				// EOS (v1.2): silent throughout a Freeze - see the
				// eosRemaining = 0 above. Always Replace (no mode param).
				if ( eosOut[t] )
					eosOut[t][i] = 0.0f;
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
					tr.eosCount = 0;	// EOS's "every Length steps" cycle realigns to the reset too
				}

				// Bresenham/Euclidean scheduling: on average exactly num
				// advances per den master ticks, phase-locked to the
				// global grid (a pure function of tickCount, never
				// accumulated, so rate changes never drift). Unifies what
				// used to be two separate branches - slow rates skipped
				// ticks, fast rates queued extra sub-steps within one
				// tick - into one formula that also covers fractional
				// rates: 1.5x (num=3,den=2) naturally alternates
				// 1,2,1,2... advances/tick; /1.5 (num=2,den=3) naturally
				// gives 0,1,1,0,1,1... - both verified against the old
				// two-branch logic across all 21 existing rate table
				// entries before this shipped. tickCount*num stays well
				// inside uint32 for any realistic session (lesson 1: no
				// 64-bit division).
				uint32_t before = ( dtc->tickCount * num ) / den;
				uint32_t after = ( ( dtc->tickCount + 1 ) * num ) / den;
				uint32_t count = after - before;
				uint32_t stepPeriod = ( period * den ) / num;	// constant average spacing - correct for gate-length math even though individual ticks fire unevenly

				if ( dtc->resetPrime )
				{
					// reset is always a grid origin, regardless of this
					// tick's natural cadence - land tight, then resume the
					// normal schedule from here
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

		// Clock out: 5V, high for half the master (x1) period, so other
		// algorithms and external gear can ride Shoal's grid
		if ( tick )
			dtc->clockPulse = period / 2;
		if ( clkOut )
		{
			float cv = dtc->clockPulse ? 5.0f : 0.0f;
			if ( clkRep ) clkOut[i] = cv;
			else clkOut[i] += cv;
		}
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

			// MIDI note-off (v1.2): fires on the exact same signal that
			// silences the CV gate - natural gate end and mute both land
			// here, so MIDI note timing always matches the audible gate.
			bool midiSoundingNow = gate > 0.0f;
			if ( tr.midiSounding && !midiSoundingNow && tr.midiHeld )
			{
				NT_sendMidi3ByteMessage( tr.midiHeldDest, (uint8_t)( 0x80 | ( tr.midiHeldChan - 1 ) ), tr.midiHeldNote, 0 );
				tr.midiHeld = false;
			}
			tr.midiSounding = midiSoundingNow;

			if ( pitchOut[t] )
			{
				float pv = tr.pitchVolts * pScaleF[t] + pOffF[t];
				if ( pitchRep[t] )
					pitchOut[t][i] = pv;
				else
					pitchOut[t][i] += pv;
			}
			if ( gateOut[t] )
			{
				if ( gateRep[t] )
					gateOut[t][i] = gate;
				else
					gateOut[t][i] += gate;
			}

			// Currents (v1.2): smoothstep from currentPrev towards
			// currentTarget over currentPeriod samples. Plain multiply/add
			// only (no cosf/libm call to verify on this bare-metal target -
			// same caution as lesson 1 for 64-bit division) - t*t*(3-2t)
			// gives the same eased S-curve shape a cosine crossfade would.
			if ( tr.currentElapsed < tr.currentPeriod )
				tr.currentElapsed += 1;
			float ct = ( tr.currentPeriod > 0 )
					? (float)tr.currentElapsed / (float)tr.currentPeriod : 1.0f;
			float ease = ct * ct * ( 3.0f - 2.0f * ct );
			tr.currentVolts = tr.currentPrev + ( tr.currentTarget - tr.currentPrev ) * ease;
			// Always Replace (no mode param - see CP()).
			if ( currentOut[t] )
				currentOut[t][i] = tr.currentVolts;

			// End-of-sequence (v1.2): a short 5V trigger, counted down the
			// same way gateRemaining/clockPulse already are elsewhere in
			// this file. Width is fixed (set in advanceTrack when the
			// pulse starts, half the step period at the time), not tied
			// to Gate length - EOS marks the sequence's structure, not a
			// note. Always Replace (no mode param - see EP()).
			bool eosHigh = tr.eosRemaining > 0;
			if ( eosHigh )
				tr.eosRemaining -= 1;
			if ( eosOut[t] )
				eosOut[t][i] = eosHigh ? 5.0f : 0.0f;
		}
	}
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

static void fireEncHolds( _melodySeqAlgorithm* pThis );
static uint32_t nextRand( _melodySeqAlgorithm* pThis );

// An 11x5 pixel fish, two frames of tail flick. Bit 0 = leftmost column.
static const uint16_t fishFrames[2][5] = {
	{
		(1<<2)|(1<<3)|(1<<4)|(1<<9),
		(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<8)|(1<<9),
		0x1FF,
		(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<8)|(1<<9),
		(1<<2)|(1<<3)|(1<<4)|(1<<9),
	},
	{
		(1<<2)|(1<<3)|(1<<4),
		(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<9),
		0x1FF|(1<<10),
		(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<9),
		(1<<2)|(1<<3)|(1<<4),
	},
};

static void drawFish( int x0, int y0, int frame, int colour )
{
	const uint16_t* rows = fishFrames[frame];
	for ( int r = 0; r < 5; ++r )
		for ( int c = 0; c < 11; ++c )
			if ( rows[r] & ( 1 << c ) )
				NT_drawShapeI( kNT_point, x0 + c, y0 + r, x0 + c, y0 + r, colour );
	NT_drawShapeI( kNT_point, x0 + 2, y0 + 1, x0 + 2, y0 + 1, 1 );	// eye
}

// a small hand-rolled sine for the screensaver's swaying and wobbling
static const int8_t swayTab[16] = { 0, 1, 2, 2, 2, 1, 1, 0, 0, -1, -2, -2, -2, -1, -1, 0 };

// The screensaver IS the shoal: one fish per track swimming the open sea,
// darting forward whenever its track sounds a note. Muted tracks drift dim
// and slow; a breathing track fades; Freeze stills the whole scene while
// the bubbles keep rising. Any touch wakes the normal display.
static void drawSaver( _melodySeqAlgorithm* pThis )
{
	_melodySeq_DTC* dtc = pThis->dtc;
	const int16_t* v = pThis->v;
	uint32_t tick = pThis->drawTick;
	bool frozen = v[kGFreeze];

	bool anySolo = false;
	for ( int t = 0; t < kNumTracks; ++t )
		if ( pThis->solo[t] ) { anySolo = true; break; }

	// seabed: an uneven line of sediment
	for ( int x = 0; x < 256; x += 5 )
	{
		int h = (int)( hash3( (uint32_t)x, 0, 0x0BEDu ) % 3 );
		NT_drawShapeI( kNT_point, x, 63 - h, x, 63 - h, 3 );
	}

	// seaweed, swaying from the bed
	static const uint8_t weedX[3] = { 34, 138, 214 };
	for ( int w = 0; w < 3; ++w )
		for ( int seg = 0; seg < 5; ++seg )
		{
			int dx = ( swayTab[ ( ( tick >> 3 ) + w * 5 + seg * 2 ) & 15 ] * ( seg + 1 ) ) / 4;
			int y = 61 - seg * 3;
			NT_drawShapeI( kNT_point, weedX[w] + dx, y, weedX[w] + dx, y - 1, 4 );
		}

	// bubbles rise even while the shoal is frozen - the sea stays alive
	for ( int b = 0; b < 4; ++b )
	{
		if ( ( tick & 1 ) == 0 )
			pThis->bubY[b] -= 1;
		if ( pThis->bubY[b] < 2 )
		{
			pThis->bubY[b] = (int16_t)( 58 + ( nextRand( pThis ) % 5 ) );
			pThis->bubX[b] = (int16_t)( 8 + ( nextRand( pThis ) % 240 ) );
		}
		int bx = pThis->bubX[b] + ( swayTab[ ( ( tick >> 2 ) + b * 4 ) & 15 ] >> 1 );
		NT_drawShapeI( kNT_point, bx, pThis->bubY[b], bx, pThis->bubY[b], 6 );
	}

	// the shoal: track order top to bottom, each fish in its own water
	for ( int t = 0; t < kNumTracks; ++t )
	{
		TrackState& tr = dtc->tracks[t];
		bool muted = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );
		bool sounding = tr.gateRemaining > 0 && !muted;

		float speed;
		if ( frozen )
			speed = 0.0f;
		else
		{
			speed = 0.28f + 0.045f * (float)( ( t * 5 ) % 5 );	// no two fish alike
			if ( muted )
				speed *= 0.4f;
			else if ( sounding )
				speed += 1.1f;									// the dart
		}
		pThis->fishX[t] -= speed;
		if ( pThis->fishX[t] < -12.0f )
			pThis->fishX[t] = 258.0f + (float)( nextRand( pThis ) % 48 );

		int y = 2 + t * 7 + swayTab[ ( ( tick >> 2 ) + t * 3 ) & 15 ];
		int colour = muted ? 3 : ( tr.resting ? 4 : ( sounding ? 15 : 9 ) );
		int frame = frozen ? 0 : (int)( ( ( tick >> 3 ) + t ) & 1 );
		drawFish( (int)pThis->fishX[t], y, frame, colour );
	}
}

bool draw( _NT_algorithm* self )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	_melodySeq_DTC* dtc = pThis->dtc;
	const int16_t* v = pThis->v;
	char buff[16];

	pThis->drawTick += 1;
	fireEncHolds( pThis );		// holds must fire even while controls are motionless

	// screensaver: engage after the idle time, until any touch
	if ( v[kGSaver] > 0 )
	{
		uint32_t limit = NT_globals.sampleRate * ( v[kGSaver] == 1 ? 60u : 300u );
		if ( dtc->uiClock - pThis->lastTouch >= limit )
		{
			pThis->saverActive = true;
			drawSaver( pThis );
			return true;
		}
	}
	pThis->saverActive = false;

	bool anySolo = false;
	for ( int t = 0; t < kNumTracks; ++t )
		if ( pThis->solo[t] ) { anySolo = true; break; }

	for ( int t = 0; t < kNumTracks; ++t )
	{
		TrackState& tr = dtc->tracks[t];
		int y = 1 + t * 7;
		bool selT = ( t == pThis->sel );
		bool muted = v[ TP( t, kTMute ) ] || ( anySolo && !pThis->solo[t] );
		int length = v[ TP( t, kTLength ) ];
		int base = muted ? 4 : ( tr.resting ? 5 : ( selT ? 13 : 9 ) );	// breathing rows dim

		if ( selT )
			NT_drawShapeI( kNT_rectangle, 0, y, 1, y + 5, 15 );
		NT_intToString( buff, t + 1 );
		NT_drawText( 4, y + 5, buff, selT ? 15 : ( muted ? 4 : 8 ), kNT_textLeft, kNT_textTiny );

		// adaptive density: longer loops draw with narrower cells, so the
		// whole pattern is always visible - no scrolling
		int cw, bw, cells;
		if ( length <= 16 )      { cw = 9; bw = 7; cells = 16; }
		else if ( length <= 32 ) { cw = 4; bw = 3; cells = length; }
		else                     { cw = 2; bw = 2; cells = length; }

		for ( int s = 0; s < cells; ++s )
		{
			int x = 10 + s * cw;
			if ( s >= length )
			{
				NT_drawShapeI( kNT_point, x + 3, y + 3, x + 3, y + 3, 2 );
				continue;
			}
			bool isPos = ( s == tr.pos );
			if ( isPos )
				NT_drawShapeI( kNT_rectangle, x, y, x + bw - 1, y + 5, 5 );
			StepEval ev;
			evalStep( pThis, t, s, ev );
			if ( !ev.fires )
				NT_drawShapeI( kNT_rectangle, x, y + 4, x + bw - 1, y + 4, muted ? 2 : 3 );
			else
				NT_drawShapeI( kNT_rectangle, x, y + 5 - ev.barH, x + bw - 1, y + 5, isPos ? 15 : base );
		}

		NT_drawText( 156, y + 5, enumStringsRate[ v[ TP( t, kTRate ) ] ], selT ? 12 : 6, kNT_textLeft, kNT_textTiny );
		int src = v[ TP( t, kTSource ) ];
		if ( src > 0 && src - 1 != t )
		{
			buff[0] = '<';
			int n = 1 + NT_intToString( buff + 1, src );
			buff[n] = 0;
			NT_drawText( 172, y + 5, buff, 7, kNT_textLeft, kNT_textTiny );
		}
		if ( pThis->solo[t] )
			NT_drawText( 186, y + 5, "S", 15, kNT_textLeft, kNT_textTiny );
		else if ( muted )
			NT_drawText( 186, y + 5, "M", 12, kNT_textLeft, kNT_textTiny );
	}

	NT_drawShapeI( kNT_line, 190, 0, 190, 56, 3 );

	// selected-track detail panel
	int t = pThis->sel;
	const int P = 194;
	buff[0] = 'T';
	int n = 1 + NT_intToString( buff + 1, t + 1 );
	buff[n] = 0;
	NT_drawText( P, 10, buff, 15, kNT_textLeft, kNT_textNormal );

	// pot params bright; the dim column holds live performance state -
	// direction, seed, evolve (GATE/TIE/SLOP live on the parameter page)
	static char const * const dirShort[9] = {
		"FWD", "REV", "PND", "RND", "DRK", "PNG", "TID", "SHF", "POL",
	};
	NT_drawText( P, 19, "CH", 14, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTChance ) ] );
	NT_drawText( P + 10, 19, buff, 14, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 30, 19, "DIR", 8, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 44, 19, dirShort[ v[ TP( t, kTDirection ) ] ], 8, kNT_textLeft, kNT_textTiny );

	NT_drawText( P, 26, "NT", 14, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTNote ) ] );
	NT_drawText( P + 10, 26, buff, 14, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 30, 26, "SD", 8, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTSeed ) ] );
	NT_drawText( P + 40, 26, buff, 8, kNT_textLeft, kNT_textTiny );

	NT_drawText( P, 33, "OC", 14, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTOct ) ] );
	NT_drawText( P + 10, 33, buff, 14, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 30, 33, "EV", 8, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTEvolve ) ] );
	NT_drawText( P + 40, 33, buff, 8, kNT_textLeft, kNT_textTiny );

	NT_drawText( P, 40, "RATE", 9, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 20, 40, enumStringsRate[ v[ TP( t, kTRate ) ] ], 9, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 36, 40, "LEN", 9, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTLength ) ] );
	NT_drawText( P + 52, 40, buff, 9, kNT_textLeft, kNT_textTiny );

	NT_drawText( P, 47, "OCT", 9, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTOctave ) ] );
	NT_drawText( P + 14, 47, buff, 9, kNT_textLeft, kNT_textTiny );
	NT_drawText( P + 26, 47, "TR", 9, kNT_textLeft, kNT_textTiny );
	NT_intToString( buff, v[ TP( t, kTTrans ) ] );
	NT_drawText( P + 36, 47, buff, 9, kNT_textLeft, kNT_textTiny );
	int selSrc = v[ TP( t, kTSource ) ];
	if ( selSrc > 0 && selSrc - 1 != t )
	{
		buff[0] = '<';
		n = 1 + NT_intToString( buff + 1, selSrc );
		buff[n] = 0;
		NT_drawText( P + 48, 47, buff, 9, kNT_textLeft, kNT_textTiny );
	}

	if ( dtc->tracks[t].pendingSeed >= 0 )
	{
		if ( ( pThis->drawTick >> 2 ) & 1 )
			NT_drawText( P, 55, "RESEED ARM", 15, kNT_textLeft, kNT_textTiny );
	}
	else
	{
		// the global key, quietly, while nothing more urgent needs the corner
		static char const * const noteNames[12] = {
			"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
		};
		static char const * const scaleShort[13] = {
			"CHROM", "MAJ", "NMIN", "HMIN", "DOR", "PHRYG", "LYD",
			"MIXO", "PENT+", "PENT-", "BLUES", "HIRA", "INSEN",
		};
		const char* rn = noteNames[ v[kGRoot] % 12 ];
		const char* sn = scaleShort[ v[kGScale] ];
		int m = 0;
		while ( *rn ) buff[m++] = *rn++;
		buff[m++] = ' ';
		while ( *sn ) buff[m++] = *sn++;
		buff[m] = 0;
		NT_drawText( P, 55, buff, 7, kNT_textLeft, kNT_textTiny );
	}
	if ( v[kGFreeze] )
		NT_drawText( P + 44, 55, "FRZ", 15, kNT_textLeft, kNT_textTiny );

	// the shoal's mascot and wordmark, top right - tail flicks faster while
	// a reseed is armed, and stops entirely while frozen
	NT_drawText( 220, 7, "SHOAL", 7, kNT_textLeft, kNT_textTiny );
	int flickShift = ( dtc->tracks[t].pendingSeed >= 0 ) ? 2 : 4;
	int fishFrame = v[kGFreeze] ? 0 : (int)( ( pThis->drawTick >> flickShift ) & 1 );
	drawFish( 244, 2, fishFrame, 10 );

	// bottom bar: live pot assignments
	NT_drawShapeI( kNT_line, 0, 57, 255, 57, 4 );
	// pot labels: bright while away from home, dim when the pot sits at
	// its default (push a pot to toggle home <-> dialled)
	NT_drawText( 6, 63, "P1", 5, kNT_textLeft, kNT_textTiny );
	NT_drawText( 18, 63, "CHANCE", v[ TP( t, kTChance ) ] != 100 ? 15 : 5, kNT_textLeft, kNT_textTiny );
	NT_drawText( 96, 63, "P2", 5, kNT_textLeft, kNT_textTiny );
	NT_drawText( 108, 63, "NOTE", v[ TP( t, kTNote ) ] != 0 ? 15 : 5, kNT_textLeft, kNT_textTiny );
	NT_drawText( 176, 63, "P3", 5, kNT_textLeft, kNT_textTiny );
	NT_drawText( 188, 63, "OCT", v[ TP( t, kTOct ) ] != 0 ? 15 : 5, kNT_textLeft, kNT_textTiny );

	return true;
}

// ---------------------------------------------------------------------------
// Custom UI
// ---------------------------------------------------------------------------

// The three pots own CHANCE / NOTE / OCT. Pushing a pot toggles its
// parameter between "home" (the default) and the last dialled value.
static const uint8_t potParam[3] = { kTChance, kTNote, kTOct };
static const int16_t potHome[3]  = { 100, 0, 0 };

static void potRange( int pot, int& mn, int& mx )
{
	if ( pot == 0 ) { mn = 0; mx = 100; }					// chance
	else { mn = -100; mx = 100; }							// note / oct (bipolar, centre = 0)
}

uint32_t hasCustomUi( _NT_algorithm* self )
{
	return kNT_potL | kNT_potC | kNT_potR
		 | kNT_potButtonL | kNT_potButtonC | kNT_potButtonR
		 | kNT_encoderL | kNT_encoderR
		 | kNT_encoderButtonL | kNT_encoderButtonR;
}

static void setTrackParam( _melodySeqAlgorithm* pThis, int t, int off, int value )
{
	int algIdx = NT_algorithmIndex( pThis );
	if ( algIdx < 0 )
		return;
	pThis->selfWrite = true;
	NT_setParameterFromUi( (uint32_t)algIdx, TP( t, off ) + NT_parameterOffset(), (int16_t)value );
	pThis->selfWrite = false;
}

static uint32_t nextRand( _melodySeqAlgorithm* pThis )
{
	pThis->uiRng = pThis->uiRng * 1664525u + 1013904223u;
	return pThis->uiRng >> 16;
}

// Encoder holds fire on real elapsed time (the audio clock), while still
// held. Checked from draw() as well as customUi(), because customUi() is
// not called while the controls are motionless.
static void fireEncHolds( _melodySeqAlgorithm* pThis )
{
	uint32_t hold = NT_globals.sampleRate / 2;		// ~0.5 s
	uint32_t now = pThis->dtc->uiClock;

	if ( pThis->encLHeldPrev && !pThis->encLTurned && !pThis->encLFired
			&& now - pThis->encLDown >= hold )
	{
		pThis->encLFired = true;
		pThis->solo[ pThis->sel ] = !pThis->solo[ pThis->sel ];
	}
	if ( pThis->encRHeldPrev && !pThis->encRTurned && !pThis->encRFired
			&& now - pThis->encRDown >= hold )
	{
		pThis->encRFired = true;
		int algIdx = NT_algorithmIndex( pThis );
		if ( algIdx >= 0 )
			NT_setParameterFromUi( (uint32_t)algIdx, kGReseedAll + NT_parameterOffset(),
								   (int16_t)( nextRand( pThis ) % 1000 ) );
	}
}

void customUi( _NT_algorithm* self, const _NT_uiData& data )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	const int16_t* v = pThis->v;
	int t = pThis->sel;

	// A "touch" must be meaningful: real hardware pots jitter by a count
	// or two at rest, and the OS reports that as control activity - which
	// silently kept resetting the idle timer, so the screensaver never
	// engaged. Buttons and encoder clicks always count; a pot counts only
	// once it has moved visibly. (While asleep, potPrev is frozen as the
	// anchor, so even a slow deliberate turn accumulates into a wake.)
	static const uint16_t wakeButtons =
		  kNT_encoderButtonL | kNT_encoderButtonR
		| kNT_potButtonL | kNT_potButtonC | kNT_potButtonR;
	bool touched = data.encoders[0] != 0 || data.encoders[1] != 0
		|| ( data.controls & wakeButtons ) || ( data.lastButtons & wakeButtons );
	for ( int i = 0; i < 3; ++i )
	{
		float d = data.pots[i] - pThis->potPrev[i];
		if ( d > 0.01f || d < -0.01f )
			touched = true;
	}

	if ( pThis->saverActive )
	{
		if ( !touched )
			return;						// jitter, not a touch: stay asleep
		// wake, and swallow the waking touch - you can't reseed a track
		// by accident just waking the screen
		pThis->saverActive = false;
		pThis->lastTouch = pThis->dtc->uiClock;
		for ( int i = 0; i < 3; ++i )
		{
			pThis->potCaptured[i] = false;
			pThis->potPrev[i] = data.pots[i];
		}
		return;
	}
	if ( touched )
		pThis->lastTouch = pThis->dtc->uiClock;

	// ---- left encoder: track select / length / mute / solo ----
	bool lHeld = data.controls & kNT_encoderButtonL;
	if ( lHeld && !pThis->encLHeldPrev )
	{
		pThis->encLDown = pThis->dtc->uiClock;
		pThis->encLTurned = false;
		pThis->encLFired = false;
	}
	if ( data.encoders[0] )
	{
		if ( lHeld )
		{
			int len = v[ TP( t, kTLength ) ] + data.encoders[0];
			if ( len < 1 ) len = 1;
			if ( len > kMaxSteps ) len = kMaxSteps;
			setTrackParam( pThis, t, kTLength, len );
			pThis->encLTurned = true;
		}
		else
		{
			int s = pThis->sel + data.encoders[0];
			if ( s < 0 ) s = 0;
			if ( s >= kNumTracks ) s = kNumTracks - 1;
			if ( s != pThis->sel )
			{
				pThis->sel = s;
				for ( int i = 0; i < 3; ++i )
					pThis->potCaptured[i] = false;
			}
		}
	}
	if ( !lHeld && pThis->encLHeldPrev && !pThis->encLTurned && !pThis->encLFired )
		setTrackParam( pThis, t, kTMute, !v[ TP( t, kTMute ) ] );		// tap = mute
	pThis->encLHeldPrev = lHeld;

	// ---- right encoder: rate / gate / reseed ----
	bool rHeld = data.controls & kNT_encoderButtonR;
	if ( rHeld && !pThis->encRHeldPrev )
	{
		pThis->encRDown = pThis->dtc->uiClock;
		pThis->encRTurned = false;
		pThis->encRFired = false;
	}
	if ( data.encoders[1] )
	{
		if ( rHeld )
		{
			// turning while pressed cancels the reseed gesture (GATE lives
			// on Pot C's push now)
			pThis->encRTurned = true;
		}
		else
		{
			int r = v[ TP( t, kTRate ) ] + data.encoders[1];
			if ( r < 0 ) r = 0;
			if ( r > kNumRates - 1 ) r = kNumRates - 1;
			setTrackParam( pThis, t, kTRate, r );
		}
	}
	if ( !rHeld && pThis->encRHeldPrev && !pThis->encRTurned && !pThis->encRFired )
		setTrackParam( pThis, t, kTSeed, nextRand( pThis ) % 1000 );		// tap = reseed track
	pThis->encRHeldPrev = rHeld;

	fireEncHolds( pThis );

	// ---- pot pushes: toggle home <-> dialled value ----
	// Pressing a pot physically nudges its ADC by a few percent, and after
	// a punch-back the pot sits exactly at the restored value - so takeover
	// captures instantly and the click's wobble would write straight
	// through. Each press or release therefore starts a short quiet period
	// during which the pot's value is ignored.
	uint32_t quietUntil = pThis->dtc->uiClock + NT_globals.sampleRate / 5;	// ~200 ms
	static const uint16_t potBtn[3] = { kNT_potButtonL, kNT_potButtonC, kNT_potButtonR };
	static const uint16_t potBit[3] = { kNT_potL, kNT_potC, kNT_potR };
	for ( int i = 0; i < 3; ++i )
	{
		if ( ( data.controls & potBtn[i] ) && !( data.lastButtons & potBtn[i] ) )
		{
			int16_t cur = v[ TP( t, potParam[i] ) ];
			if ( cur != potHome[i] )
			{
				pThis->potStash[t][i] = cur;			// punch home, remember where we were
				pThis->potStashValid[t][i] = true;
				setTrackParam( pThis, t, potParam[i], potHome[i] );
			}
			else if ( pThis->potStashValid[t][i] )
				setTrackParam( pThis, t, potParam[i], pThis->potStash[t][i] );	// punch back
			pThis->potCaptured[i] = false;
			pThis->potQuiet[i] = quietUntil;
		}
		else if ( !( data.controls & potBtn[i] ) && ( data.lastButtons & potBtn[i] ) )
			pThis->potQuiet[i] = quietUntil;			// release wobbles too
	}

	// ---- pots, with soft takeover ----
	for ( int i = 0; i < 3; ++i )
	{
		float pot = data.pots[i];
		bool quiet = ( data.controls & potBtn[i] )
			|| (int32_t)( pThis->potQuiet[i] - pThis->dtc->uiClock ) > 0;
		if ( quiet )
		{
			pThis->potPrev[i] = pot;					// track it, act on none of it
			continue;
		}
		if ( data.controls & potBit[i] )
		{
			int off = potParam[i];
			int mn, mx;
			potRange( i, mn, mx );
			float target = ( v[ TP( t, off ) ] - mn ) / (float)( mx - mn );
			if ( !pThis->potCaptured[i] )
			{
				float prev = pThis->potPrev[i];
				bool crossed = ( prev <= target && pot >= target )
							|| ( prev >= target && pot <= target );
				float dist = pot - target;
				if ( crossed || ( dist > -0.03f && dist < 0.03f ) )
					pThis->potCaptured[i] = true;
			}
			if ( pThis->potCaptured[i] )
			{
				int value = mn + (int)( pot * ( mx - mn ) + 0.5f );
				if ( value < mn ) value = mn;
				if ( value > mx ) value = mx;
				setTrackParam( pThis, t, off, value );
			}
		}
		pThis->potPrev[i] = pot;
	}
}

void setupUi( _NT_algorithm* self, _NT_float3& pots )
{
	_melodySeqAlgorithm* pThis = (_melodySeqAlgorithm*)self;
	const int16_t* v = pThis->v;
	int t = pThis->sel;
	pThis->lastTouch = pThis->dtc->uiClock;		// entering the UI is a touch
	pThis->saverActive = false;
	for ( int i = 0; i < 3; ++i )
	{
		int off = potParam[i];
		int mn, mx;
		potRange( i, mn, mx );
		float norm = ( v[ TP( t, off ) ] - mn ) / (float)( mx - mn );
		pots[i] = norm;
		pThis->potPrev[i] = norm;
		pThis->potCaptured[i] = true;
	}
}

// ---------------------------------------------------------------------------
// Misc callbacks
// ---------------------------------------------------------------------------

int parameterUiPrefix( _NT_algorithm* self, int p, char* buff )
{
	int t = -1;
	if ( p >= kTrackBase && p < kRoutingBase )
		t = ( p - kTrackBase ) / kNumTrackParams;
	else if ( p >= kRoutingBase && p < kRoutingBase + kNumTracks * kNumRoutingParams )
		t = ( p - kRoutingBase ) / kNumRoutingParams;		// Clock out sits past the routing block: no track prefix
	else if ( p >= kExtBase && p < kExtBase + kNumTracks * kNumExtParams )
		t = ( p - kExtBase ) / kNumExtParams;				// v1.1 extras are per-track again
	else if ( p >= kMidiBase && p < kMidiBase + kNumTracks * kNumMidiParams )
		t = ( p - kMidiBase ) / kNumMidiParams;			// v1.2 MIDI params are per-track too
	else if ( p >= kCurrentBase && p < kCurrentBase + kNumTracks * kNumCurrentParams )
		t = ( p - kCurrentBase ) / kNumCurrentParams;		// v1.2 Currents are per-track too
	else if ( p >= kEosBase && p < kEosBase + kNumTracks * kNumEosParams )
		t = ( p - kEosBase ) / kNumEosParams;				// v1.2 EOS is per-track too
	if ( t < 0 )
		return 0;
	buff[0] = 'T';
	buff[1] = (char)( '1' + t );
	buff[2] = ' ';
	buff[3] = 0;
	return 3;
}

static const _NT_factory factory =
{
	.guid = NT_MULTICHAR( 'S', 'h', 'o', 'l' ),
	.name = "Shoal",
	// version first: the algorithm browser truncates long descriptions,
	// and the version is the one part that must always be visible
	.description = "v1.2.0 - 8-track generative melody sequencer",
	.numSpecifications = 0,
	.calculateRequirements = calculateRequirements,
	.construct = construct,
	.parameterChanged = parameterChanged,
	.step = step,
	.draw = draw,
	.midiRealtime = midiRealtime,
	.tags = kNT_tagInstrument | kNT_tagUtility,
	.hasCustomUi = hasCustomUi,
	.customUi = customUi,
	.setupUi = setupUi,
	.parameterUiPrefix = parameterUiPrefix,
};

uintptr_t pluginEntry( _NT_selector selector, uint32_t data )
{
	switch ( selector )
	{
	case kNT_selector_version:
		return kNT_apiVersionCurrent;
	case kNT_selector_numFactories:
		return 1;
	case kNT_selector_factoryInfo:
		return (uintptr_t)( ( data == 0 ) ? &factory : NULL );
	}
	return 0;
}
