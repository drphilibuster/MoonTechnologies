// Ledger's patterns (Pattern.hpp), the pattern player (Player.hpp) and the hook that
// lets Shoal's walker drive a pattern track (Shoal::external / Host::externalStep).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../src/Ledger/Player.hpp"
#include "../../src/Ledger/Voices.hpp"

namespace S = ledger::shoal;
namespace L = ledger;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )
static bool near( float a, float b, float tol = 1e-3f ) { return std::fabs( a - b ) <= tol; }

static L::Pattern* newPattern()
{
	L::Pattern* p = new L::Pattern;
	p->clear();
	return p;
}

static L::Note note( int start, int len, int pitch, int vel = 100 )
{
	L::Note n;
	n.start = (uint16_t)start; n.len = (uint16_t)len; n.pitch = (uint8_t)pitch; n.vel = (uint8_t)vel;
	return n;
}

// ---- patterns ----

static void testPatternEdit()
{
	L::Pattern* p = newPattern();
	p->add( note( 48, 24, 60 ) );
	p->add( note( 0, 24, 62 ) );
	p->add( note( 24, 12, 64 ) );
	CHECK( p->count == 3 && p->notes[0].start == 0 && p->notes[1].start == 24 && p->notes[2].start == 48,
		   "add keeps notes ordered" );
	p->remove( 1 );
	CHECK( p->count == 2 && p->notes[1].pitch == 60, "remove closes the gap" );
	p->add( note( 100, 24, 70 ) );
	p->truncate( 3 );						// 72 ticks
	CHECK( p->count == 2, "truncate drops notes past the end (%d left)", p->count );
	for ( int i = 0; i < L::kMaxNotes + 5; ++i ) p->add( note( i % 1500, 1, 60 ) );
	CHECK( p->count == L::kMaxNotes, "a full pattern refuses more (%d)", p->count );
	delete p;
}

static void testLanes()
{
	L::Pattern* p = newPattern();
	const int len = 8;
	CHECK( L::laneValueAt( *p, L::kLaneMod1, 3.0f, len ) == (float)L::kLaneUnset, "an empty lane is unset" );
	p->lane[L::kLaneMod1][2] = 100;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 5.5f, len ), 100.0f ), "one point holds everywhere" );
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 0.0f, len ), 100.0f ), "...and wraps round" );
	p->lane[L::kLaneMod1][6] = 20;
	p->interp[L::kLaneMod1] = L::kInterpLin;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 4.0f, len ), 60.0f ), "LIN halfway 2->6 (%g)", L::laneValueAt( *p, L::kLaneMod1, 4.0f, len ) );
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 2.0f, len ), 100.0f ), "LIN at a point is the point" );
	// wrapping: 6 -> 2 is four steps through the loop point
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 0.0f, len ), 60.0f ), "LIN wraps 6->2 (%g)", L::laneValueAt( *p, L::kLaneMod1, 0.0f, len ) );
	p->interp[L::kLaneMod1] = L::kInterpOff;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 5.9f, len ), 100.0f ), "OFF holds until the next point" );
	p->interp[L::kLaneMod1] = L::kInterpS;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 4.0f, len ), 60.0f ), "S is symmetric at the middle" );
	CHECK( L::laneValueAt( *p, L::kLaneMod1, 3.0f, len ) > 80.0f, "S lingers near the start" );
	p->interp[L::kLaneMod1] = L::kInterpLog;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 4.0f, len ), 100.0f - 80.0f * 0.75f ), "LOG moves fast first" );
	p->interp[L::kLaneMod1] = L::kInterpExp;
	CHECK( near( L::laneValueAt( *p, L::kLaneMod1, 4.0f, len ), 100.0f - 80.0f * 0.25f ), "EXP moves slow first" );
	// a point past the pattern's length is not part of it
	L::Pattern* q = newPattern();
	q->lane[L::kLaneMod2][10] = 5;
	CHECK( L::laneValueAt( *q, L::kLaneMod2, 1.0f, 8 ) == (float)L::kLaneUnset, "points past the end are ignored" );
	delete p; delete q;
}

// ---- capture and the generator ----

static void defaults( S::Shoal& s )
{
	S::construct( &s, 48000 );
	for ( int p = 0; p < S::kNumParameters; ++p ) s.v[p] = S::paramRange( p ).def;
	S::rebuildScale( s.dtc, s.v[S::kGScale] );
}

static void testCapture()
{
	S::Shoal s;
	defaults( s );
	s.v[S::kGScale] = 2;
	S::rebuildScale( s.dtc, 2 );
	s.v[ S::TP( 0, S::kTChance ) ] = 60;
	s.v[ S::TP( 0, S::kTNote ) ] = 40;
	s.v[ S::TP( 0, S::kTTie ) ] = 30;
	s.v[ S::TP( 0, S::kTGate ) ] = 75;
	s.v[ S::TP( 0, S::kTLength ) ] = 24;
	s.dtc->tracks[0].activeSeed = 321;
	s.dtc->tracks[0].epoch[5] = 3;			// evolved steps are captured as they are now
	L::Pattern* p = newPattern();
	L::capture( &s, 0, 90, *p );
	int k = 0, bad = 0;
	for ( int st = 0; st < 24; ++st )
	{
		S::StepEval ev;
		S::evalStep( &s, 0, st, ev );
		if ( !ev.fires ) continue;
		const L::Note& n = p->notes[k++];
		S::StepEval nx;
		S::evalStep( &s, 0, ( st + 1 ) % 24, nx );
		int len = ev.tie ? ( nx.fires ? 36 : 24 ) : ( 24 * 75 ) / 100;
		if ( n.start != st * 24 || n.pitch != ev.note || n.len != len || n.vel != 90 ) ++bad;
	}
	CHECK( k == p->count && bad == 0, "capture = evalStep: %d notes, %d captured, %d wrong", k, p->count, bad );

	int16_t block[L::kNumBlock];
	L::storeBlock( block, s.v, 0 );
	CHECK( block[L::kBlockChance] == 60 && block[L::kBlockLength] == 24, "storeBlock reads the track" );
	L::captureBlock( block );
	CHECK( block[L::kBlockChance] == 100 && block[L::kBlockNote] == 0 && block[L::kBlockTie] == 0
		   && block[L::kBlockLength] == 24, "a captured pattern does not filter itself twice" );
	for ( int i = 0; i < L::kNumBlock; ++i )
		CHECK( L::blockParam( i, 2 ) >= S::TP( 2, 0 ), "block param %d is track 3's", i );
	delete p;
}

static void testGenerator()
{
	CHECK( L::degreeToNote( 48, 1, 0 ) == 48 && L::degreeToNote( 48, 1, 2 ) == 52 && L::degreeToNote( 48, 1, 7 ) == 60
		   && L::degreeToNote( 48, 1, -1 ) == 47, "degrees of C major" );
	L::GenSettings g;
	L::Pattern* a = newPattern();
	L::Pattern* b = newPattern();
	L::generate( *a, 16, g, 50, 2, 1234, L::kGenAll );
	L::generate( *b, 16, g, 50, 2, 1234, L::kGenAll );
	CHECK( a->count > 0 && a->count == b->count && !memcmp( a->notes, b->notes, sizeof( L::Note ) * a->count ),
		   "a seed is a pattern" );
	int outOfKey = 0;
	for ( int i = 0; i < a->count; ++i )
		if ( !( S::scaleMasks[2] & ( 1 << ( ( a->notes[i].pitch - 50 + 120 ) % 12 ) ) ) ) ++outOfKey;
	CHECK( outOfKey == 0, "%d generated notes out of key", outOfKey );
	g.density = 100;
	L::generate( *a, 16, g, 48, 0, 1, L::kGenAll );
	CHECK( a->count == 16, "full density on a step grid is one note per step (%d)", a->count );
	g.density = 0;
	L::generate( *a, 16, g, 48, 0, 1, L::kGenAll );
	CHECK( a->count == 0, "no density, no notes" );
	g.density = 70;
	L::generate( *a, 16, g, 48, 1, 77, L::kGenAll );
	*b = *a;
	L::generate( *b, 16, g, 48, 1, 78, L::kGenPitch );
	int samePlace = 0, samePitch = 0;
	for ( int i = 0; i < a->count; ++i )
	{
		samePlace += a->notes[i].start == b->notes[i].start && a->notes[i].len == b->notes[i].len
					 && a->notes[i].vel == b->notes[i].vel;
		samePitch += a->notes[i].pitch == b->notes[i].pitch;
	}
	CHECK( samePlace == a->count && samePitch < a->count, "re-rolling pitch moves only pitches (%d/%d kept)",
		   samePitch, a->count );
	delete a; delete b;
}

// ---- the player ----

// The pattern source and the voices, wired straight together (no effects), behind the
// interface these tests were first written against.
struct Player
{
	L::PatternSource src;
	L::Voices vs;
	L::IdSource ids;
	L::Voice ( &voice )[L::kMaxVoices];
	int& alloc;
	bool& retrig;
	uint8_t& lastVel;
	struct Q { uint8_t note; } q[256];
	int qn;

	Player() : voice( vs.voice ), alloc( vs.alloc ), retrig( vs.retrig ), lastVel( vs.lastVel ), qn( 0 ) {}
	void init( uint32_t sr ) { vs.init( sr ); src.reset(); qn = 0; }
	void reset() { src.reset(); vs.reset(); qn = 0; }
	void setVoices( int n ) { vs.setVoices( n ); }
	void step( const L::Pattern& p, int st, uint32_t per, bool silent, int chance, uint32_t seed, uint32_t pass, int tr )
	{
		src.step( p, st, per, silent, chance, seed, pass, tr, ids );
		qn = 0;
		for ( int i = 0; i < src.q.n; ++i ) if ( src.q.q[i].ev.type == L::kEvNoteOn ) q[qn++].note = src.q.q[i].ev.note;
	}
	void process() { src.tick( [&]( const L::Ev& e ) { vs.event( e ); } ); vs.tick(); }
	bool gate( int i ) const { return vs.gate( i ); }
};

// Run a player for n samples; record each voice's gate, sample by sample.
static std::vector<std::vector<bool> > run( Player& pl, int n )
{
	std::vector<std::vector<bool> > g( L::kMaxVoices, std::vector<bool>( n ) );
	for ( int i = 0; i < n; ++i )
	{
		pl.process();
		for ( int v = 0; v < L::kMaxVoices; ++v ) g[v][i] = pl.gate( v );
	}
	return g;
}

static int firstHigh( const std::vector<bool>& g ) { for ( size_t i = 0; i < g.size(); ++i ) if ( g[i] ) return (int)i; return -1; }
static int highCount( const std::vector<bool>& g ) { int c = 0; for ( size_t i = 0; i < g.size(); ++i ) c += g[i]; return c; }

static void testPlayerTiming()
{
	L::Pattern* p = newPattern();
	p->add( note( 24 + 12, 24, 60, 77 ) );		// step 1, half way in, one step long
	p->add( note( 24 + 0, 6, 64 ) );			// step 1, on the beat, a quarter step
	p->add( note( 48, 24, 67 ) );				// step 2: not this step's
	Player pl;
	pl.init( 48000 );
	pl.setVoices( 2 );
	pl.step( *p, 1, 480, false, 100, 1, 0, 0 );
	CHECK( pl.qn == 2, "a step schedules its own notes only (%d)", pl.qn );
	std::vector<std::vector<bool> > g = run( pl, 2000 );
	// voice 0 takes the first note to start (the beat note), voice 1 the other
	CHECK( firstHigh( g[0] ) == 0 && highCount( g[0] ) == 120, "beat note: on at 0 for 120 samples (%d, %d)",
		   firstHigh( g[0] ), highCount( g[0] ) );
	CHECK( firstHigh( g[1] ) == 240 && highCount( g[1] ) == 480, "half-step note: on at 240 for 480 (%d, %d)",
		   firstHigh( g[1] ), highCount( g[1] ) );
	CHECK( pl.lastVel == 77, "velocity of the last note (%d)", pl.lastVel );

	pl.reset();
	pl.step( *p, 1, 480, true, 100, 1, 0, 0 );
	CHECK( pl.qn == 0, "a silent step plays nothing" );

	pl.reset();
	pl.setVoices( 1 );
	pl.step( *p, 1, 480, false, 100, 1, 0, -70 );
	run( pl, 1 );
	CHECK( pl.voice[0].note == 0, "transpose clamps at 0 (%d)", pl.voice[0].note );
	delete p;
}

static void testPlayerChance()
{
	L::Pattern* p = newPattern();
	for ( int i = 0; i < 24; ++i ) p->add( note( i, 1, 60 + i ) );
	Player pl;
	pl.step( *p, 0, 480, false, 0, 9, 0, 0 );
	CHECK( pl.qn == 0, "chance 0 plays nothing" );
	pl.step( *p, 0, 480, false, 100, 9, 0, 0 );
	CHECK( pl.qn == 24, "chance 100 plays everything" );
	int a[3];
	for ( int k = 0; k < 3; ++k )
	{
		pl.reset();
		pl.step( *p, 0, 480, false, 50, 9, (uint32_t)( k == 2 ? 1 : 0 ), 0 );
		a[k] = pl.qn;
		uint32_t mask = 0;
		for ( int i = 0; i < pl.qn; ++i ) mask ^= 1u << ( pl.q[i].note - 60 );
		a[k] = (int)mask;
	}
	CHECK( a[0] == a[1], "the same pass rolls the same notes" );
	CHECK( a[0] != a[2], "another pass rolls others" );
	delete p;
}

static void testAllocators()
{
	L::Pattern* chord = newPattern();
	chord->add( note( 0, 24, 60 ) );
	chord->add( note( 0, 24, 64 ) );
	chord->add( note( 0, 24, 67 ) );
	chord->add( note( 0, 24, 71 ) );

	// LRU across three voices: three notes, three voices; the fourth steals the oldest
	Player pl;
	pl.setVoices( 3 );
	pl.alloc = L::kAllocLRU;
	pl.step( *chord, 0, 480, false, 100, 1, 0, 0 );
	run( pl, 10 );
	int on = 0;
	for ( int v = 0; v < 3; ++v ) on += pl.voice[v].on;
	CHECK( on == 3, "LRU: three voices sounding (%d)", on );

	// FIRST drops what does not fit
	pl.reset();
	pl.alloc = L::kAllocFirst;
	pl.step( *chord, 0, 480, false, 100, 1, 0, 0 );
	run( pl, 10 );
	bool has71 = false;
	for ( int v = 0; v < 3; ++v ) has71 = has71 || pl.voice[v].note == 71;
	CHECK( !has71, "FIRST drops the fourth note" );

	// CYCLIC walks round
	pl.reset();
	pl.alloc = L::kAllocCyclic;
	L::Pattern* seq = newPattern();
	for ( int i = 0; i < 4; ++i ) seq->add( note( i * 6, 3, 60 + i ) );
	pl.step( *seq, 0, 480, false, 100, 1, 0, 0 );
	run( pl, 480 );
	CHECK( pl.voice[0].note == 63 && pl.voice[1].note == 61 && pl.voice[2].note == 62,
		   "CYCLIC: 60,61,62 then 63 back on voice 0 (%d %d %d)", pl.voice[0].note, pl.voice[1].note, pl.voice[2].note );

	// POLY: a repeated note keeps its voice
	pl.reset();
	pl.alloc = L::kAllocPoly;
	L::Pattern* rep = newPattern();
	rep->add( note( 0, 3, 60 ) );
	rep->add( note( 6, 3, 64 ) );
	rep->add( note( 12, 3, 60 ) );
	pl.step( *rep, 0, 480, false, 100, 1, 0, 0 );
	std::vector<std::vector<bool> > g = run( pl, 480 );
	CHECK( pl.voice[0].note == 60 && highCount( g[0] ) == 2 * 60 && pl.voice[1].note == 64,
		   "POLY: 60 twice on voice 0 (%d samples high)", highCount( g[0] ) );

	// mono: a note over a sounding note is legato by default -- the gate stays high --
	// and retriggers (the gate dips) with GATE RETRIG on
	Player mono;
	mono.init( 48000 );
	L::Pattern* leg = newPattern();
	leg->add( note( 0, 24, 60 ) );
	leg->add( note( 12, 24, 62 ) );
	mono.step( *leg, 0, 480, false, 100, 1, 0, 0 );
	g = run( mono, 1000 );
	CHECK( highCount( g[0] ) == 720 && firstHigh( g[0] ) == 0, "legato: one gate, 0 to 720 (%d high)", highCount( g[0] ) );
	delete leg;
	mono.reset();
	mono.retrig = true;
	L::Pattern* legato = newPattern();
	legato->add( note( 0, 24, 60 ) );
	legato->add( note( 12, 24, 62 ) );
	mono.step( *legato, 0, 480, false, 100, 1, 0, 0 );
	g = run( mono, 1000 );
	CHECK( g[0][239] && !g[0][240] && !g[0][240 + 47] && g[0][240 + 48], "retrigger: 48 samples (1 ms) low at 240" );
	CHECK( mono.voice[0].note == 62, "mono plays the newest note" );
	delete chord; delete seq; delete rep; delete legato;
}

// ---- the hook: Shoal's walker drives an external track ----

struct Recorder : S::Host
{
	std::vector<int> pos, period;
	std::vector<bool> silent;
	void setParameterFromAudio( S::Shoal* s, int p, int16_t v ) override { s->v[p] = v; S::parameterChanged( s, p ); }
	void sendMidi3( uint32_t, uint8_t, uint8_t, uint8_t ) override {}
	void externalStep( S::Shoal*, int t, int p, uint32_t sp, bool sil ) override
	{
		if ( t != 0 ) return;
		pos.push_back( p ); period.push_back( (int)sp ); silent.push_back( sil );
	}
};

static void testExternalHook()
{
	for ( int dir = 0; dir < S::kNumDirections; ++dir )
	{
		S::Shoal a, b;
		Recorder ha, hb;
		S::Shoal* both[2] = { &a, &b };
		Recorder* hosts[2] = { &ha, &hb };
		for ( int k = 0; k < 2; ++k )
		{
			defaults( *both[k] );
			both[k]->host = hosts[k];
			both[k]->v[S::kGBPM] = 300;
			both[k]->v[ S::TP( 0, S::kTDirection ) ] = (int16_t)dir;
			both[k]->v[ S::TP( 0, S::kTLength ) ] = 11;
			both[k]->v[ S::TP( 0, S::kTRate ) ] = 25;		// x8: a few hundred steps, so Breathe gets to roll
			both[k]->v[ S::TP( 0, S::kTSlop ) ] = 40;
			both[k]->v[ S::TP( 0, S::kTBreathe ) ] = 30;
			both[k]->v[ S::TP( 1, S::kTSource ) ] = 1;		// track 2 follows track 1
			for ( int p = 0; p < S::kNumParameters; ++p ) S::parameterChanged( both[k], p );
		}
		b.external[0] = true;

		float pa[8], ga[8], pb[8], gb[8], dummy[8], clk;
		S::Io ia, ib;
		std::memset( &ia, 0, sizeof ia );
		std::memset( &ib, 0, sizeof ib );
		for ( int t = 0; t < 8; ++t )
		{
			ia.pitchOut[t] = &pa[t]; ia.gateOut[t] = &ga[t]; ia.currentOut[t] = &dummy[t];
			ib.pitchOut[t] = &pb[t]; ib.gateOut[t] = &gb[t]; ib.currentOut[t] = &dummy[t];
		}
		ia.clkOut = &clk; ib.clkOut = &clk;
		std::vector<int> walkA;
		uint32_t lastAdv = 0;
		int otherDiffs = 0, extGate = 0;
		for ( int i = 0; i < 400000; ++i )
		{
			S::step( &a, ia, 1 );
			S::step( &b, ib, 1 );
			if ( a.dtc->tracks[0].advances != lastAdv )
			{
				lastAdv = a.dtc->tracks[0].advances;
				walkA.push_back( a.dtc->tracks[0].pos );
			}
			for ( int t = 1; t < 8; ++t )
				otherDiffs += ( pa[t] != pb[t] ) || ( ga[t] != gb[t] );
			extGate += gb[0] != 0.0f;
		}
		CHECK( walkA.size() > 50 && walkA == hb.pos, "%s: the external track walks as the generator would "
			   "(%zu vs %zu steps)", S::directionNames[dir], walkA.size(), hb.pos.size() );
		CHECK( otherDiffs == 0, "%s: the other tracks -- a follower included -- are untouched (%d)",
			   S::directionNames[dir], otherDiffs );
		CHECK( extGate == 0, "%s: Shoal itself never gates an external track", S::directionNames[dir] );
		int sil = 0;
		for ( size_t k = 0; k < hb.silent.size(); ++k ) sil += hb.silent[k];
		CHECK( sil > 0 && sil < (int)hb.silent.size(), "%s: Breathe reaches the external track (%d of %zu silent)",
			   S::directionNames[dir], sil, hb.silent.size() );
	}
}

int main()
{
	testPatternEdit();
	testLanes();
	testCapture();
	testGenerator();
	testPlayerTiming();
	testPlayerChance();
	testAllocators();
	testExternalHook();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
