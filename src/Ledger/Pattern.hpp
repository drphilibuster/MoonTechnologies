#pragma once
// Ledger's written patterns (Hermod+-style), and the slots that hold them.
//
// A pattern is what a generator is not: notes written down. It is played by the same
// walker as a generator -- Shoal's scheduler and its fifteen walks advance the track
// one step at a time (see Host::externalStep in Shoal.hpp) -- so a pattern track keeps
// its RATE, LENG, DIRN, SHFT, SLOP, BREATHE and CHANCE. What a step holds is different:
// any number of notes, each starting anywhere inside the step (kTicksPerStep to a
// step) and lasting any length, so chords, overlaps and off-grid notes are all fine.
//
// Each step also carries a point on each of six lanes (MOD 1-4, pitch bend,
// aftertouch); between points a lane holds, ramps or curves (Interp).
//
// A track has kNumSlots slots. A slot is empty, a generator or a pattern, and carries
// the track settings it plays with (its "block"), so launching a slot changes the
// whole of what the track is doing.
//
// Pure C++11, no Rack types, no allocation.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "Shoal.hpp"

namespace ledger {

enum
{
	kTicksPerStep = 24,
	kMaxNotes = 256,
	kNumSlots = 16,
	kLaneUnset = -32768,
};

enum Lane { kLaneMod1, kLaneMod2, kLaneMod3, kLaneMod4, kLanePB, kLaneAT, kNumLanes };
static const char* const laneNames[kNumLanes] = { "MOD 1", "MOD 2", "MOD 3", "MOD 4", "BEND", "TOUCH" };
// Value ranges: MOD and TOUCH 0..127, BEND -8192..8191.
static inline int laneMin( int lane ) { return lane == kLanePB ? -8192 : 0; }
static inline int laneMax( int lane ) { return lane == kLanePB ? 8191 : 127; }

enum Interp { kInterpOff, kInterpLin, kInterpS, kInterpLog, kInterpExp, kNumInterps };
static const char* const interpNames[kNumInterps] = { "OFF", "LIN", "S", "LOG", "EXP" };

struct Note
{
	uint16_t	start;		// ticks from the start of the pattern
	uint16_t	len;		// ticks, >= 1
	uint8_t		pitch;		// MIDI note, 60 = C4 (0 V is C3 = 48)
	uint8_t		vel;		// 1..127
};

struct Pattern
{
	uint16_t	count;							// notes in use, sorted by start
	Note		notes[kMaxNotes];
	int16_t		lane[kNumLanes][shoal::kMaxSteps];	// a point per step, or kLaneUnset
	uint8_t		interp[kNumLanes];

	void clear()
	{
		count = 0;
		memset( notes, 0, sizeof notes );
		for ( int l = 0; l < kNumLanes; ++l )
		{
			for ( int s = 0; s < shoal::kMaxSteps; ++s )
				lane[l][s] = kLaneUnset;
			interp[l] = kInterpLin;
		}
	}

	// Keep `notes` ordered by start (insertion sort: patterns are small and edits few).
	void sort()
	{
		for ( int i = 1; i < count; ++i )
		{
			Note n = notes[i];
			int j = i - 1;
			while ( j >= 0 && notes[j].start > n.start )
			{
				notes[j + 1] = notes[j];
				--j;
			}
			notes[j + 1] = n;
		}
	}

	// Add a note, keeping the order. False when the pattern is full.
	bool add( const Note& n )
	{
		if ( count >= kMaxNotes )
			return false;
		notes[count++] = n;
		sort();
		return true;
	}

	void remove( int i )
	{
		if ( i < 0 || i >= count )
			return;
		for ( int j = i; j < count - 1; ++j )
			notes[j] = notes[j + 1];
		--count;
	}

	// Drop notes that start at or past `steps` (the pattern was shortened).
	void truncate( int steps )
	{
		int end = steps * kTicksPerStep;
		int n = 0;
		for ( int i = 0; i < count; ++i )
			if ( notes[i].start < end )
				notes[n++] = notes[i];
		count = (uint16_t)n;
	}
};

// The value of `lane` at position x (in steps, fractional) of a pattern `length` steps
// long; kLaneUnset when the lane has no points. Points wrap round the loop.
static inline float laneValueAt( const Pattern& p, int lane, float x, int length )
{
	if ( length < 1 ) return (float)kLaneUnset;
	if ( length > shoal::kMaxSteps ) length = shoal::kMaxSteps;
	int s0 = (int)floorf( x );
	s0 = ( ( s0 % length ) + length ) % length;
	float frac = x - floorf( x );
	const int16_t* L = p.lane[lane];
	// the point at or before s0
	int prev = -1, back = 0;
	for ( int k = 0; k < length; ++k )
	{
		int s = ( s0 - k + length ) % length;
		if ( L[s] != kLaneUnset ) { prev = s; back = k; break; }
	}
	if ( prev < 0 )
		return (float)kLaneUnset;
	float v0 = (float)L[prev];
	if ( p.interp[lane] == kInterpOff )
		return v0;
	// the point after it
	int next = -1, fwd = 0;
	for ( int k = 1; k <= length; ++k )
	{
		int s = ( prev + k ) % length;
		if ( L[s] != kLaneUnset ) { next = s; fwd = k; break; }
	}
	if ( next < 0 || next == prev )
		return v0;
	float v1 = (float)L[next];
	float t = ( (float)back + frac ) / (float)fwd;
	if ( t < 0.0f ) t = 0.0f;
	if ( t > 1.0f ) t = 1.0f;
	switch ( p.interp[lane] )
	{
		case kInterpS:   t = t * t * ( 3.0f - 2.0f * t ); break;
		case kInterpLog: t = 1.0f - ( 1.0f - t ) * ( 1.0f - t ); break;	// fast, then settling
		case kInterpExp: t = t * t; break;								// slow, then rushing
		default: break;
	}
	return v0 + ( v1 - v0 ) * t;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

enum SlotKind { kSlotEmpty, kSlotGen, kSlotPat };

// The track settings a slot carries. Output setup (gate level, pitch scale and
// offset, MIDI channel) and mute belong to the track, not to a slot.
enum { kNumBlock = 15 };
static inline int blockParam( int i, int t )
{
	namespace S = shoal;
	static const int off[kNumBlock - 1] = {
		S::kTLength, S::kTRate, S::kTDirection, S::kTChance, S::kTBreathe, S::kTNote, S::kTOct,
		S::kTEvolve, S::kTGate, S::kTTie, S::kTSlop, S::kTOctave, S::kTTrans, S::kTSeed,
	};
	return i < kNumBlock - 1 ? S::TP( t, off[i] ) : S::XP( t, S::kXShift );
}
// Note: Follow (kTSource) is a track's relationship to the others and stays with the
// track, like mute.

// A slot's own value for one effect parameter (Hermod+'s pattern values).
struct FxOverride { int8_t fx; int8_t param; int16_t value; };
enum { kMaxOverrides = 16 };

struct Slot
{
	uint8_t		kind;
	int16_t		block[kNumBlock];
	Pattern		pat;
	uint8_t		fxMute;				// effects muted while this slot plays (Hermod+'s pattern mute)
	uint8_t		nOverrides;
	FxOverride	ov[kMaxOverrides];

	void clear()
	{
		kind = kSlotEmpty;
		for ( int i = 0; i < kNumBlock; ++i ) block[i] = 0;
		pat.clear();
		fxMute = 0;
		nOverrides = 0;
	}

	int findOverride( int fx, int param ) const
	{
		for ( int i = 0; i < nOverrides; ++i ) if ( ov[i].fx == fx && ov[i].param == param ) return i;
		return -1;
	}
	void setOverride( int fx, int param, int value )
	{
		int i = findOverride( fx, param );
		if ( i < 0 ) { if ( nOverrides >= kMaxOverrides ) return; i = nOverrides++; }
		ov[i].fx = (int8_t)fx; ov[i].param = (int8_t)param; ov[i].value = (int16_t)value;
	}
	void clearOverride( int fx, int param )
	{
		int i = findOverride( fx, param );
		if ( i >= 0 ) ov[i] = ov[--nOverrides];
	}
	// An effect changed type or moved: its slot values no longer mean anything.
	void clearOverrides( int fx )
	{
		for ( int i = 0; i < nOverrides; ) if ( ov[i].fx == fx ) ov[i] = ov[--nOverrides]; else ++i;
	}
};

// Copy a track's settings out of a parameter array into a block, and back.
static inline void storeBlock( int16_t* block, const int16_t* v, int t )
{
	for ( int i = 0; i < kNumBlock; ++i ) block[i] = v[ blockParam( i, t ) ];
}

// The block a fresh slot starts with: the defaults, and the given seed.
static inline void defaultBlock( int16_t* block, int t, int seed )
{
	for ( int i = 0; i < kNumBlock; ++i ) block[i] = shoal::paramRange( blockParam( i, t ) ).def;
	block[13] = (int16_t)seed;
}
enum { kBlockLength = 0, kBlockRate = 1, kBlockChance = 3, kBlockNote = 5, kBlockOct = 6,
	   kBlockEvolve = 7, kBlockGate = 8, kBlockTie = 9, kBlockSeed = 13, kBlockShift = 14 };

// ---------------------------------------------------------------------------
// Capture: a generator's loop, written down
// ---------------------------------------------------------------------------

// Writes what track t of `s` plays at each step 0..length-1 into `out`, as Shoal would
// play it with the track's present settings and evolution: the note, its gate length
// and `vel`. A tie holds into the next step: Shoal's tied gate runs 1.5 steps, so it is
// still high when the next note lands (legato), but a tie into a rest is closed at
// that step. The walk is not baked in: the pattern keeps the track's DIRN, so a
// Forwards loop replays sample for sample as it sounded.
static inline void capture( const shoal::Shoal* s, int t, uint8_t vel, Pattern& out )
{
	namespace S = shoal;
	out.clear();
	int length = s->v[ S::TP( t, S::kTLength ) ];
	int gate = s->v[ S::TP( t, S::kTGate ) ];
	for ( int st = 0; st < length && st < S::kMaxSteps; ++st )
	{
		S::StepEval ev;
		S::evalStep( s, t, st, ev );
		if ( !ev.fires )
			continue;
		Note n;
		n.start = (uint16_t)( st * kTicksPerStep );
		int len = ( kTicksPerStep * gate ) / 100;
		if ( ev.tie )
		{
			S::StepEval next;
			S::evalStep( s, t, ( st + 1 ) % length, next );
			len = next.fires ? kTicksPerStep + kTicksPerStep / 2 : kTicksPerStep;
		}
		n.len = (uint16_t)( len < 1 ? 1 : len );
		n.pitch = (uint8_t)ev.note;
		n.vel = vel;
		out.notes[out.count++] = n;
	}
}

// The block a captured pattern plays with: the generator's, with everything that
// already went into the written notes set back to neutral -- chance, variation,
// evolution, ties and shift -- so the pattern replays what was heard, not a
// further-filtered copy of it. The seed stays, so SLOP still leans the same way.
static inline void captureBlock( int16_t* block )
{
	block[kBlockChance] = 100;
	block[kBlockNote] = 0;
	block[kBlockOct] = 0;
	block[kBlockEvolve] = 0;
	block[kBlockTie] = 0;
	block[kBlockShift] = 0;
}

// ---------------------------------------------------------------------------
// Generator: a random pattern, in key
// ---------------------------------------------------------------------------

struct GenSettings
{
	int		density;		// % of grid positions that get a note
	int		grid;			// ticks between positions
	int		degreesBelow;	// pitch range, in scale degrees around the root
	int		degreesAbove;
	int		lenMin, lenMax;	// ticks
	int		velMin, velMax;

	GenSettings() : density( 50 ), grid( kTicksPerStep ), degreesBelow( 7 ), degreesAbove( 7 ),
		lenMin( kTicksPerStep / 4 ), lenMax( kTicksPerStep ), velMin( 70 ), velMax( 120 ) {}
};

enum GenWhat { kGenAll, kGenPitch, kGenLength, kGenVelocity };

// The MIDI note `degree` scale degrees from `root` in `scale`.
static inline int degreeToNote( int root, int scale, int degree )
{
	uint16_t mask = shoal::scaleMasks[scale];
	int pcs[12], k = 0;
	for ( int i = 0; i < 12; ++i ) if ( mask & ( 1 << i ) ) pcs[k++] = i;
	int q = degree / k, r = degree - q * k;
	if ( r < 0 ) { r += k; q -= 1; }
	int n = root + 12 * q + pcs[r];
	while ( n < 0 ) n += 12;
	while ( n > 127 ) n -= 12;
	return n;
}

// Fill `p` (kGenAll) or re-roll one attribute of the notes already there, from `seed`.
// Pitches are drawn from the scale, so a random pattern is in key from the start.
static inline void generate( Pattern& p, int length, const GenSettings& g, int root, int scale,
							 uint32_t seed, int what )
{
	using shoal::hash3;
	if ( length < 1 ) length = 1;
	int lo = g.degreesBelow < 0 ? 0 : g.degreesBelow, hi = g.degreesAbove < 0 ? 0 : g.degreesAbove;
	int lmin = g.lenMin < 1 ? 1 : g.lenMin, lmax = g.lenMax < lmin ? lmin : g.lenMax;
	int vmin = g.velMin < 1 ? 1 : g.velMin, vmax = g.velMax > 127 ? 127 : g.velMax;
	if ( vmax < vmin ) vmax = vmin;
	int grid = g.grid < 1 ? 1 : g.grid;
	if ( what == kGenAll )
	{
		p.count = 0;					// the lanes are left as they are
		int end = length * kTicksPerStep;
		for ( int pos = 0, i = 0; pos < end && p.count < kMaxNotes; pos += grid, ++i )
		{
			if ( (int)( hash3( seed, (uint32_t)i, 0xD3E5u ) % 100 ) >= g.density )
				continue;
			Note n;
			n.start = (uint16_t)pos;
			n.pitch = (uint8_t)degreeToNote( root, scale,
				-lo + (int)( hash3( seed, (uint32_t)i, 0x917Cu ) % (uint32_t)( lo + hi + 1 ) ) );
			n.len = (uint16_t)( lmin + (int)( hash3( seed, (uint32_t)i, 0x1E2Bu ) % (uint32_t)( lmax - lmin + 1 ) ) );
			n.vel = (uint8_t)( vmin + (int)( hash3( seed, (uint32_t)i, 0x7E10u ) % (uint32_t)( vmax - vmin + 1 ) ) );
			p.notes[p.count++] = n;
		}
		return;
	}
	for ( int i = 0; i < p.count; ++i )
	{
		Note& n = p.notes[i];
		if ( what == kGenPitch )
			n.pitch = (uint8_t)degreeToNote( root, scale,
				-lo + (int)( hash3( seed, (uint32_t)i, 0x917Cu ) % (uint32_t)( lo + hi + 1 ) ) );
		else if ( what == kGenLength )
			n.len = (uint16_t)( lmin + (int)( hash3( seed, (uint32_t)i, 0x1E2Bu ) % (uint32_t)( lmax - lmin + 1 ) ) );
		else if ( what == kGenVelocity )
			n.vel = (uint8_t)( vmin + (int)( hash3( seed, (uint32_t)i, 0x7E10u ) % (uint32_t)( vmax - vmin + 1 ) ) );
	}
}

}
