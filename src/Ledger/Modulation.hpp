#pragma once
// Ledger's modulation and output stage, around the Shoal engine and never inside it.
//
// What moves a track's settings, other than its knobs:
//  - the CV jack under each knob. Polyphonic: channel n is track n, a mono cable moves
//    every track. Additive and clamped, the way Shoal's own expander is: a unipolar
//    setting takes 0-10 V across its whole range, a bipolar one ±5 V, so one volt is a
//    tenth of the range either way. ROOT's jack is the exception -- absolute, 1 V/oct
//    with 0 V = C -- so a keyboard changes key.
//  - a mod matrix: four slots per track, each routing one of CV A-D to one of the
//    track's settings through an attenuverter, a polarity and an offset (Hermod+'s
//    modMatrix). Also additive, so the knob still works underneath.
//
// The engine only ever sees the result. Its parameter array holds the *effective*
// value; the set value stays in Ledger's own copy, which is what knobs show and
// patches save. Nothing here touches Shoal's sequencing, so the golden test's claim
// is unaffected: an unmodulated Ledger is Shoal.
//
// Pure C++11, no Rack types.
#include <math.h>
#include <stdint.h>
#include "Shoal.hpp"

namespace ledger {

// The track settings a CV can move, in panel order.
enum ModDest {
	kDChance, kDNote, kDOct, kDLength, kDRate, kDDirection, kDTrans, kDShift, kDOctave,
	kDEvolve, kDBreathe, kDGate, kDTie, kDSlop,
	kNumDests
};

static const char* const destNames[kNumDests] = {
	"Chance", "Note", "Octave leaps", "Length", "Rate", "Direction", "Transpose", "Shift",
	"Octave", "Evolve", "Breathe", "Gate length", "Tie", "Slop",
};

// The engine parameter a destination moves, for track t.
static inline int destParam( int d, int t )
{
	namespace S = shoal;
	switch ( d )
	{
		case kDChance:    return S::TP( t, S::kTChance );
		case kDNote:      return S::TP( t, S::kTNote );
		case kDOct:       return S::TP( t, S::kTOct );
		case kDLength:    return S::TP( t, S::kTLength );
		case kDRate:      return S::TP( t, S::kTRate );
		case kDDirection: return S::TP( t, S::kTDirection );
		case kDTrans:     return S::TP( t, S::kTTrans );
		case kDShift:     return S::XP( t, S::kXShift );
		case kDOctave:    return S::TP( t, S::kTOctave );
		case kDEvolve:    return S::TP( t, S::kTEvolve );
		case kDBreathe:   return S::TP( t, S::kTBreathe );
		case kDGate:      return S::TP( t, S::kTGate );
		case kDTie:       return S::TP( t, S::kTTie );
		default:          return S::TP( t, S::kTSlop );
	}
}

// How far a value moves for a CV: a tenth of the parameter's range per volt, so
// 0-10 V spans a unipolar setting and ±5 V a bipolar one.
static inline float cvSpan( int p )
{
	shoal::ParamRange r = shoal::paramRange( p );
	return (float)( r.max - r.min ) * 0.1f;
}

// base + delta, rounded and kept in range.
static inline int16_t applyDelta( int p, int base, float delta )
{
	shoal::ParamRange r = shoal::paramRange( p );
	int v = base + (int)lroundf( delta );
	if ( v < r.min ) v = r.min;
	if ( v > r.max ) v = r.max;
	return (int16_t)v;
}

// ROOT's jack: absolute 1 V/oct, 0 V = C. The pitch class it names, 0..11.
static inline int rootFromVolts( float volts )
{
	int semis = (int)lroundf( volts * 12.0f );
	int pc = semis % 12;
	return pc < 0 ? pc + 12 : pc;
}

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

// Sources: CV A-D, the track's own MOD 1-4 lanes, and a MIDI CC the track receives
// (both 0..127 read as 0..10 V). A slot whose source is the CC names which one.
enum { kNumMatrixSlots = 4, kNumCvSources = 4, kNumMatrixSources = 9, kSrcCC = 9 };
static const char* const sourceNames[kNumMatrixSources + 1] = {
	"None", "CV A", "CV B", "CV C", "CV D", "MOD 1 lane", "MOD 2 lane", "MOD 3 lane", "MOD 4 lane", "MIDI CC",
};

// Destinations past kNumDests are effect parameters: kNumDests + slot * kFxParamStride + param.
enum { kFxParamStride = 10 };
static inline bool destIsFx( int d ) { return d >= kNumDests; }
static inline int destFxSlot( int d ) { return ( d - kNumDests ) / kFxParamStride; }
static inline int destFxParam( int d ) { return ( d - kNumDests ) % kFxParamStride; }
static inline int fxDest( int slot, int param ) { return kNumDests + slot * kFxParamStride + param; }

struct MatrixSlot
{
	int8_t	source;		// 0 = none, 1..4 = CV A..D, 5..8 = MOD 1..4 lanes, 9 = MIDI CC
	int8_t	dest;		// ModDest
	int16_t	amount;		// attenuverter, -100..100 %
	bool	unipolar;	// false: ±5 V is -1..1; true: 0..10 V is 0..1 ("increase only")
	int16_t	offset;		// -100..100 % of the destination's range, added while the slot is live
	int8_t	cc;			// source 9: which CC, 0..119

	MatrixSlot() : source( 0 ), dest( kDChance ), amount( 100 ), unipolar( false ), offset( 0 ), cc( 1 ) {}
	bool live() const { return source > 0 && source <= kNumMatrixSources; }
};

// A 0..127 source (a lane, a CC) as volts for a slot: increase-only reads it 0..10 V,
// bipolar ±5 V about its middle, the way pitch bend is read: 64 is no change, 0 and
// 127 the two ends.
static inline float midiVolts( int value, bool unipolar )
{
	if ( unipolar ) return value * ( 10.0f / 127.0f );
	return value >= 64 ? ( value - 64 ) * ( 5.0f / 63.0f ) : ( value - 64 ) * ( 5.0f / 64.0f );
}

// What one slot adds to a destination whose range is `span` wide, for `volts` on its source.
static inline float slotDeltaSpan( const MatrixSlot& s, float span, float volts )
{
	if ( !s.live() )
		return 0.0f;
	float x;
	if ( s.unipolar )
		x = volts * 0.1f;
	else
		x = volts * 0.2f;
	if ( x > 1.0f ) x = 1.0f;
	if ( x < ( s.unipolar ? 0.0f : -1.0f ) ) x = s.unipolar ? 0.0f : -1.0f;
	return ( x * s.amount + s.offset ) * 0.01f * span;
}

// ...for one of the track's own settings.
static inline float slotDelta( const MatrixSlot& s, int track, float volts )
{
	if ( !s.live() || destIsFx( s.dest ) )
		return 0.0f;
	shoal::ParamRange r = shoal::paramRange( destParam( s.dest, track ) );
	return slotDeltaSpan( s, (float)( r.max - r.min ), volts );
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// Pitch standards (Hermod+'s OUTPUT STANDARD). Applied to a track's pitch after
// Shoal's own scale and offset.
enum OutStd { kStdVOct, kStd12VOct, kStdHzV, kNumOutStds };
static const char* const outStdNames[kNumOutStds] = {
	"1 V/oct", "1.2 V/oct (Buchla)", "Hz/V (Korg, Yamaha)",
};

// `volts` is 1 V/oct with 0 V = C3. Hz/V doubles per octave and gives 1 V at C3,
// so C3 is 1 V, C4 2 V, C6 8 V; held at 10 V above about D#6.
static inline float applyOutStd( int std, float volts )
{
	switch ( std )
	{
		case kStd12VOct: return volts * 1.2f;
		case kStdHzV:
		{
			float v = exp2f( volts );
			return v > 10.0f ? 10.0f : v;
		}
		default: return volts;
	}
}

// Shoal's poly output routing: which hub jacks carry several tracks. The mono
// jacks keep working; a hub jack's channel c is track firstTrack + c.
enum PolyHub { kHubOff, kHubPairs, kHubSplit, kHubAll, kNumHubs };
static const char* const hubNames[kNumHubs] = {
	"Off", "Pairs (jacks 2, 4, 6, 8)", "Split 4+4 (jacks 4, 8)", "All 8 (jack 8)",
};

struct HubSpan { int firstTrack; int channels; };

// For output jack j (0..7) under `mode`: the tracks it carries. A jack that is not a
// hub carries its own track alone.
static inline HubSpan hubSpan( int mode, int j )
{
	HubSpan h = { j, 1 };
	switch ( mode )
	{
		case kHubPairs: if ( j % 2 == 1 ) { h.firstTrack = j - 1; h.channels = 2; } break;
		case kHubSplit: if ( j == 3 || j == 7 ) { h.firstTrack = j - 3; h.channels = 4; } break;
		case kHubAll:   if ( j == 7 ) { h.firstTrack = 0; h.channels = 8; } break;
		default: break;
	}
	return h;
}

}
