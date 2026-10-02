// Ledger's generator engine against the original Shoal, frame by frame.
//
// src/Ledger/Shoal.hpp claims to be shoal.cpp's sequencing code with the NT host
// swapped out. This holds it to that: both engines get identical parameters,
// inputs and gestures, and every output on every frame -- 8 gates, 8 pitches,
// 8 Currents, 8 EOS, clock out -- plus every MIDI byte must match exactly.
//
// The scenarios are seeded fuzz over everything the engine reacts to: all 15
// directions, all 29 rates, all 13 scales, Follow chains (cycles included),
// Shift, Tide, Evolve, Breathe, Weight, Slop, Tie, Freeze in both clock modes,
// solo and mute, armed reseeds, reseed-all from the parameter and from the
// trigger input, reset, external / internal / MIDI clock, and MIDI note out.
// Plus a few hand-checked anchors so a bug common to both copies (a bad vendor
// update) still has something to trip on.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "upstream.hpp"
#include "../../src/Ledger/Shoal.hpp"

namespace S = ledger::shoal;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )

struct Rng
{
	uint32_t s;
	explicit Rng( uint32_t seed ) : s( seed * 2654435761u + 12345u ) {}
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	int range( int lo, int hi ) { return lo + (int)( next() % (uint32_t)( hi - lo + 1 ) ); }
	bool chance( int pct ) { return (int)( next() % 100 ) < pct; }
};

// The port's host: the same contract upstream.cpp gives the original.
struct PortHost : S::Host
{
	std::vector<upstream::MidiMsg> midi;
	uint32_t frame = 0;
	void setParameterFromAudio( S::Shoal* s, int p, int16_t value ) override
	{
		s->v[p] = value;
		S::parameterChanged( s, p );
	}
	void sendMidi3( uint32_t dest, uint8_t b0, uint8_t b1, uint8_t b2 ) override
	{
		upstream::MidiMsg m = { dest, b0, b1, b2, frame };
		midi.push_back( m );
	}
};

struct Pair
{
	upstream::Instance* up;
	S::Shoal port;
	PortHost host;

	Pair()
	{
		up = upstream::create();
		port.host = &host;
		port.pulseVolts = 5.0f;
		S::construct( &port, upstream::sampleRate() );
		for ( int p = 0; p < S::kNumParameters; ++p )
			port.v[p] = upstream::get( up, p );		// same values, routing included
		for ( int p = 0; p < S::kNumParameters; ++p )
			S::parameterChanged( &port, p );
	}
	~Pair() { upstream::destroy( up ); }

	void set( int p, int16_t value )
	{
		upstream::set( up, p, value );
		port.v[p] = value;
		S::parameterChanged( &port, p );
	}
	void solo( int t, bool on ) { upstream::setSolo( up, t, on ); port.solo[t] = on; }
	void midiRt( uint8_t b ) { upstream::midiRealtime( up, b ); S::midiRealtime( &port, b ); }
};

// One random parameter edit of the kinds a player (or a CV mapping) makes.
static void randomEdit( Pair& pr, Rng& rng, bool allowFreeze )
{
	int t = rng.range( 0, S::kNumTracks - 1 );
	switch ( rng.range( 0, 27 ) )
	{
	case 0:  pr.set( S::TP( t, S::kTLength ), (int16_t)rng.range( 1, 64 ) ); break;
	case 1:  pr.set( S::TP( t, S::kTRate ), (int16_t)rng.range( 0, S::kNumRates - 1 ) ); break;
	case 2:  pr.set( S::TP( t, S::kTDirection ), (int16_t)rng.range( 0, S::kNumDirections - 1 ) ); break;
	case 3:  pr.set( S::TP( t, S::kTChance ), (int16_t)rng.range( 0, 100 ) ); break;
	case 4:  pr.set( S::TP( t, S::kTBreathe ), (int16_t)( rng.chance( 50 ) ? 0 : rng.range( 0, 100 ) ) ); break;
	case 5:  pr.set( S::TP( t, S::kTNote ), (int16_t)rng.range( -100, 100 ) ); break;
	case 6:  pr.set( S::TP( t, S::kTOct ), (int16_t)rng.range( -100, 100 ) ); break;
	case 7:  pr.set( S::TP( t, S::kTEvolve ), (int16_t)rng.range( 0, 100 ) ); break;
	case 8:  pr.set( S::TP( t, S::kTGate ), (int16_t)rng.range( 5, 95 ) ); break;
	case 9:  pr.set( S::TP( t, S::kTTie ), (int16_t)rng.range( 0, 100 ) ); break;
	case 10: pr.set( S::TP( t, S::kTSlop ), (int16_t)rng.range( 0, 100 ) ); break;
	case 11: pr.set( S::TP( t, S::kTOctave ), (int16_t)rng.range( -3, 3 ) ); break;
	case 12: pr.set( S::TP( t, S::kTTrans ), (int16_t)rng.range( -7, 7 ) ); break;
	case 13: pr.set( S::TP( t, S::kTSource ), (int16_t)rng.range( 0, 8 ) ); break;
	case 14: pr.set( S::TP( t, S::kTMute ), (int16_t)rng.range( 0, 1 ) ); break;
	case 15: pr.set( S::TP( t, S::kTSeed ), (int16_t)rng.range( 0, 999 ) ); break;
	case 16: pr.set( S::XP( t, S::kXShift ), (int16_t)rng.range( -63, 63 ) ); break;
	case 17: pr.set( S::XP( t, S::kXGateVolts ), (int16_t)rng.range( 1, 10 ) ); break;
	case 18: pr.set( S::XP( t, S::kXPitchScale ), (int16_t)rng.range( 5, 200 ) ); break;
	case 19: pr.set( S::XP( t, S::kXPitchOffset ), (int16_t)rng.range( -100, 100 ) ); break;
	case 20: pr.set( S::MP( t ), (int16_t)rng.range( 0, 16 ) ); break;
	case 21: pr.set( S::kGScale, (int16_t)rng.range( 0, S::kNumScales - 1 ) ); break;
	case 22: pr.set( S::kGRoot, (int16_t)rng.range( 0, 127 ) ); break;
	case 23: pr.set( S::kGWeight, (int16_t)rng.range( 0, 100 ) ); break;
	case 24: pr.set( S::kGReseedAll, (int16_t)rng.range( 0, 999 ) ); break;
	case 25: pr.solo( t, rng.chance( 30 ) ); break;
	case 26:
		if ( allowFreeze ) pr.set( S::kGFreeze, (int16_t)rng.chance( 40 ) );
		else pr.set( S::kGFreezeClock, (int16_t)rng.range( 0, 1 ) );
		break;
	case 27: pr.set( S::kGMidiDest, (int16_t)rng.range( 0, 15 ) ); break;
	}
}

static void randomise( Pair& pr, Rng& rng )
{
	for ( int i = 0; i < 60; ++i )
		randomEdit( pr, rng, false );
	// make the scenario busy enough to mean something
	for ( int t = 0; t < S::kNumTracks; ++t )
		if ( rng.chance( 50 ) ) pr.set( S::MP( t ), (int16_t)rng.range( 1, 16 ) );
}

enum ClockKind { kExternal, kInternal, kMidi };

// Run one scenario; returns false (after reporting) at the first mismatch.
static bool runScenario( uint32_t seed, int frames, bool verbose )
{
	Rng rng( seed );
	Pair pr;
	randomise( pr, rng );

	ClockKind ck = (ClockKind)rng.range( 0, 2 );
	if ( rng.chance( 15 ) ) ck = kInternal;
	pr.set( S::kGClockSource, (int16_t)( ck == kExternal ? 0 : ck == kInternal ? 1 : 2 ) );
	pr.set( S::kGBPM, (int16_t)rng.range( 200, 300 ) );
	int clkPeriod = rng.range( 24, 1200 );			// external clock, samples
	int midiPeriod = rng.range( 2, 60 );			// samples per 0xF8
	if ( ck == kMidi ) pr.midiRt( 0xFA );

	const int B = 4;								// the NT's frame quantum
	const int nb = upstream::kNumBusses;
	std::vector<float> bus( nb * B );
	std::vector<float> pitch( S::kNumTracks * B ), gate( S::kNumTracks * B ),
		cur( S::kNumTracks * B ), eos( S::kNumTracks * B ), clk( B );
	std::vector<float> clkIn( B ), rstIn( B ), rsdIn( B );

	S::Io io;
	std::memset( &io, 0, sizeof io );
	io.clockIn = clkIn.data();
	io.resetIn = rstIn.data();
	io.reseedIn = rsdIn.data();
	io.clkOut = clk.data();
	for ( int t = 0; t < S::kNumTracks; ++t )
	{
		io.pitchOut[t] = &pitch[t * B];
		io.gateOut[t] = &gate[t * B];
		io.currentOut[t] = &cur[t * B];
		io.eosOut[t] = &eos[t * B];
	}

	int rstLeft = 0, rsdLeft = 0, midiCount = 0;
	for ( int f0 = 0; f0 < frames; f0 += B )
	{
		// gestures between blocks
		if ( rng.chance( 2 ) ) randomEdit( pr, rng, true );
		if ( rng.chance( 1 ) && pr.port.v[S::kGFreeze] && rng.chance( 50 ) ) pr.set( S::kGFreeze, 0 );
		if ( rng.chance( 1 ) ) rstLeft = rng.range( 1, 40 );
		if ( rng.range( 0, 999 ) < 3 ) rsdLeft = rng.range( 1, 40 );
		if ( ck == kMidi )
		{
			if ( ++midiCount >= midiPeriod ) { midiCount = 0; pr.midiRt( 0xF8 ); }
			if ( rng.range( 0, 3999 ) == 0 ) pr.midiRt( 0xFC );
			if ( rng.range( 0, 1999 ) == 0 ) pr.midiRt( rng.chance( 50 ) ? 0xFA : 0xFB );
		}
		if ( rng.range( 0, 4999 ) == 0 ) pr.set( S::kGRun, (int16_t)rng.chance( 80 ) );

		for ( int i = 0; i < B; ++i )
		{
			int f = f0 + i;
			clkIn[i] = ( ck == kExternal && ( f % clkPeriod ) < clkPeriod / 2 ) ? 5.0f : 0.0f;
			rstIn[i] = rstLeft > 0 ? 5.0f : 0.0f;
			rsdIn[i] = rsdLeft > 0 ? 5.0f : 0.0f;
			if ( rstLeft > 0 ) --rstLeft;
			if ( rsdLeft > 0 ) --rsdLeft;
		}
		std::fill( bus.begin(), bus.end(), -99.0f );
		std::memcpy( &bus[( upstream::kBusClock - 1 ) * B], clkIn.data(), B * sizeof( float ) );
		std::memcpy( &bus[( upstream::kBusReset - 1 ) * B], rstIn.data(), B * sizeof( float ) );
		std::memcpy( &bus[( upstream::kBusReseed - 1 ) * B], rsdIn.data(), B * sizeof( float ) );
		// a frozen "Stops" clock out leaves the bus alone after zeroing it once;
		// prime both sides alike so untouched frames compare equal
		std::fill( clk.begin(), clk.end(), -99.0f );
		std::fill( pitch.begin(), pitch.end(), -99.0f );
		std::fill( gate.begin(), gate.end(), -99.0f );
		std::fill( cur.begin(), cur.end(), -99.0f );
		std::fill( eos.begin(), eos.end(), -99.0f );

		upstream::step( pr.up, bus.data(), B );
		pr.host.frame = (uint32_t)f0;
		S::step( &pr.port, io, B );
		pr.host.frame = (uint32_t)( f0 + B );	// between blocks, as upstream.cpp counts

		for ( int i = 0; i < B; ++i )
		{
			float uc = bus[( upstream::kBusClkOut - 1 ) * B + i];
			if ( uc != clk[i] )
			{
				std::printf( "FAIL scenario %u frame %d: clock out %g vs %g\n", seed, f0 + i, uc, clk[i] );
				return false;
			}
			for ( int t = 0; t < S::kNumTracks; ++t )
			{
				float ug = bus[( upstream::kBusGate0 + t - 1 ) * B + i];
				float up = bus[( upstream::kBusPitch0 + t - 1 ) * B + i];
				float uk = bus[( upstream::kBusCurrent0 + t - 1 ) * B + i];
				float ue = bus[( upstream::kBusEos0 + t - 1 ) * B + i];
				if ( ug != gate[t * B + i] || up != pitch[t * B + i]
						|| uk != cur[t * B + i] || ue != eos[t * B + i] )
				{
					std::printf( "FAIL scenario %u frame %d track %d: gate %g/%g pitch %g/%g "
								 "current %g/%g eos %g/%g\n", seed, f0 + i, t,
								 ug, gate[t * B + i], up, pitch[t * B + i],
								 uk, cur[t * B + i], ue, eos[t * B + i] );
					return false;
				}
			}
		}
		std::vector<upstream::MidiMsg>& um = upstream::midiLog( pr.up );
		if ( um.size() != pr.host.midi.size() )
		{
			std::printf( "FAIL scenario %u frame %d: %zu MIDI messages vs %zu\n",
						 seed, f0, um.size(), pr.host.midi.size() );
			return false;
		}
		for ( size_t k = 0; k < um.size(); ++k )
		{
			const upstream::MidiMsg& a = um[k];
			const upstream::MidiMsg& b = pr.host.midi[k];
			if ( a.dest != b.dest || a.b0 != b.b0 || a.b1 != b.b1 || a.b2 != b.b2 || a.frame != b.frame )
			{
				std::printf( "FAIL scenario %u: MIDI #%zu %02X %02X %02X @%u vs %02X %02X %02X @%u\n",
							 seed, k, a.b0, a.b1, a.b2, a.frame, b.b0, b.b1, b.b2, b.frame );
				return false;
			}
		}
		if ( verbose && f0 == frames - B )
			std::printf( "  scenario %u: %zu MIDI messages, clock kind %d\n", seed, um.size(), (int)ck );
	}
	return true;
}

// ---- anchors: facts about Shoal checked against the port alone ----

static void testAnchors()
{
	// hash3 is the documented avalanche hash, not some other one
	uint32_t h = S::hash3( 1, 2, 3 );
	uint32_t e = 1u * 0x9E3779B1u ^ 2u * 0x85EBCA77u ^ 3u * 0xC2B2AE3Du;
	e ^= e >> 16; e *= 0x7FEB352Du; e ^= e >> 15; e *= 0x846CA68Bu; e ^= e >> 16;
	CHECK( h == e, "hash3(1,2,3) = %08X, want %08X", h, e );

	// 29 rates, ascending, x1 at index 13
	CHECK( S::rateMult[S::kRateX1] == 1 && S::rateDiv[S::kRateX1] == 1, "x1 is not 1/1" );
	for ( int r = 1; r < S::kNumRates; ++r )
		CHECK( (double)S::rateMult[r] / S::rateDiv[r] > (double)S::rateMult[r - 1] / S::rateDiv[r - 1],
			   "rate %d not ascending", r );

	// Bresenham: x1.5 gives 1,2,1,2; /1.5 gives 0,1,1 (README)
	{
		int num = 3, den = 2, want[4] = { 1, 2, 1, 2 };
		for ( uint32_t t = 0; t < 4; ++t )
			CHECK( (int)( ( ( t + 1 ) * num ) / den - ( t * num ) / den ) == want[t], "x1.5 tick %u", t );
		int num2 = 2, den2 = 3, want2[3] = { 0, 1, 1 };
		for ( uint32_t t = 0; t < 3; ++t )
			CHECK( (int)( ( ( t + 1 ) * num2 ) / den2 - ( t * num2 ) / den2 ) == want2[t], "/1.5 tick %u", t );
	}

	// Scales: Major is 7 notes with root/third/fifth at indices 0, 2, 4
	{
		S::Dtc d;
		S::rebuildScale( &d, 1 );
		CHECK( d.scaleCount == 7, "major has %d notes", d.scaleCount );
		CHECK( d.stableCount == 3 && d.stableIdx[1] == 2 && d.stableIdx[2] == 4,
			   "major stable degrees %d %d %d", d.stableIdx[0], d.stableIdx[1], d.stableIdx[2] );
		S::rebuildScale( &d, 0 );
		CHECK( d.scaleCount == 12, "chromatic has %d notes", d.scaleCount );
	}

	// Directions, walked by hand: the first steps each mode visits from a reset
	{
		struct Want { int dir; int len; int seq[12]; };
		const Want wants[] = {
			{ S::kDirForwards, 4, { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 } },
			{ S::kDirReverse, 4, { 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0 } },
			{ S::kDirPendulum, 4, { 0, 1, 2, 3, 3, 2, 1, 0, 0, 1, 2, 3 } },
			{ S::kDirPong, 4, { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1 } },
			{ S::kDirStride, 4, { 0, 2, 1, 3, 2, 0, 3, 1, 0, 2, 1, 3 } },
			{ S::kDirConverge, 5, { 0, 4, 1, 3, 2, 0, 4, 1, 3, 2, 0, 4 } },
			{ S::kDirDiverge, 5, { 2, 3, 1, 4, 0, 2, 3, 1, 4, 0, 2, 3 } },
			{ S::kDirAnchor, 4, { 0, 1, 0, 2, 0, 3, 0, 1, 0, 2, 0, 3 } },
		};
		for ( const Want& w : wants )
		{
			S::Shoal s;
			S::construct( &s, 48000 );
			for ( int p = 0; p < S::kNumParameters; ++p ) s.v[p] = S::paramRange( p ).def;
			s.v[ S::TP( 0, S::kTLength ) ] = (int16_t)w.len;
			s.v[ S::TP( 0, S::kTDirection ) ] = (int16_t)w.dir;
			S::rebuildScale( s.dtc, 0 );
			for ( int k = 0; k < 12; ++k )
			{
				S::advanceTrack( &s, 0, 1000, false );
				CHECK( s.dtc->tracks[0].pos == w.seq[k], "%s step %d: pos %d, want %d",
					   S::directionNames[w.dir], k, s.dtc->tracks[0].pos, w.seq[k] );
			}
		}
	}

	// Follow resolves chains and ignores cycles; a follower plays its source's notes
	{
		S::Shoal s;
		S::construct( &s, 48000 );
		for ( int p = 0; p < S::kNumParameters; ++p ) s.v[p] = S::paramRange( p ).def;
		s.v[ S::TP( 2, S::kTSource ) ] = 2;		// 3 follows 2
		s.v[ S::TP( 1, S::kTSource ) ] = 1;		// 2 follows 1
		CHECK( S::effectiveTrack( &s, 2 ) == 0, "chain 3->2->1 resolves to %d", S::effectiveTrack( &s, 2 ) );
		s.v[ S::TP( 0, S::kTSource ) ] = 3;		// 1 follows 3: a cycle
		int e = S::effectiveTrack( &s, 2 );
		CHECK( e >= 0 && e < S::kNumTracks, "cycle resolves out of range: %d", e );
		s.v[ S::TP( 0, S::kTSource ) ] = 0;
		S::rebuildScale( s.dtc, 0 );
		for ( int t = 0; t < 3; ++t ) s.dtc->tracks[t].activeSeed = 100 + t;
		int same = 0;
		for ( int st = 0; st < 16; ++st )
		{
			S::StepEval a, b;
			S::evalStep( &s, 0, st, a );
			S::evalStep( &s, 2, st, b );
			same += ( a.fires == b.fires && a.note == b.note );
		}
		CHECK( same == 16, "follower matches source on %d/16 steps", same );
	}

	// Notes stay in key: every fired note of a Major line is a Major pitch class
	{
		S::Shoal s;
		S::construct( &s, 48000 );
		for ( int p = 0; p < S::kNumParameters; ++p ) s.v[p] = S::paramRange( p ).def;
		s.v[S::kGScale] = 1;
		s.v[S::kGRoot] = 50;					// D
		s.v[ S::TP( 0, S::kTNote ) ] = 100;
		s.v[ S::TP( 0, S::kTOct ) ] = -100;
		s.v[ S::TP( 0, S::kTTrans ) ] = 3;
		S::rebuildScale( s.dtc, 1 );
		int bad = 0;
		for ( uint32_t seed = 0; seed < 1000; ++seed )
		{
			s.dtc->tracks[0].activeSeed = seed;
			for ( int st = 0; st < 64; ++st )
			{
				S::StepEval ev;
				S::evalStep( &s, 0, st, ev );
				if ( !ev.fires ) continue;
				int pc = ( ( ev.note - 50 ) % 12 + 12 ) % 12;
				if ( !( S::scaleMasks[1] & ( 1 << pc ) ) || ev.note < 0 || ev.note > 127 ) ++bad;
			}
		}
		CHECK( bad == 0, "%d out-of-key or out-of-range notes", bad );
	}
}

int main( int argc, char** argv )
{
	int scenarios = argc > 1 ? std::atoi( argv[1] ) : 150;
	int frames = argc > 2 ? std::atoi( argv[2] ) : 100000;

	testAnchors();

	int passed = 0;
	for ( int sc = 0; sc < scenarios; ++sc )
	{
		++g_checks;
		if ( runScenario( (uint32_t)sc + 1, frames, sc < 3 ) ) ++passed;
		else ++g_fail;
	}
	std::printf( "%d/%d scenarios of %d frames frame-identical to upstream Shoal\n",
				 passed, scenarios, frames );
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
