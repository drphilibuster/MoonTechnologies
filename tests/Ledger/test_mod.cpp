// Ledger's modulation and output stage (src/Ledger/Modulation.hpp): what a volt does
// to a setting, the mod matrix, ROOT's absolute input, the pitch standards and the
// poly hubs.
#include <cmath>
#include <cstdio>
#include <set>
#include "../../src/Ledger/Modulation.hpp"

namespace S = ledger::shoal;
namespace L = ledger;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )
static bool near( float a, float b, float tol = 1e-4f ) { return std::fabs( a - b ) <= tol; }

static void testDestinations()
{
	// every destination of every track is its own engine parameter, inside the track block
	std::set<int> seen;
	for ( int t = 0; t < S::kNumTracks; ++t )
		for ( int d = 0; d < L::kNumDests; ++d )
		{
			int p = L::destParam( d, t );
			CHECK( p >= S::kTrackBase && p < S::kNumParameters, "dest %d track %d -> %d", d, t, p );
			CHECK( seen.insert( p ).second, "dest %d track %d shares parameter %d", d, t, p );
		}
	// and they are the ones the names say
	CHECK( L::destParam( L::kDChance, 3 ) == S::TP( 3, S::kTChance ), "chance" );
	CHECK( L::destParam( L::kDShift, 5 ) == S::XP( 5, S::kXShift ), "shift lives in the extras block" );
	CHECK( L::destParam( L::kDOctave, 0 ) == S::TP( 0, S::kTOctave ), "OCTA is the fixed octave" );
	CHECK( L::destParam( L::kDOct, 0 ) == S::TP( 0, S::kTOct ), "OCTAVE ± is the random one" );
}

static void testSpans()
{
	// a tenth of the range per volt: 0-10 V spans a unipolar setting, ±5 V a bipolar one
	CHECK( near( L::cvSpan( S::TP( 0, S::kTChance ) ), 10.0f ), "chance %g/V", L::cvSpan( S::TP( 0, S::kTChance ) ) );
	CHECK( near( L::cvSpan( S::TP( 0, S::kTNote ) ), 20.0f ), "note %g/V", L::cvSpan( S::TP( 0, S::kTNote ) ) );
	CHECK( near( L::cvSpan( S::TP( 0, S::kTTrans ) ), 1.4f ), "transpose %g/V", L::cvSpan( S::TP( 0, S::kTTrans ) ) );
	CHECK( near( L::cvSpan( S::XP( 0, S::kXShift ) ), 12.6f ), "shift %g/V", L::cvSpan( S::XP( 0, S::kXShift ) ) );
	CHECK( near( L::cvSpan( S::TP( 0, S::kTGate ) ), 9.5f ), "gate %g/V", L::cvSpan( S::TP( 0, S::kTGate ) ) );

	int chance = S::TP( 0, S::kTChance );
	CHECK( L::applyDelta( chance, 0, 10.0f * L::cvSpan( chance ) ) == 100, "0 + 10 V = 100%%" );
	CHECK( L::applyDelta( chance, 50, 2.0f * L::cvSpan( chance ) ) == 70, "50 + 2 V = 70%%" );
	CHECK( L::applyDelta( chance, 90, 5.0f * L::cvSpan( chance ) ) == 100, "clamped high" );
	CHECK( L::applyDelta( chance, 10, -5.0f * L::cvSpan( chance ) ) == 0, "clamped low" );
	int note = S::TP( 0, S::kTNote );
	CHECK( L::applyDelta( note, 0, -5.0f * L::cvSpan( note ) ) == -100, "note 0 - 5 V = -100" );
	CHECK( L::applyDelta( note, 0, 5.0f * L::cvSpan( note ) ) == 100, "note 0 + 5 V = +100" );
	int gate = S::TP( 0, S::kTGate );
	CHECK( L::applyDelta( gate, 50, -10.0f ) == 40, "gate rounds and adds" );
	CHECK( L::applyDelta( gate, 50, -100.0f ) == 5, "gate floor is 5%%" );
	int len = S::TP( 0, S::kTLength );
	CHECK( L::applyDelta( len, 16, -100.0f ) == 1, "length floor is 1" );
	int rate = S::TP( 0, S::kTRate );
	CHECK( L::applyDelta( rate, S::kRateX1, 1.0f * L::cvSpan( rate ) ) == S::kRateX1 + 3, "rate +1 V = 2.8 rate steps -> 3" );
	CHECK( L::applyDelta( chance, 50, 0.49f ) == 50 && L::applyDelta( chance, 50, 0.51f ) == 51, "rounds to nearest" );
}

static void testRoot()
{
	CHECK( L::rootFromVolts( 0.0f ) == 0, "0 V = C" );
	CHECK( L::rootFromVolts( 7.0f / 12.0f ) == 7, "7/12 V = G" );
	CHECK( L::rootFromVolts( 1.0f ) == 0, "1 V = C again" );
	CHECK( L::rootFromVolts( 1.0f + 2.0f / 12.0f ) == 2, "D an octave up" );
	CHECK( L::rootFromVolts( -1.0f / 12.0f ) == 11, "below C is B" );
	CHECK( L::rootFromVolts( -2.0f + 4.0f / 12.0f ) == 4, "E two octaves down" );
	CHECK( L::rootFromVolts( 0.04f ) == 0 && L::rootFromVolts( 0.05f ) == 1, "nearest semitone" );
}

static void testMatrix()
{
	L::MatrixSlot s;
	CHECK( !s.live(), "a new slot is off" );
	CHECK( L::slotDelta( s, 0, 5.0f ) == 0.0f, "an off slot adds nothing" );

	s.source = 1;
	s.dest = L::kDNote;			// span 200
	s.amount = 100;
	CHECK( near( L::slotDelta( s, 0, 5.0f ), 200.0f ), "bipolar +5 V at 100%% = full span (%g)", L::slotDelta( s, 0, 5.0f ) );
	CHECK( near( L::slotDelta( s, 0, -5.0f ), -200.0f ), "bipolar -5 V = minus full span" );
	CHECK( near( L::slotDelta( s, 0, 9.0f ), 200.0f ), "bipolar clamps beyond 5 V" );
	s.amount = -50;
	CHECK( near( L::slotDelta( s, 0, 2.5f ), -50.0f ), "attenuverted: 0.5 x -50%% x 200 = -50 (%g)", L::slotDelta( s, 0, 2.5f ) );

	s.unipolar = true;
	s.amount = 100;
	s.dest = L::kDChance;		// span 100
	CHECK( near( L::slotDelta( s, 0, 10.0f ), 100.0f ), "unipolar 10 V = full span" );
	CHECK( near( L::slotDelta( s, 0, 5.0f ), 50.0f ), "unipolar 5 V = half" );
	CHECK( near( L::slotDelta( s, 0, -3.0f ), 0.0f ), "increase-only never decreases" );

	s.offset = 25;
	CHECK( near( L::slotDelta( s, 0, 0.0f ), 25.0f ), "offset is a share of the range" );
	s.source = 0;
	CHECK( L::slotDelta( s, 0, 0.0f ) == 0.0f, "offset needs a live slot" );

	// per track: the same slot on another track moves that track's parameter span
	s.source = 2; s.offset = 0; s.unipolar = false; s.amount = 100; s.dest = L::kDTrans;
	CHECK( near( L::slotDelta( s, 6, 5.0f ), 14.0f ), "transpose full span is 14 (%g)", L::slotDelta( s, 6, 5.0f ) );
}

static void testOutStd()
{
	CHECK( L::applyOutStd( L::kStdVOct, 1.25f ) == 1.25f, "V/oct is untouched" );
	CHECK( near( L::applyOutStd( L::kStd12VOct, 1.0f ), 1.2f ), "1.2 V/oct" );
	CHECK( near( L::applyOutStd( L::kStdHzV, 0.0f ), 1.0f ), "Hz/V: C3 = 1 V" );
	CHECK( near( L::applyOutStd( L::kStdHzV, 1.0f ), 2.0f ), "Hz/V doubles per octave" );
	CHECK( near( L::applyOutStd( L::kStdHzV, -1.0f ), 0.5f ), "Hz/V halves going down" );
	CHECK( near( L::applyOutStd( L::kStdHzV, 7.0f / 12.0f ), std::pow( 2.0f, 7.0f / 12.0f ) ), "Hz/V a fifth up" );
	CHECK( L::applyOutStd( L::kStdHzV, 5.0f ) == 10.0f, "Hz/V held at 10 V" );
}

static void testHubs()
{
	// Off: every jack its own track
	for ( int j = 0; j < 8; ++j )
	{
		L::HubSpan h = L::hubSpan( L::kHubOff, j );
		CHECK( h.firstTrack == j && h.channels == 1, "off jack %d", j );
	}
	// the hubs carry their group, in track order; the rest stay mono
	struct Want { int mode, jack, first, n; };
	const Want w[] = {
		{ L::kHubPairs, 0, 0, 1 }, { L::kHubPairs, 1, 0, 2 }, { L::kHubPairs, 3, 2, 2 },
		{ L::kHubPairs, 5, 4, 2 }, { L::kHubPairs, 7, 6, 2 }, { L::kHubPairs, 6, 6, 1 },
		{ L::kHubSplit, 3, 0, 4 }, { L::kHubSplit, 7, 4, 4 }, { L::kHubSplit, 1, 1, 1 },
		{ L::kHubAll, 7, 0, 8 }, { L::kHubAll, 3, 3, 1 }, { L::kHubAll, 0, 0, 1 },
	};
	for ( const Want& x : w )
	{
		L::HubSpan h = L::hubSpan( x.mode, x.jack );
		CHECK( h.firstTrack == x.first && h.channels == x.n, "mode %d jack %d: %d+%d, want %d+%d",
			   x.mode, x.jack, h.firstTrack, h.channels, x.first, x.n );
	}
	// every hub's channels stay inside the eight tracks
	for ( int m = 0; m < L::kNumHubs; ++m )
		for ( int j = 0; j < 8; ++j )
		{
			L::HubSpan h = L::hubSpan( m, j );
			CHECK( h.firstTrack >= 0 && h.firstTrack + h.channels <= 8, "mode %d jack %d overruns", m, j );
		}
}

// The engine plays what it is handed: a track whose CHANCE is modulated to 0 is silent,
// while its set value is untouched.
static void testEngineSeesEffective()
{
	S::Shoal s;
	S::construct( &s, 48000 );
	int16_t base[S::kNumParameters];
	for ( int p = 0; p < S::kNumParameters; ++p ) base[p] = s.v[p] = S::paramRange( p ).def;
	S::rebuildScale( s.dtc, 0 );
	s.dtc->tracks[0].activeSeed = 7;
	int p = L::destParam( L::kDChance, 0 );
	s.v[p] = L::applyDelta( p, base[p], -10.0f * L::cvSpan( p ) );
	int fired = 0;
	for ( int st = 0; st < 16; ++st )
	{
		S::StepEval ev;
		S::evalStep( &s, 0, st, ev );
		fired += ev.fires;
	}
	CHECK( fired == 0 && base[p] == 100, "modulated to 0 fires %d (set value %d)", fired, base[p] );
}

// A lane or CC (0..127) read by a slot: bipolar centres on 64 and reaches both ends.
static void testMidiVolts()
{
	CHECK( L::midiVolts( 64, false ) == 0.f && L::midiVolts( 127, false ) == 5.f && L::midiVolts( 0, false ) == -5.f,
		   "bipolar: 64 is 0 V, 127 +5 V, 0 -5 V" );
	CHECK( L::midiVolts( 0, true ) == 0.f && std::fabs( L::midiVolts( 127, true ) - 10.f ) < 1e-5f, "increase only: 0..10 V" );
	L::MatrixSlot s;
	s.source = L::kSrcCC;
	s.amount = 100;
	float top = L::slotDelta( s, 0, L::midiVolts( 127, false ) ), mid = L::slotDelta( s, 0, L::midiVolts( 64, false ) );
	CHECK( std::fabs( top - 100.f ) < 1e-3f && mid == 0.f, "a CC at the top moves Chance its whole range; at 64, not at all (%g %g)", top, mid );
}

int main()
{
	testDestinations();
	testSpans();
	testRoot();
	testMatrix();
	testOutStd();
	testHubs();
	testEngineSeesEffective();
	testMidiVolts();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
