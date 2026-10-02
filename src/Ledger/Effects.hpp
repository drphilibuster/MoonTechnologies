#pragma once
// Ledger's per-track effects: Hermod+'s nineteen, up to eight in series.
//
// Squarp's firmware is closed, so these are written from its manual (see
// docs/research/HermodPlus.md) and the parameter tables below are Ledger's own
// specification of them. They run on events (Events.hpp): each takes note-ons,
// note-offs and modulation values from the slot before it and hands what it makes to
// the slot after. Time is the master clock: one tick is one beat (a quarter note), so
// "1/16" is a quarter of a tick, and everything synced lands on the clock's own grid.
//
// Every effect that makes or holds notes keeps the ids of what it started, so that
// flush() -- on a mute, a removal, a change of type -- can end each of them. A chain
// never strands a note.
//
// Pure C++11, no Rack types, no allocation.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "Events.hpp"
#include "Midi.hpp"
#include "Shoal.hpp"

namespace ledger {

enum FxType {
	kFxNone, kFxArp, kFxBernoulli, kFxChance, kFxEcho, kFxEnvelope, kFxEuclid, kFxFilter,
	kFxGlide, kFxHarmonizer, kFxHold, kFxLfo, kFxModToNote, kFxMidiOut, kFxNoteToMod,
	kFxRandomizer, kFxRatchet, kFxRegister, kFxScale, kFxSwing,
	kNumFxTypes
};

enum { kMaxFxParams = 10, kChainSlots = 8 };

// Note divisions, in beats.
static const char* const divNames[] = {
	"1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64",
};
static const double divBeats[] = {
	4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625,
};
enum { kNumDivs = 12, kDiv16 = 7, kDiv8 = 5, kDiv32 = 9 };
// LFO periods, in beats: 1/16 up to 32 bars.
static const char* const lfoDivNames[] = {
	"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars",
};
static const double lfoDivBeats[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128 };
enum { kNumLfoDivs = 10 };

static const char* const offOn[] = { "Off", "On" };
static const char* const fadeNames[] = { "Off", "Linear", "Exponential", "Logarithmic" };
static const char* const laneDest[] = { "MOD 1", "MOD 2", "MOD 3", "MOD 4", "BEND", "TOUCH" };
static const char* const arpStyles[] = { "Up", "Down", "Up-down", "Down-up", "As played", "Random", "Converge", "Diverge" };
static const char* const chanceLots[] = { "Each note", "1/16", "1/8", "Beat", "Bar" };
static const char* const chanceSyncs[] = { "Off", "Bar", "Beat", "1/8", "1/16" };
static const char* const envModes[] = { "Gate", "Trigger", "Wait AHD" };
static const char* const holdModes[] = { "Hold (toggle)", "Relatch" };
static const char* const lfoWaves[] = { "Sine", "Triangle", "Ramp", "Square", "Random" };
static const char* const pathB[] = { "Drop", "MIDI out A", "MIDI out B" };
static const char* const n2mValues[] = { "Velocity", "Gate (127 / 0)", "Pitch" };
static const char* const stickNames[] = { "Nearest", "Down", "Up", "Drop" };
static const char* const swingGrids[] = { "1/8", "1/16", "1/32" };
static const char* const scaleChoice[] = {
	"The books'", "Chromatic", "Major", "Natural minor", "Harmonic minor", "Dorian", "Phrygian",
	"Lydian", "Mixolydian", "Major pentatonic", "Minor pentatonic", "Blues", "Hirajoshi", "In-Sen",
};
static const char* const keyChoice[] = { "The books'", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const midiPorts[] = { "Out A", "Out B" };
static const char* const modToNoteSample[] = {
	"On each note", "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64",
};

struct ParamDesc
{
	const char*			name;
	int16_t				min, max, def;
	const char* const*	labels;		// enum names (min..max), or NULL for a number
	const char*			unit;
};

struct FxDesc
{
	const char*	name;
	const char*	shortName;
	int			nParams;
	ParamDesc	p[kMaxFxParams];
};

static const FxDesc fxDescs[kNumFxTypes] = {
	{ "Empty", "--", 0, {} },
	{ "Arpeggiator", "ARP", 7, {
		{ "Style", 0, 7, 0, arpStyles, "" },
		{ "Rate", 0, kNumDivs - 1, kDiv16, divNames, "" },
		{ "Octaves", -5, 5, 0, NULL, "" },
		{ "Gate", 1, 200, 50, NULL, "%" },
		{ "Humanize", 0, 100, 0, NULL, "%" },
		{ "Restart on new note", 0, 1, 1, offOn, "" },
		{ "Cycles (0 = endless)", 0, 16, 0, NULL, "" } } },
	{ "Bernoulli", "BER", 3, {
		{ "Chance of B", 0, 100, 50, NULL, "%" },
		{ "Path B", 0, 2, 0, pathB, "" },
		{ "B channel", 1, 16, 1, NULL, "" } } },
	{ "Chance", "CHN", 5, {
		{ "Chance", 0, 100, 50, NULL, "%" },
		{ "One roll per", 0, 4, 0, chanceLots, "" },
		{ "Chance x velocity", 0, 1, 0, offOn, "" },
		{ "Sync grid", 0, 4, 0, chanceSyncs, "" },
		{ "Chance on the grid", 0, 100, 100, NULL, "%" } } },
	{ "Echo", "ECH", 6, {
		{ "Time", 0, kNumDivs - 1, kDiv8, divNames, "" },
		{ "Repeats", 0, 16, 3, NULL, "" },
		{ "Fade velocity", 0, 3, 1, fadeNames, "" },
		{ "Fade gate", 0, 3, 0, fadeNames, "" },
		{ "Pitch up", 0, 60, 0, NULL, " st" },
		{ "Pitch down", 0, 60, 0, NULL, " st" } } },
	{ "Envelope", "ENV", 10, {
		{ "Attack", 0, 100, 10, NULL, "" },
		{ "Hold", 0, 100, 0, NULL, "" },
		{ "Decay", 0, 100, 40, NULL, "" },
		{ "Sustain", 0, 100, 50, NULL, "%" },
		{ "Release", 0, 100, 40, NULL, "" },
		{ "Depth", 0, 100, 100, NULL, "%" },
		{ "Offset", -100, 100, 0, NULL, "%" },
		{ "Destination", 0, 5, 0, laneDest, "" },
		{ "Mode", 0, 2, 0, envModes, "" },
		{ "Sidechain note (-1 = any)", -1, 127, -1, NULL, "" } } },
	{ "Euclid", "EUC", 7, {
		{ "Note (-1 = held notes)", -1, 127, -1, NULL, "" },
		{ "Steps", 1, 32, 16, NULL, "" },
		{ "Fills", 0, 32, 5, NULL, "" },
		{ "Rate", 0, kNumDivs - 1, kDiv16, divNames, "" },
		{ "Gate", 1, 100, 50, NULL, "%" },
		{ "Rotate", 0, 31, 0, NULL, "" },
		{ "Velocity", 1, 127, 100, NULL, "" } } },
	{ "Filter", "FLT", 4, {
		{ "Lowest note", 0, 127, 0, NULL, "" },
		{ "Highest note", 0, 127, 127, NULL, "" },
		{ "Lowest mod", 0, 127, 0, NULL, "" },
		{ "Highest mod", 0, 127, 127, NULL, "" } } },
	{ "Glide", "GLD", 2, {
		{ "Type", 0, 3, 1, NULL, "" },
		{ "Time", 0, 100, 30, NULL, "" } } },
	{ "Harmonizer", "HRM", 5, {
		{ "Keep the played note", 0, 1, 1, offOn, "" },
		{ "Harmony 1 (0 = off)", -24, 24, 7, NULL, " st" },
		{ "Harmony 2", -24, 24, 0, NULL, " st" },
		{ "Harmony 3", -24, 24, 0, NULL, " st" },
		{ "Harmony 4", -24, 24, 0, NULL, " st" } } },
	{ "Hold", "HLD", 2, {
		{ "Mode", 0, 1, 0, holdModes, "" },
		{ "Most notes (0 = any)", 0, 12, 0, NULL, "" } } },
	{ "LFO", "LFO", 8, {
		{ "Wave", 0, 4, 0, lfoWaves, "" },
		{ "Destination", 0, 5, 0, laneDest, "" },
		{ "Sync", 0, 1, 1, offOn, "" },
		{ "Period (synced)", 0, kNumLfoDivs - 1, 4, lfoDivNames, "" },
		{ "Rate (free)", 0, 100, 40, NULL, "" },
		{ "Range", -100, 100, 100, NULL, "%" },
		{ "Offset", -100, 100, 0, NULL, "%" },
		{ "Phase", -180, 180, 0, NULL, " deg" } } },
	{ "Mod to note", "M2N", 5, {
		{ "Sample", 0, kNumDivs, 0, modToNoteSample, "" },
		{ "Lowest note", 0, 127, 48, NULL, "" },
		{ "Highest note", 0, 127, 72, NULL, "" },
		{ "Pass mod on", 0, 1, 1, offOn, "" },
		{ "Source lane", 0, 5, 0, laneDest, "" } } },
	{ "MIDI output", "MID", 5, {
		{ "Port", 0, 1, 0, midiPorts, "" },
		{ "Channel", 1, 16, 1, NULL, "" },
		{ "Send notes", 0, 1, 1, offOn, "" },
		{ "Send mod", 0, 1, 1, offOn, "" },
		{ "Velocity scale", 0, 200, 100, NULL, "%" } } },
	{ "Note to mod", "N2M", 4, {
		{ "Destination", 0, 5, 0, laneDest, "" },
		{ "Value", 0, 2, 0, n2mValues, "" },
		{ "At note-off (-1 = hold)", -1, 127, -1, NULL, "" },
		{ "Pass notes on", 0, 1, 1, offOn, "" } } },
	{ "Randomizer", "RND", 8, {
		{ "Note down", 0, 12, 0, NULL, " st" },
		{ "Note up", 0, 12, 0, NULL, " st" },
		{ "Octave down", 0, 5, 0, NULL, "" },
		{ "Octave up", 0, 5, 0, NULL, "" },
		{ "Velocity down", 0, 100, 0, NULL, "%" },
		{ "Velocity up", 0, 100, 0, NULL, "%" },
		{ "Length", 0, 100, 0, NULL, "%" },
		{ "Chance", 0, 100, 100, NULL, "%" } } },
	{ "Ratchet", "RAT", 3, {
		{ "Rate", 0, kNumDivs - 1, kDiv32, divNames, "" },
		{ "Gate", 1, 100, 50, NULL, "%" },
		{ "Long-short", 0, 100, 0, NULL, "%" } } },
	{ "Register", "REG", 8, {
		{ "Seed", 0, 255, 0, NULL, "" },
		{ "Rate", 0, kNumDivs - 1, kDiv16, divNames, "" },
		{ "Bits", 2, 16, 8, NULL, "" },
		{ "Chaos", 0, 100, 10, NULL, "%" },
		{ "Centre", 0, 127, 60, NULL, "" },
		{ "Span", 0, 60, 12, NULL, " st" },
		{ "Gate", 1, 100, 50, NULL, "%" },
		{ "Every step", 0, 1, 0, offOn, "" } } },
	{ "Scale", "SCL", 4, {
		{ "Scale", 0, 13, 0, scaleChoice, "" },
		{ "Key", 0, 12, 0, keyChoice, "" },
		{ "Out-of-key notes", 0, 3, 0, stickNames, "" },
		{ "Transpose first", -36, 36, 0, NULL, " st" } } },
	{ "Swing", "SWG", 4, {
		{ "Groove", 0, 100, 60, NULL, "%" },
		{ "Grid", 0, 2, 1, swingGrids, "" },
		{ "Accent", 0, 100, 50, NULL, "%" },
		{ "Human", 0, 100, 0, NULL, "%" } } },
};

// Envelope and Glide times: 0..100 on an exponential scale.
static inline float envSeconds( int x ) { return 0.001f * powf( 10000.f, x * 0.01f ); }		// 1 ms .. 10 s
static inline int glideMs( int x ) { return x <= 0 ? 0 : (int)( 10.f * powf( 1000.f, x * 0.01f ) ); }	// 10 ms .. 10 s

// ---------------------------------------------------------------------------

// The output side of one chain slot: what an effect hands on goes to the slot after.
struct Out
{
	void (*fn)( void* ctx, int slot, const Ev& e );
	void* ctx;
	int slot;
	void operator()( const Ev& e ) const { fn( ctx, slot, e ); }
};

// Which notes an effect started for which input note.
struct IdMap
{
	enum { N = 48, K = 5 };
	struct E { uint16_t in; uint8_t n; uint16_t out[K]; };
	E e[N];
	int n;
	IdMap() : n( 0 ) {}
	void clear() { n = 0; }
	E* add( uint16_t in )
	{
		if ( n >= N ) return NULL;
		e[n].in = in; e[n].n = 0;
		return &e[n++];
	}
	// Take the entry for `in` out of the map; true if there was one.
	bool take( uint16_t in, E& out )
	{
		for ( int i = 0; i < n; ++i )
			if ( e[i].in == in ) { out = e[i]; e[i] = e[--n]; return true; }
		return false;
	}
	bool has( uint16_t in ) const { for ( int i = 0; i < n; ++i ) if ( e[i].in == in ) return true; return false; }
};

// Notes an effect is holding (arpeggiator, euclid, hold, ratchet).
struct Held
{
	enum { N = 16 };
	uint16_t id[N];
	uint8_t note[N], vel[N];
	uint32_t next[N];		// ratchet: when this one fires again
	uint16_t outId[N];		// ratchet / hold: what it is sounding as
	int n;
	Held() : n( 0 ) {}
	void clear() { n = 0; }
	int find( uint16_t i ) const { for ( int k = 0; k < n; ++k ) if ( id[k] == i ) return k; return -1; }
	int findNote( int nt ) const { for ( int k = 0; k < n; ++k ) if ( note[k] == nt ) return k; return -1; }
	bool add( uint16_t i, int nt, int v )
	{
		if ( n >= N ) return false;
		id[n] = i; note[n] = (uint8_t)nt; vel[n] = (uint8_t)v; next[n] = 0; outId[n] = 0;
		++n;
		return true;
	}
	void removeAt( int k ) { for ( int j = k; j < n - 1; ++j ) { id[j] = id[j + 1]; note[j] = note[j + 1]; vel[j] = vel[j + 1]; next[j] = next[j + 1]; outId[j] = outId[j + 1]; } --n; }
};

struct Fx
{
	uint8_t		type;
	int16_t		p[kMaxFxParams];	// as set (the track's values)
	int16_t		pe[kMaxFxParams];	// as playing: a slot's own values and CV applied
	bool		gmute;				// muted on every slot

	// state
	EvQueue<128>	q;			// what it has scheduled for the slot after it
	IdMap			map;
	Held			held;
	uint32_t		rng;
	int64_t			gridIdx;	// the last grid step it acted on
	int				idx, dir, cycles, step, physical;
	uint16_t		soundingId;	// a mono generator's (arp, register, mod-to-note) current note
	uint32_t		reg;
	float			env, envLevelAtRelease;
	int				envStage;
	uint32_t		envT;
	float			lfoPhase, lfoPrev;
	int16_t			lastMod;
	int				modIn;		// the last value on the mod-to-note source lane
	int64_t			lotIdx;
	bool			lotPass;
	uint32_t		ctlCount;
	MidiTap			tap;		// MIDI output and Bernoulli's B path: what they sounded

	Fx() { setType( kFxNone ); gmute = false; }

	const FxDesc& desc() const { return fxDescs[type < kNumFxTypes ? type : 0]; }

	void setType( int t )
	{
		type = (uint8_t)( t < 0 || t >= kNumFxTypes ? kFxNone : t );
		for ( int i = 0; i < kMaxFxParams; ++i )
			p[i] = pe[i] = i < desc().nParams ? desc().p[i].def : 0;
		resetState();
	}

	void resetState()
	{
		q.clear(); map.clear(); held.clear();
		rng = 0x9E3779B9u;
		gridIdx = INT64_MIN;
		idx = 0; dir = 1; cycles = 0; step = 0; physical = 0;
		soundingId = 0;
		reg = 0xFFFFFFFFu;
		env = 0.f; envLevelAtRelease = 0.f; envStage = 0; envT = 0;
		lfoPhase = 0.f; lfoPrev = 0.f;
		lastMod = INT16_MIN;
		modIn = 0;
		lotIdx = INT64_MIN; lotPass = true;
		ctlCount = 0;
		tap.reset();
		for ( int i = 0; i < kEchoRecs; ++i ) echoes[i].used = false;
	}

	uint32_t rand()
	{
		rng = rng * 1664525u + 1013904223u;
		return rng >> 8;
	}
	float frand() { return ( rand() & 0xFFFFFF ) / 16777216.f; }

	// End everything this effect has started, now, and forget its state.
	void flush( const Out& out )
	{
		// what was scheduled: note-offs leave now, note-ons never
		releaseQueue( out );
		for ( int i = 0; i < map.n; ++i )
			for ( int k = 0; k < map.e[i].n; ++k ) out( Ev::off( map.e[i].out[k] ) );
		for ( int i = 0; i < held.n; ++i ) if ( held.outId[i] ) out( Ev::off( held.outId[i] ) );
		if ( soundingId ) out( Ev::off( soundingId ) );
		for ( int i = 0; i < kEchoRecs; ++i )
			if ( echoes[i].used )
				for ( int k = 0; k < echoes[i].n; ++k ) out( Ev::off( echoes[i].echo[k] ) );
		tap.flush();
		resetState();
	}

	// ---- helpers ------------------------------------------------------------

	// A grid of `beats` crossed since the last call: true once per step.
	bool gridStep( const TimeCtx& c, double beats )
	{
		if ( beats <= 0.0 || !c.running ) return false;
		int64_t i = (int64_t)floor( c.beat / beats + 1e-9 );
		if ( i == gridIdx ) return false;
		gridIdx = i;
		return true;
	}

	uint32_t beatsToSamples( const TimeCtx& c, double beats ) const
	{
		double s = beats * c.spb;
		return s < 1.0 ? 1u : ( s > 2.0e9 ? 2000000000u : (uint32_t)s );
	}

	static int clampNote( int n ) { return n < 0 ? 0 : n > 127 ? 127 : n; }

	// Schedule an event. A note-off that will not fit leaves now instead -- a note cut
	// short, never a note left hanging; a note-on that will not fit is dropped.
	void sched( uint32_t at, const Ev& e, const Out& out )
	{
		if ( !q.push( at, e ) && e.type == kEvNoteOff ) out( e );
	}
	// Drop what was still to start, and end now what was still to end.
	void releaseQueue( const Out& out )
	{
		q.dropNoteOns();
		while ( q.n ) { Ev e = q.q[q.n - 1].ev; --q.n; if ( e.type == kEvNoteOff ) out( e ); }
	}

	// Pass a note-on through under a new id, remembering it.
	void passOn( const Ev& in, const Out& out, IdSource& ids, int note, int vel )
	{
		IdMap::E* m = map.add( in.id );
		if ( !m ) return;
		Ev e = in;
		e.id = ids.get();
		e.note = (uint8_t)clampNote( note );
		e.vel = (uint8_t)( vel < 1 ? 1 : vel > 127 ? 127 : vel );
		m->out[m->n++] = e.id;
		out( e );
	}
	// End what an input note started. A note-off for a note this effect never saw (it
	// went round while the effect was muted, or before it was there) goes on as it is,
	// so whatever downstream did start for it can end.
	void passOff( const Ev& in, const Out& out )
	{
		IdMap::E m;
		if ( map.take( in.id, m ) )
			for ( int k = 0; k < m.n; ++k ) out( Ev::off( m.out[k] ) );
		else
			out( in );
	}

	// ---- events -------------------------------------------------------------

	void event( const Ev& in, const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		if ( in.type == kEvMod )
		{
			switch ( type )
			{
				case kFxFilter:
				{
					if ( in.lane > 3 ) { out( in ); return; }	// bend and touch are not mod values
					int lo = P[2], hi = P[3], v = in.value;
					bool inside = lo <= hi ? ( v >= lo && v <= hi ) : !( v > hi && v < lo );
					if ( inside ) out( in );
					return;
				}
				case kFxModToNote:
					if ( in.lane == P[4] ) modIn = in.value;
					if ( P[3] ) out( in );
					return;
				case kFxMidiOut:
					if ( P[3] ) tap.mod( c.midi, P[0], P[1] - 1, in, c.midiMap );
					out( in );
					return;
				default:
					out( in );
					return;
			}
		}
		if ( in.type == kEvNoteOff )
		{
			noteOff( in, c, out, ids );
			return;
		}
		noteOn( in, c, out, ids );
	}

	void noteOn( const Ev& in, const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		switch ( type )
		{
			case kFxArp:
				if ( held.n == 0 ) { cycles = 0; if ( P[5] ) idx = 0; }	// a new phrase
				held.add( in.id, in.note, in.vel );
				return;
			case kFxBernoulli:
				// path A goes on down the chain as itself; path B is dropped, or leaves for MIDI
				if ( (int)( rand() % 100 ) < P[0] )
				{
					if ( P[1] > 0 ) tap.on( c.midi, P[1] - 1, P[2] - 1, in, in.vel );
					return;
				}
				out( in );
				return;
			case kFxChance:
			{
				bool pass;
				static const double lots[] = { 0, 0.25, 0.5, 1, 4 };
				static const double syncs[] = { 0, 4, 1, 0.5, 0.25 };
				double sg = syncs[P[3] < 0 ? 0 : P[3] > 4 ? 4 : P[3]];
				// on the grid: within a hundredth of a beat of one of its lines
				bool onGrid = sg > 0 && fabs( c.beat - floor( c.beat / sg + 0.5 ) * sg ) < 0.01;
				int chance = onGrid ? P[4] : P[0];
				if ( P[2] ) chance = chance * in.vel / 127;
				double lot = lots[P[1] < 0 ? 0 : P[1] > 4 ? 4 : P[1]];
				if ( lot > 0 && !onGrid )
				{
					int64_t li = (int64_t)floor( c.beat / lot + 1e-9 );
					if ( li != lotIdx ) { lotIdx = li; lotPass = (int)( rand() % 100 ) < chance; }
					pass = lotPass;
				}
				else
					pass = (int)( rand() % 100 ) < chance;
				if ( pass ) out( in );
				return;
			}
			case kFxEcho:
			{
				// the note itself, then its echoes' note-ons; their note-offs come when the
				// note ends (noteOff), as long after each echo's start as the note lasted
				passOn( in, out, ids, in.note, in.vel );
				if ( !map.has( in.id ) ) return;
				EchoRec* r = NULL;
				for ( int i = 0; i < kEchoRecs && !r; ++i ) if ( !echoes[i].used ) r = &echoes[i];
				if ( !r ) return;							// no room to remember them: no echoes
				r->used = true; r->in = in.id; r->start = c.now; r->n = 0;
				uint32_t T = beatsToSamples( c, divBeats[P[0]] );
				for ( int k = 1; k <= P[1] && k <= 16; ++k )
				{
					float x = (float)k / (float)( P[1] + 1 );
					int vel = (int)( in.vel * fade( P[2], x ) + 0.5f );
					if ( vel < 1 ) break;
					int shift = 0;
					if ( P[4] && P[5] ) shift = ( k & 1 ) ? (int)lroundf( P[4] * (float)k / P[1] ) : -(int)lroundf( P[5] * (float)k / P[1] );
					else if ( P[4] ) shift = (int)lroundf( P[4] * (float)k / P[1] );
					else if ( P[5] ) shift = -(int)lroundf( P[5] * (float)k / P[1] );
					Ev e = in;
					e.id = ids.get();
					e.note = (uint8_t)clampNote( in.note + shift );
					e.vel = (uint8_t)vel;
					e.flags = 0;
					uint32_t at = c.now + (uint32_t)k * T;
					if ( !q.push( at, e ) ) break;
					r->echo[r->n] = e.id;
					r->echoAt[r->n] = at;
					r->n++;
				}
				return;
			}
			case kFxEnvelope:
				out( in );
				if ( P[9] < 0 || P[9] == in.note )
				{
					held.add( in.id, in.note, in.vel );
					envStage = 1;		// attack, from where it is
					envT = 0;
				}
				return;
			case kFxEuclid:
				if ( P[0] < 0 ) held.add( in.id, in.note, in.vel );
				else passOn( in, out, ids, in.note, in.vel );
				return;
			case kFxFilter:
			{
				int lo = P[0], hi = P[1], n = in.note;
				bool inside = lo <= hi ? ( n >= lo && n <= hi ) : !( n > hi && n < lo );
				if ( inside ) out( in );
				return;
			}
			case kFxGlide:
			{
				IdMap::E* m = map.add( in.id );
				if ( !m ) return;
				Ev e = in;
				e.id = ids.get();
				e.glideType = (uint8_t)P[0];
				e.glideMs = (uint16_t)( glideMs( P[1] ) > 65535 ? 65535 : glideMs( P[1] ) );
				m->out[m->n++] = e.id;
				out( e );
				return;
			}
			case kFxHarmonizer:
			{
				IdMap::E* m = map.add( in.id );
				if ( !m ) return;
				int notes[5], nn = 0;
				if ( P[0] ) notes[nn++] = in.note;
				for ( int h = 1; h <= 4; ++h )
				{
					if ( P[h] == 0 ) continue;
					int n = clampNote( in.note + P[h] );
					bool dup = false;
					for ( int j = 0; j < nn; ++j ) dup = dup || notes[j] == n;
					if ( !dup ) notes[nn++] = n;
				}
				for ( int j = 0; j < nn && m->n < IdMap::K; ++j )
				{
					Ev e = in;
					e.id = ids.get();
					e.note = (uint8_t)notes[j];
					m->out[m->n++] = e.id;
					out( e );
				}
				return;
			}
			case kFxHold:
			{
				if ( P[0] == 1 )	// relatch: a new chord starts once every key was up
				{
					if ( physical == 0 )
						for ( int k = held.n - 1; k >= 0; --k ) { out( Ev::off( held.outId[k] ) ); held.removeAt( k ); }
					physical++;
				}
				else
				{
					int k = held.findNote( in.note );
					if ( k >= 0 ) { out( Ev::off( held.outId[k] ) ); held.removeAt( k ); return; }	// toggled off
				}
				if ( P[1] > 0 && held.n >= P[1] ) { out( Ev::off( held.outId[0] ) ); held.removeAt( 0 ); }
				if ( held.n >= Held::N ) { out( Ev::off( held.outId[0] ) ); held.removeAt( 0 ); }
				held.add( in.id, in.note, in.vel );
				Ev e = in;
				e.id = ids.get();
				held.outId[held.n - 1] = e.id;
				out( e );
				return;
			}
			case kFxModToNote:
				if ( P[0] == 0 )
				{
					int lo = P[1], hi = P[2];
					int v = modIn < 0 ? 0 : modIn > 127 ? 127 : modIn;
					int n = lo + (int)lroundf( ( hi - lo ) * v / 127.f );
					passOn( in, out, ids, n, in.vel );
				}
				else
					passOn( in, out, ids, in.note, in.vel );
				return;
			case kFxNoteToMod:
			{
				int v = P[1] == 0 ? in.vel : P[1] == 1 ? 127 : in.note;
				out( Ev::mod( P[0], P[0] == 4 ? ( v - 64 ) * 128 : v ) );
				if ( P[3] ) passOn( in, out, ids, in.note, in.vel );
				return;
			}
			case kFxRandomizer:
			{
				int note = in.note, vel = in.vel;
				if ( (int)( rand() % 100 ) < P[7] )
				{
					if ( P[0] + P[1] > 0 ) note += -P[0] + (int)( rand() % (uint32_t)( P[0] + P[1] + 1 ) );
					if ( P[2] + P[3] > 0 ) note += 12 * ( -P[2] + (int)( rand() % (uint32_t)( P[2] + P[3] + 1 ) ) );
					if ( P[4] + P[5] > 0 )
						vel += (int)lroundf( 1.27f * ( -P[4] + (int)( rand() % (uint32_t)( P[4] + P[5] + 1 ) ) ) );
				}
				passOn( in, out, ids, note, vel );
				return;
			}
			case kFxRatchet:
			{
				if ( !held.add( in.id, in.note, in.vel ) ) return;
				int k = held.n - 1;
				held.next[k] = c.now;		// the first pulse is now
				ratchetFire( k, c, out, ids );
				return;
			}
			case kFxScale:
			{
				int n = quantize( in.note + P[3], c );
				if ( n >= 0 ) passOn( in, out, ids, n, in.vel );
				return;
			}
			case kFxSwing:
			{
				double g = P[1] == 0 ? 0.5 : P[1] == 1 ? 0.25 : 0.125;
				double k = floor( c.beat / g + 0.5 );
				bool off = ( (int64_t)k & 1 ) != 0;
				bool near = fabs( c.beat - k * g ) < g * 0.25;
				double d = 0.0;
				if ( near )
				{
					if ( off && P[0] > 50 ) d = ( P[0] - 50 ) / 50.0 * g * 0.5;
					if ( !off && P[0] < 50 ) d = ( 50 - P[0] ) / 50.0 * g * 0.5;
				}
				d += P[3] / 100.0 * g * 0.25 * frand();
				int vel = in.vel;
				if ( near )
				{
					float a = ( P[2] - 50 ) / 50.f * 0.5f;
					vel = (int)lroundf( in.vel * ( 1.f + ( off ? -a : a ) ) );
				}
				uint32_t delay = (uint32_t)( d * c.spb );
				IdMap::E* m = map.add( in.id );
				if ( !m ) return;
				Ev e = in;
				e.id = ids.get();
				e.vel = (uint8_t)( vel < 1 ? 1 : vel > 127 ? 127 : vel );
				m->out[m->n++] = e.id;
				m->out[IdMap::K - 1] = (uint16_t)( delay > 65535 ? 65535 : delay );	// remembered for the note-off
				if ( delay == 0 || !q.push( c.now + delay, e ) ) out( e );
				return;
			}
			case kFxMidiOut:
				// a tap: the note goes to MIDI and on down the chain
				if ( P[2] ) tap.on( c.midi, P[0], P[1] - 1, in, ( in.vel * P[4] + 50 ) / 100 );
				out( in );
				return;
			default:
				out( in );
				return;
		}
	}

	void noteOff( const Ev& in, const TimeCtx& c, const Out& out, IdSource& ids )
	{
		(void)ids;
		const int16_t* P = pe;
		switch ( type )
		{
			case kFxArp:
			case kFxEuclid:
			{
				int k = held.find( in.id );
				if ( k >= 0 ) held.removeAt( k );
				else passOff( in, out );
				if ( type == kFxArp && held.n == 0 && k >= 0 )
				{
					// nothing held: the arpeggio stops with the last key
					releaseQueue( out );
					if ( soundingId ) out( Ev::off( soundingId ) );
					soundingId = 0;
				}
				return;
			}
			case kFxEcho:
			{
				// the original ends now; each echo ends as long after its start, faded
				IdMap::E m;
				if ( !map.take( in.id, m ) ) { out( in ); return; }
				for ( int k = 0; k < m.n; ++k ) out( Ev::off( m.out[k] ) );
				echoRelease( in.id, c, out );
				return;
			}
			case kFxEnvelope:
			{
				out( in );
				int k = held.find( in.id );
				if ( k >= 0 ) held.removeAt( k );
				if ( held.n == 0 && P[8] == 0 && envStage > 0 && envStage < 5 )
				{
					envStage = 5;		// release
					envT = 0;
					envLevelAtRelease = env;
				}
				return;
			}
			case kFxHold:
				if ( held.find( in.id ) < 0 ) { out( in ); return; }	// not one of ours
				if ( P[0] == 1 && physical > 0 ) physical--;
				return;			// held notes outlive their keys
			case kFxRandomizer:
			{
				IdMap::E m;
				if ( !map.take( in.id, m ) ) { out( in ); return; }
				uint32_t extra = 0;
				if ( P[6] > 0 ) extra = (uint32_t)( frand() * P[6] / 100.f * 4.f * c.spb );
				for ( int k = 0; k < m.n; ++k )
				{
					if ( extra == 0 ) out( Ev::off( m.out[k] ) );
					else sched( c.now + extra, Ev::off( m.out[k] ), out );
				}
				return;
			}
			case kFxRatchet:
			{
				int k = held.find( in.id );
				if ( k < 0 ) { out( in ); return; }
				if ( held.outId[k] ) out( Ev::off( held.outId[k] ) );
				// a pulse's scheduled note-off may still be waiting: it finds its voice gone
				held.removeAt( k );
				return;
			}
			case kFxSwing:
			{
				IdMap::E m;
				if ( !map.take( in.id, m ) ) { out( in ); return; }
				uint32_t delay = m.out[IdMap::K - 1];
				for ( int k = 0; k < m.n; ++k )
				{
					if ( delay == 0 ) out( Ev::off( m.out[k] ) );
					else sched( c.now + delay, Ev::off( m.out[k] ), out );
				}
				return;
			}
			case kFxNoteToMod:
				if ( P[2] >= 0 ) out( Ev::mod( P[0], P[0] == 4 ? ( P[2] - 64 ) * 128 : P[2] ) );
				else if ( P[1] == 1 ) out( Ev::mod( P[0], 0 ) );		// gate mode: back to 0
				passOff( in, out );		// whether or not notes pass now: they may have when it began
				return;
			case kFxMidiOut:
				tap.off( c.midi, in.id );
				out( in );
				return;
			case kFxBernoulli:
				// a note that went to B ends there; one that went on (or was dropped) ends down the chain
				if ( !tap.off( c.midi, in.id ) ) out( in );
				return;
			case kFxNone:
			case kFxLfo:
			case kFxRegister:
				out( in );
				return;
			default:
				passOff( in, out );
				return;
		}
	}

	// ---- time ---------------------------------------------------------------

	// One sample. Scheduled events leave; clocked effects act on their grid.
	void tick( const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		Ev e;
		while ( q.popDue( c.now, e ) )
		{
			if ( ( type == kFxArp || type == kFxRegister || ( type == kFxModToNote && P[0] > 0 ) )
				 && e.type == kEvNoteOff && e.id == soundingId )
				soundingId = 0;
			out( e );
		}
		switch ( type )
		{
			case kFxArp:
				if ( gridStep( c, divBeats[P[1]] ) && held.n > 0 )
					arpFire( c, out, ids );
				break;
			case kFxEuclid:
				if ( gridStep( c, divBeats[P[3]] ) )
				{
					int steps = P[1] < 1 ? 1 : P[1];
					int s = step % steps;
					step = ( step + 1 ) % steps;
					int fills = P[2] > steps ? steps : P[2];
					bool hit = fills > 0 && ( ( ( s + P[5] ) * fills ) % steps ) < fills;
					if ( !hit ) break;
					uint32_t len = beatsToSamples( c, divBeats[P[3]] * P[4] / 100.0 );
					if ( P[0] >= 0 ) fireNote( P[0], P[6], len, c, out, ids, false );
					else
						for ( int k = 0; k < held.n; ++k ) fireNote( held.note[k], held.vel[k], len, c, out, ids, false );
				}
				break;
			case kFxRatchet:
				for ( int k = 0; k < held.n; ++k )
					if ( (int32_t)( c.now - held.next[k] ) >= 0 ) ratchetFire( k, c, out, ids );
				break;
			case kFxRegister:
				if ( gridStep( c, divBeats[P[1]] ) ) registerFire( c, out, ids );
				break;
			case kFxModToNote:
				if ( P[0] > 0 && gridStep( c, divBeats[P[0] - 1] ) )
				{
					int lo = P[1], hi = P[2];
					int v = modIn < 0 ? 0 : modIn > 127 ? 127 : modIn;
					int n = lo + (int)lroundf( ( hi - lo ) * v / 127.f );
					fireNote( n, 100, beatsToSamples( c, divBeats[P[0] - 1] * 0.5 ), c, out, ids, true );
				}
				break;
			case kFxLfo:
				if ( ( ctlCount++ & 31 ) == 0 ) lfoTick( c, out );
				break;
			case kFxEnvelope:
				envTick( c );
				if ( ( ctlCount++ & 31 ) == 0 ) envEmit( out );
				break;
			default:
				break;
		}
	}

	// ---- the effects' insides ----------------------------------------------

	// x is how far through the repeats, 0..1. Past 1 (Repeats was turned down while the
	// echoes it made were still sounding) is the end of the fade.
	static float fade( int mode, float x )
	{
		if ( x > 1.f ) x = 1.f;
		if ( x < 0.f ) x = 0.f;
		switch ( mode )
		{
			case 1: return 1.f - x;
			case 2: return ( 1.f - x ) * ( 1.f - x );
			case 3: return sqrtf( 1.f - x );
			default: return 1.f;
		}
	}

	// Echo's bookkeeping: for each sounding note, when it started and its echoes.
	enum { kEchoRecs = 12 };
	struct EchoRec { uint16_t in; uint32_t start; uint16_t echo[16]; uint32_t echoAt[16]; uint8_t n; bool used; };
	EchoRec echoes[kEchoRecs];

	void echoRelease( uint16_t in, const TimeCtx& c, const Out& out )
	{
		for ( int i = 0; i < kEchoRecs; ++i )
		{
			EchoRec& r = echoes[i];
			if ( !r.used || r.in != in ) continue;
			uint32_t len = c.now - r.start;
			for ( int k = 0; k < r.n; ++k )
			{
				float x = (float)( k + 1 ) / (float)( pe[1] + 1 );
				uint32_t l = (uint32_t)( len * fade( pe[3], x ) );
				if ( l < 1 ) l = 1;
				uint32_t offAt = r.echoAt[k] + l;
				if ( (int32_t)( offAt - c.now ) < 0 ) offAt = c.now;		// that echo has already played out
				sched( offAt, Ev::off( r.echo[k] ), out );
			}
			r.used = false;
		}
	}

	// A one-shot note from a clocked generator; `mono` ends the last one first.
	void fireNote( int note, int vel, uint32_t len, const TimeCtx& c, const Out& out, IdSource& ids, bool mono )
	{
		if ( mono && soundingId ) { out( Ev::off( soundingId ) ); soundingId = 0; }
		uint16_t id = ids.get();
		out( Ev::on( id, note, vel ) );
		sched( c.now + len, Ev::off( id ), out );
		if ( mono ) soundingId = id;
	}

	void arpFire( const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		if ( P[6] > 0 && cycles >= P[6] ) return;
		// the notes it walks: held notes (sorted, or as played), across the octaves
		int base[Held::N], bvel[Held::N], nb = held.n;
		for ( int k = 0; k < nb; ++k ) { base[k] = held.note[k]; bvel[k] = held.vel[k]; }
		if ( P[0] != 4 )
			for ( int i = 1; i < nb; ++i )
			{
				int v = base[i], w = bvel[i], j = i - 1;
				while ( j >= 0 && base[j] > v ) { base[j + 1] = base[j]; bvel[j + 1] = bvel[j]; --j; }
				base[j + 1] = v; bvel[j + 1] = w;
			}
		int oct = P[2], no = ( oct < 0 ? -oct : oct ) + 1;
		int seq[Held::N * 6], svel[Held::N * 6], ns = 0;
		for ( int o = 0; o < no; ++o )
			for ( int k = 0; k < nb; ++k ) { seq[ns] = clampNote( base[k] + 12 * ( oct < 0 ? -o : o ) ); svel[ns++] = bvel[k]; }
		if ( ns == 0 ) return;
		int pick = 0;
		switch ( P[0] )
		{
			case 1: pick = ns - 1 - idx % ns; idx++; break;			// down
			case 2: case 3:											// up-down, down-up: no repeated ends
			{
				int period = ns > 1 ? 2 * ns - 2 : 1;
				int i = idx % period;
				int pos = i < ns ? i : period - i;
				pick = P[0] == 2 ? pos : ns - 1 - pos;
				idx++;
				break;
			}
			case 5: pick = (int)( rand() % (uint32_t)ns ); idx++; break;	// random
			case 6: { int i = idx % ns; pick = ( i & 1 ) ? ns - 1 - i / 2 : i / 2; idx++; break; }	// converge
			case 7: { int i = idx % ns, m = ( ns - 1 ) / 2, kk = ( i + 1 ) / 2; pick = m + ( ( i & 1 ) ? kk : -kk ); if ( pick < 0 ) pick = 0; if ( pick >= ns ) pick = ns - 1; idx++; break; }	// diverge
			default: pick = idx % ns; idx++; break;					// up, as played
		}
		if ( idx > 0 && idx % ns == 0 ) cycles++;
		int vel = svel[pick];
		float g = P[3] / 100.f;
		if ( P[4] > 0 )
		{
			vel = (int)lroundf( vel * ( 1.f - P[4] / 100.f * 0.5f * frand() ) );
			g *= 1.f - P[4] / 100.f * 0.5f * frand();
		}
		uint32_t len = beatsToSamples( c, divBeats[P[1]] * g );
		// a gate past 100% overlaps the next note; the voices make it legato
		if ( soundingId && P[3] <= 100 ) { out( Ev::off( soundingId ) ); soundingId = 0; }
		uint16_t id = ids.get();
		out( Ev::on( id, seq[pick], vel ) );
		sched( c.now + len, Ev::off( id ), out );
		soundingId = id;
	}

	void ratchetFire( int k, const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		uint32_t T = beatsToSamples( c, divBeats[P[0]] );
		float g = P[1] / 100.f;
		if ( P[2] > 0 ) g *= ( idx++ & 1 ) ? 1.f - P[2] / 200.f : 1.f + P[2] / 200.f;
		if ( g > 0.98f ) g = 0.98f;
		if ( held.outId[k] ) out( Ev::off( held.outId[k] ) );
		uint16_t id = ids.get();
		out( Ev::on( id, held.note[k], held.vel[k] ) );
		uint32_t len = (uint32_t)( T * g );
		sched( c.now + ( len ? len : 1 ), Ev::off( id ), out );
		held.outId[k] = id;
		held.next[k] = c.now + T;
	}

	void registerFire( const TimeCtx& c, const Out& out, IdSource& ids )
	{
		const int16_t* P = pe;
		int bits = P[2] < 2 ? 2 : P[2] > 16 ? 16 : P[2];
		uint32_t mask = ( 1u << bits ) - 1;
		if ( reg == 0xFFFFFFFFu )
		{
			rng = shoal::hash3( (uint32_t)P[0], 0x7E61u, 0 ) | 1u;
			reg = shoal::hash3( (uint32_t)P[0], 0x5EEDu, 1 ) & mask;
		}
		uint32_t top = ( reg >> ( bits - 1 ) ) & 1u;
		bool flip = P[3] >= 100 ? true : P[3] <= 0 ? false : (int)( rand() % 100 ) < P[3];
		uint32_t nb = top ^ ( flip ? 1u : 0u );
		reg = ( ( reg << 1 ) | nb ) & mask;
		bool play = P[7] || ( ( reg >> ( bits - 1 ) ) & 1u );
		if ( !play ) return;
		float v = (float)reg / (float)mask;
		int note = P[4] + (int)lroundf( ( v * 2.f - 1.f ) * P[5] );
		fireNote( clampNote( note ), 100, beatsToSamples( c, divBeats[P[1]] * P[6] / 100.0 ), c, out, ids, true );
	}

	// SCALE: the note in key, or -1 to drop it.
	int quantize( int note, const TimeCtx& c ) const
	{
		const int16_t* P = pe;
		int scale = P[0] == 0 ? c.scale : P[0] - 1;
		int key = P[1] == 0 ? c.root % 12 : P[1] - 1;
		if ( scale < 0 || scale >= shoal::kNumScales ) scale = 0;
		uint16_t mask = shoal::scaleMasks[scale];
		note = clampNote( note );
		auto in = [&]( int n ) { int pc = ( ( n - key ) % 12 + 12 ) % 12; return ( mask >> pc ) & 1; };
		if ( in( note ) ) return note;
		switch ( P[2] )
		{
			case 3: return -1;
			case 1: for ( int d = 1; d < 12; ++d ) if ( in( note - d ) ) return clampNote( note - d ); break;
			case 2: for ( int d = 1; d < 12; ++d ) if ( in( note + d ) ) return clampNote( note + d ); break;
			default:
				for ( int d = 1; d < 12; ++d )
				{
					if ( in( note - d ) ) return clampNote( note - d );
					if ( in( note + d ) ) return clampNote( note + d );
				}
		}
		return note;
	}

	void lfoTick( const TimeCtx& c, const Out& out )
	{
		const int16_t* P = pe;
		float phase;
		if ( P[2] )
			phase = (float)fmod( c.beat / lfoDivBeats[P[3]], 1.0 );
		else
		{
			float hz = 0.1f * powf( 10000.f, P[4] * 0.01f );
			lfoPhase += hz * 32.f / (float)c.sampleRate;
			lfoPhase -= floorf( lfoPhase );
			phase = lfoPhase;
		}
		phase += P[7] / 360.f;
		phase -= floorf( phase );
		float w;
		switch ( P[0] )
		{
			case 1: w = phase < 0.5f ? 4.f * phase - 1.f : 3.f - 4.f * phase; break;
			case 2: w = 2.f * phase - 1.f; break;
			case 3: w = phase < 0.5f ? 1.f : -1.f; break;
			case 4:		// random: a new value each cycle
			{
				if ( phase < lfoPrev ) idx++;
				w = ( shoal::hash3( (uint32_t)idx, 0x4A1Du, 7 ) & 0xFFFF ) / 32767.5f - 1.f;
				break;
			}
			default: w = sinf( 6.2831853f * phase ); break;
		}
		lfoPrev = phase;
		float u = P[6] / 100.f + P[5] / 100.f * w;
		int lane = P[1];
		int v = lane == 4 ? (int)lroundf( u * 8191.f ) : (int)lroundf( 63.5f + 63.5f * u );
		int lo = lane == 4 ? -8192 : 0, hi = lane == 4 ? 8191 : 127;
		v = v < lo ? lo : v > hi ? hi : v;
		if ( v != lastMod ) { lastMod = (int16_t)v; out( Ev::mod( lane, v ) ); }
	}

	// AHDSR: stages 1 attack, 2 hold, 3 decay, 4 sustain, 5 release, 0 idle.
	void envTick( const TimeCtx& c )
	{
		const int16_t* P = pe;
		if ( envStage == 0 ) return;
		float sr = (float)c.sampleRate;
		envT++;
		switch ( envStage )
		{
			case 1:
			{
				float n = envSeconds( P[0] ) * sr;
				env += ( 1.f - env ) / ( n < 1.f ? 1.f : n ) * 1.5f;
				if ( env >= 0.999f || envT >= (uint32_t)n ) { env = 1.f; envStage = 2; envT = 0; }
				break;
			}
			case 2:
				if ( P[1] == 0 || envT >= (uint32_t)( envSeconds( P[1] ) * sr ) ) { envStage = 3; envT = 0; }
				break;
			case 3:
			{
				float s = P[3] / 100.f, n = envSeconds( P[2] ) * sr;
				env += ( s - env ) * ( 4.6f / ( n < 1.f ? 1.f : n ) );
				if ( envT >= (uint32_t)n )
				{
					if ( P[8] == 1 || ( P[8] == 2 && held.n == 0 ) ) { envStage = 5; envT = 0; envLevelAtRelease = env; }
					else envStage = 4;
				}
				break;
			}
			case 4:
				env = P[3] / 100.f;
				if ( P[8] == 2 && held.n == 0 ) { envStage = 5; envT = 0; envLevelAtRelease = env; }
				break;
			case 5:
			{
				float n = envSeconds( P[4] ) * sr;
				env -= env * ( 4.6f / ( n < 1.f ? 1.f : n ) );
				if ( env < 0.001f ) { env = 0.f; envStage = 0; }
				break;
			}
		}
	}
	void envEmit( const Out& out )
	{
		const int16_t* P = pe;
		int lane = P[7];
		float u = P[6] / 100.f + P[5] / 100.f * env;
		int v = lane == 4 ? (int)lroundf( u * 8191.f ) : (int)lroundf( u * 127.f );
		int lo = lane == 4 ? -8192 : 0, hi = lane == 4 ? 8191 : 127;
		v = v < lo ? lo : v > hi ? hi : v;
		if ( v != lastMod ) { lastMod = (int16_t)v; out( Ev::mod( lane, v ) ); }
	}

	// Does this effect hold anything that a flush would have to end?
	bool busy() const { return q.n || map.n || held.n || soundingId; }
};

// ---------------------------------------------------------------------------
// A track's chain: eight slots in series.
// ---------------------------------------------------------------------------

struct Chain
{
	Fx			fx[kChainSlots];
	bool		muted[kChainSlots];		// effective: the effect's own mute, or the playing slot's
	IdSource*	ids;
	TimeCtx		ctx;
	void		(*sink)( void* ctx, const Ev& e );
	void*		sinkCtx;

	Chain() : ids( 0 ), sink( 0 ), sinkCtx( 0 )
	{
		for ( int i = 0; i < kChainSlots; ++i ) muted[i] = false;
		memset( &ctx, 0, sizeof ctx );
	}

	bool live( int i ) const { return fx[i].type != kFxNone && !muted[i]; }
	bool active() const { for ( int i = 0; i < kChainSlots; ++i ) if ( live( i ) ) return true; return false; }

	static void route( void* self, int slot, const Ev& e ) { ( (Chain*)self )->push( slot, e ); }
	Out outFrom( int slot ) { Out o; o.fn = &Chain::route; o.ctx = this; o.slot = slot; return o; }

	// Hand an event to slot i (or the first live slot after it).
	void push( int i, const Ev& e )
	{
		while ( i < kChainSlots && !live( i ) ) ++i;
		if ( i >= kChainSlots ) { if ( sink ) sink( sinkCtx, e ); return; }
		fx[i].event( e, ctx, outFrom( i + 1 ), *ids );
	}

	void tick()
	{
		for ( int i = 0; i < kChainSlots; ++i )
			if ( live( i ) ) fx[i].tick( ctx, outFrom( i + 1 ), *ids );
	}

	// Mute or unmute slot i; a slot muted with notes in it ends them first.
	void setMuted( int i, bool m )
	{
		if ( muted[i] == m ) return;
		if ( m && fx[i].type != kFxNone ) fx[i].flush( outFrom( i + 1 ) );
		muted[i] = m;
		if ( !m ) fx[i].resetState();
	}

	void setType( int i, int type )
	{
		if ( fx[i].type != kFxNone ) fx[i].flush( outFrom( i + 1 ) );
		fx[i].setType( type );
	}

	// End every note anywhere in the chain.
	void flushAll()
	{
		for ( int i = 0; i < kChainSlots; ++i )
			if ( fx[i].type != kFxNone ) fx[i].flush( outFrom( i + 1 ) );
	}
};

}
