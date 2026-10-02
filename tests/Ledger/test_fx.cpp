// Ledger's effects (Effects.hpp), its event voices (Voices.hpp) and its sources
// (Player.hpp). Each effect against what it is specified to do, then the invariant that
// matters most in a sequencer: whatever the chain, whatever is muted or swapped while
// notes are sounding, every note-on that leaves the chain is followed by its note-off.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include "../../src/Ledger/Effects.hpp"
#include "../../src/Ledger/Voices.hpp"
#include "../../src/Ledger/Player.hpp"

namespace L = ledger;
namespace S = ledger::shoal;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )

struct Got { uint32_t at; L::Ev ev; };

// A chain on a clock: one beat is `spb` samples.
struct Rig
{
	L::Chain chain;
	L::IdSource ids;
	std::vector<Got> got;
	uint32_t now = 0;
	float spb = 1000.f;

	static void sink( void* self, const L::Ev& e ) { Rig* r = (Rig*)self; r->got.push_back( Got{ r->now, e } ); }

	Rig()
	{
		chain.ids = &ids;
		chain.sink = &Rig::sink;
		chain.sinkCtx = this;
		sync();
	}
	void sync()
	{
		chain.ctx.now = now;
		chain.ctx.beat = now / (double)spb;
		chain.ctx.spb = spb;
		chain.ctx.sampleRate = 48000;
		chain.ctx.root = 48;
		chain.ctx.scale = 1;
		chain.ctx.running = true;
	}
	L::Fx& set( int slot, int type, std::initializer_list<int> params = {} )
	{
		chain.setType( slot, type );
		int i = 0;
		for ( int v : params ) { chain.fx[slot].p[i] = chain.fx[slot].pe[i] = (int16_t)v; ++i; }
		return chain.fx[slot];
	}
	void in( const L::Ev& e ) { sync(); chain.push( 0, e ); }
	void run( int n ) { for ( int i = 0; i < n; ++i ) { sync(); chain.tick(); ++now; } }

	std::vector<Got> ons() const { std::vector<Got> v; for ( auto& g : got ) if ( g.ev.type == L::kEvNoteOn ) v.push_back( g ); return v; }
	std::vector<Got> offs() const { std::vector<Got> v; for ( auto& g : got ) if ( g.ev.type == L::kEvNoteOff ) v.push_back( g ); return v; }
	std::vector<Got> mods() const { std::vector<Got> v; for ( auto& g : got ) if ( g.ev.type == L::kEvMod ) v.push_back( g ); return v; }
	uint32_t offAt( uint16_t id ) const { for ( auto& g : got ) if ( g.ev.type == L::kEvNoteOff && g.ev.id == id ) return g.at; return 0xFFFFFFFF; }
	// every note-on that left has exactly one note-off after it
	bool balanced( std::string* why = NULL ) const
	{
		std::map<uint16_t, int> open;
		for ( auto& g : got )
		{
			if ( g.ev.type == L::kEvNoteOn ) open[g.ev.id]++;
			else if ( g.ev.type == L::kEvNoteOff ) { if ( open.count( g.ev.id ) ) open[g.ev.id]--; }
		}
		for ( auto& kv : open )
			if ( kv.second > 0 ) { if ( why ) *why = "id " + std::to_string( kv.first ) + " never ended"; return false; }
		return true;
	}
};

static void testPassThrough()
{
	Rig r;
	r.in( L::Ev::on( 7, 60, 90 ) );
	r.run( 10 );
	r.in( L::Ev::off( 7 ) );
	r.in( L::Ev::mod( 0, 33 ) );
	CHECK( r.got.size() == 3 && r.got[0].ev.id == 7 && r.got[1].ev.type == L::kEvNoteOff && r.got[2].ev.value == 33,
		   "an empty chain is transparent" );
	r.got.clear();
	r.set( 3, L::kFxMidiOut );
	r.chain.setMuted( 3, true );
	r.in( L::Ev::on( 8, 61, 90 ) );
	CHECK( r.got.size() == 1 && r.got[0].ev.id == 8, "a muted slot is skipped" );
}

static void testHarmonizerScaleFilter()
{
	Rig r;
	r.set( 0, L::kFxHarmonizer, { 1, 4, 7, 0, 12 } );
	r.in( L::Ev::on( 1, 60, 100 ) );
	std::vector<Got> on = r.ons();
	std::set<int> notes;
	for ( auto& g : on ) notes.insert( g.ev.note );
	CHECK( on.size() == 4 && notes == std::set<int>( { 60, 64, 67, 72 } ), "harmonizer: 60 +4 +7 +12 (%zu notes)", on.size() );
	r.in( L::Ev::off( 1 ) );
	CHECK( r.offs().size() == 4 && r.balanced(), "one note-off ends the chord" );

	Rig s;
	s.set( 0, L::kFxScale, { 0, 0, 0, 0 } );		// the books: C major (ctx scale 1, root 48)
	s.in( L::Ev::on( 1, 61, 100 ) );					// C#: nearest is C (down first)
	s.in( L::Ev::on( 2, 66, 100 ) );					// F#: nearest is F
	s.in( L::Ev::on( 3, 64, 100 ) );					// E: in key
	CHECK( s.ons().size() == 3 && s.ons()[0].ev.note == 60 && s.ons()[1].ev.note == 65 && s.ons()[2].ev.note == 64,
		   "scale snaps to the books' key (%d %d %d)", s.ons()[0].ev.note, s.ons()[1].ev.note, s.ons()[2].ev.note );
	Rig s2;
	s2.set( 0, L::kFxScale, { 10, 10, 3, 0 } );		// minor pentatonic in A, out-of-key dropped
	s2.in( L::Ev::on( 1, 61, 100 ) );
	s2.in( L::Ev::on( 2, 69, 100 ) );
	CHECK( s2.ons().size() == 1 && s2.ons()[0].ev.note == 69, "scale can drop what is out of key" );

	Rig f;
	f.set( 0, L::kFxFilter, { 60, 72, 0, 127 } );
	f.in( L::Ev::on( 1, 59, 100 ) );
	f.in( L::Ev::on( 2, 66, 100 ) );
	f.in( L::Ev::on( 3, 73, 100 ) );
	f.in( L::Ev::off( 1 ) );
	f.in( L::Ev::off( 2 ) );
	CHECK( f.ons().size() == 1 && f.ons()[0].ev.note == 66 && f.balanced(), "filter passes the range" );
	Rig f2;
	f2.set( 0, L::kFxFilter, { 72, 60, 0, 127 } );	// min > max: blocks the range
	f2.in( L::Ev::on( 1, 59, 100 ) );
	f2.in( L::Ev::on( 2, 66, 100 ) );
	CHECK( f2.ons().size() == 1 && f2.ons()[0].ev.note == 59, "filter with min > max blocks it" );
}

static void testEcho()
{
	Rig r;
	r.set( 0, L::kFxEcho, { L::kDiv8, 3, 1, 0, 0, 0 } );	// 1/8 = 500 samples, three echoes, linear fade
	r.in( L::Ev::on( 1, 60, 100 ) );
	r.run( 200 );
	r.in( L::Ev::off( 1 ) );
	r.run( 3000 );
	std::vector<Got> on = r.ons();
	CHECK( on.size() == 4, "the note and three echoes (%zu)", on.size() );
	if ( on.size() == 4 )
	{
		CHECK( on[1].at == 500 && on[2].at == 1000 && on[3].at == 1500, "echoes every 1/8 (%u %u %u)", on[1].at, on[2].at, on[3].at );
		CHECK( on[1].ev.vel == 75 && on[2].ev.vel == 50 && on[3].ev.vel == 25, "velocity fades linearly (%d %d %d)",
			   on[1].ev.vel, on[2].ev.vel, on[3].ev.vel );
		for ( int k = 1; k < 4; ++k )
			CHECK( r.offAt( on[k].ev.id ) == on[k].at + 200, "echo %d lasts as long as the note (%u)", k, r.offAt( on[k].ev.id ) - on[k].at );
	}
	CHECK( r.balanced(), "every echo ends" );

	Rig p;
	p.set( 0, L::kFxEcho, { L::kDiv16, 4, 0, 0, 12, 0 } );
	p.in( L::Ev::on( 1, 60, 100 ) );
	p.run( 10 );
	p.in( L::Ev::off( 1 ) );
	p.run( 2000 );
	std::vector<Got> pon = p.ons();
	CHECK( pon.size() == 5 && pon[4].ev.note == 72 && pon[2].ev.note == 66, "pitch climbs to +12 by the last echo" );
	CHECK( p.balanced(), "balanced" );

	// Repeats turned down (by the matrix, say) while a note's echoes are out: the ones
	// already made fade as the last does, and none ends before it starts
	for ( int mode = 1; mode <= 3; ++mode )
	{
		Rig q;
		q.set( 0, L::kFxEcho, { L::kDiv16, 8, 0, mode, 0, 0 } );
		q.in( L::Ev::on( 1, 60, 100 ) );
		q.run( 100 );
		q.chain.fx[0].pe[1] = 1;
		q.in( L::Ev::off( 1 ) );
		q.run( 4000 );
		bool ok = q.ons().size() == 9;
		for ( auto& g : q.ons() ) ok = ok && q.offAt( g.ev.id ) != 0xFFFFFFFF && q.offAt( g.ev.id ) >= g.at;
		CHECK( ok && q.balanced(), "echo, gate fade %d: Repeats lowered mid-note, every echo still ends after it starts", mode );
	}
}

static void testArp()
{
	Rig r;
	r.set( 0, L::kFxArp, { 0, L::kDiv16, 0, 50, 0, 1, 0 } );	// up, 1/16 = 250 samples, gate 50%
	r.run( 100 );												// the grid's first line has passed
	r.in( L::Ev::on( 1, 67, 100 ) );
	r.in( L::Ev::on( 2, 60, 90 ) );
	r.in( L::Ev::on( 3, 64, 80 ) );
	r.run( 1600 );
	std::vector<Got> on = r.ons();
	CHECK( on.size() >= 6, "arpeggio notes: %zu", on.size() );
	if ( on.size() >= 6 )
	{
		CHECK( on[0].ev.note == 60 && on[1].ev.note == 64 && on[2].ev.note == 67 && on[3].ev.note == 60,
			   "up: 60 64 67 60 (%d %d %d %d)", on[0].ev.note, on[1].ev.note, on[2].ev.note, on[3].ev.note );
		CHECK( on[0].at == 250 && on[1].at == 500, "on the 1/16 grid (%u %u)", on[0].at, on[1].at );
		CHECK( on[0].ev.vel == 90 && on[1].ev.vel == 80 && on[2].ev.vel == 100, "each note keeps its velocity" );
		CHECK( r.offAt( on[0].ev.id ) == 250 + 125, "gate 50%% of a 1/16 (%u)", r.offAt( on[0].ev.id ) );
	}
	CHECK( r.ons().size() - r.offs().size() <= 1, "at most one arpeggio note sounding at a time" );
	r.in( L::Ev::off( 1 ) ); r.in( L::Ev::off( 2 ) ); r.in( L::Ev::off( 3 ) );
	size_t n = r.ons().size();
	r.run( 2000 );
	CHECK( r.ons().size() == n && r.balanced(), "letting go stops it, cleanly" );

	Rig d;
	d.set( 0, L::kFxArp, { 2, L::kDiv16, 1, 50, 0, 1, 0 } );		// up-down across two octaves
	d.in( L::Ev::on( 1, 60, 100 ) );
	d.in( L::Ev::on( 2, 64, 100 ) );
	d.run( 250 * 8 + 10 );
	std::vector<int> seq;
	for ( auto& g : d.ons() ) seq.push_back( g.ev.note );
	std::vector<int> want = { 60, 64, 72, 76, 72, 64, 60, 64 };
	CHECK( seq.size() >= 8 && std::vector<int>( seq.begin(), seq.begin() + 8 ) == want, "up-down over octaves, no repeated ends" );
}

static void testRatchetEuclidRegister()
{
	Rig r;
	r.set( 0, L::kFxRatchet, { L::kDiv32, 50, 0 } );		// 1/32 = 125 samples
	r.in( L::Ev::on( 1, 60, 100 ) );
	r.run( 1000 );
	r.in( L::Ev::off( 1 ) );
	r.run( 500 );
	CHECK( r.ons().size() == 8, "a held note ratchets every 1/32 (%zu in a beat)", r.ons().size() );
	CHECK( r.ons().size() >= 2 && r.ons()[1].at == 125 && r.offAt( r.ons()[0].ev.id ) == 62, "on time, half-gated" );
	CHECK( r.balanced(), "balanced" );

	Rig e;
	e.set( 0, L::kFxEuclid, { 50, 16, 5, L::kDiv16, 50, 0, 100 } );
	e.run( 4 * 1000 * 4 );									// four bars: 64 sixteenths
	CHECK( e.ons().size() == 20, "5 of 16, four times (%zu)", e.ons().size() );
	CHECK( e.ons().size() && e.ons()[0].ev.note == 50, "its own note" );
	e.in( L::Ev::on( 5, 70, 100 ) );
	CHECK( e.ons().back().ev.note == 70, "fixed-note mode passes input through" );

	Rig g;
	g.set( 0, L::kFxRegister, { 42, L::kDiv16, 8, 0, 60, 12, 50, 1 } );	// chaos 0: locked loop of 8
	g.run( 250 * 32 + 5 );
	std::vector<int> ns;
	for ( auto& x : g.ons() ) ns.push_back( x.ev.note );
	bool loops = ns.size() >= 24;
	for ( size_t i = 8; i < ns.size() && loops; ++i ) loops = ns[i] == ns[i - 8];
	bool range = true;
	for ( int n : ns ) range = range && n >= 48 && n <= 72;
	CHECK( loops && range, "locked register: a loop of 8, within centre +- span (%zu notes)", ns.size() );
	Rig g2;
	g2.set( 0, L::kFxRegister, { 42, L::kDiv16, 8, 0, 60, 12, 50, 1 } );
	g2.run( 250 * 32 + 5 );
	bool same = g2.ons().size() == g.ons().size();
	for ( size_t i = 0; same && i < g.ons().size(); ++i ) same = g2.ons()[i].ev.note == g.ons()[i].ev.note;
	CHECK( same, "a seed is a melody" );
}

static void testChanceBernoulliRandomizer()
{
	Rig a, b;
	a.set( 0, L::kFxChance, { 0, 0, 0, 0, 100 } );
	b.set( 0, L::kFxChance, { 100, 0, 0, 0, 100 } );
	for ( int i = 1; i <= 50; ++i ) { a.in( L::Ev::on( i, 60, 100 ) ); b.in( L::Ev::on( i, 60, 100 ) ); }
	CHECK( a.ons().empty() && b.ons().size() == 50, "chance 0 and 100 (%zu, %zu)", a.ons().size(), b.ons().size() );
	Rig h;
	h.set( 0, L::kFxChance, { 50, 0, 0, 0, 100 } );
	for ( int i = 1; i <= 2000; ++i ) { h.in( L::Ev::on( i, 60, 100 ) ); h.in( L::Ev::off( i ) ); }
	CHECK( h.ons().size() > 850 && h.ons().size() < 1150 && h.balanced(), "chance 50: about half (%zu)", h.ons().size() );
	Rig lot;
	lot.set( 0, L::kFxChance, { 50, 3, 0, 0, 100 } );				// one roll per beat
	int agree = 0, beats = 0;
	for ( int bt = 0; bt < 200; ++bt )
	{
		size_t before = lot.ons().size();
		lot.run( 100 );
		lot.in( L::Ev::on( 1000 + bt * 2, 60, 100 ) );
		lot.run( 400 );
		lot.in( L::Ev::on( 1001 + bt * 2, 62, 100 ) );
		lot.run( 500 );
		size_t got = lot.ons().size() - before;
		agree += got == 0 || got == 2;
		++beats;
	}
	CHECK( agree == beats, "LOT = beat: both notes of a beat go together (%d/%d)", agree, beats );

	Rig ber;
	ber.set( 0, L::kFxBernoulli, { 100, 0 } );
	ber.in( L::Ev::on( 1, 60, 100 ) );
	CHECK( ber.ons().empty(), "Bernoulli: all to B, B dropped" );

	Rig rnd;
	rnd.set( 0, L::kFxRandomizer, { 2, 3, 0, 1, 10, 10, 100, 100 } );
	int lo = 999, hi = -999;
	for ( int i = 1; i <= 500; ++i )
	{
		rnd.in( L::Ev::on( i, 60, 100 ) );
		int n = rnd.ons().back().ev.note;
		lo = std::min( lo, n ); hi = std::max( hi, n );
		rnd.run( 1 );
		rnd.in( L::Ev::off( i ) );
	}
	CHECK( lo == 58 && hi == 60 + 3 + 12, "randomizer stays in its range (%d..%d)", lo, hi );
	rnd.run( 5000 );
	CHECK( rnd.balanced(), "and every lengthened note ends" );
}

static void testSwingHoldGlide()
{
	Rig s;
	s.set( 0, L::kFxSwing, { 100, 1, 50, 0 } );					// full swing on 1/16 (250)
	s.in( L::Ev::on( 1, 60, 100 ) );							// on the beat: untouched
	s.run( 250 );
	s.in( L::Ev::on( 2, 62, 100 ) );							// the off-beat sixteenth: pushed half a sixteenth
	s.run( 300 );
	CHECK( s.ons().size() == 2 && s.ons()[0].at == 0 && s.ons()[1].at == 250 + 125, "swing delays the off-beat (%u)",
		   s.ons().size() == 2 ? s.ons()[1].at : 0 );
	s.in( L::Ev::off( 2 ) );
	s.run( 200 );
	CHECK( s.offAt( s.ons()[1].ev.id ) == 550 + 125, "and its note-off by as much" );

	Rig h;
	h.set( 0, L::kFxHold, { 0, 0 } );
	h.in( L::Ev::on( 1, 60, 100 ) );
	h.in( L::Ev::off( 1 ) );
	h.in( L::Ev::on( 2, 64, 100 ) );
	h.in( L::Ev::off( 2 ) );
	CHECK( h.ons().size() == 2 && h.offs().empty(), "hold: notes outlive their keys" );
	h.in( L::Ev::on( 3, 60, 100 ) );
	CHECK( h.offs().size() == 1 && h.offs()[0].ev.id == h.ons()[0].ev.id, "pressing a held note again lets it go" );
	Rig rl;
	rl.set( 0, L::kFxHold, { 1, 0 } );
	rl.in( L::Ev::on( 1, 60, 100 ) ); rl.in( L::Ev::on( 2, 64, 100 ) );
	rl.in( L::Ev::off( 1 ) ); rl.in( L::Ev::off( 2 ) );
	CHECK( rl.offs().empty(), "relatch: the chord stays after the keys" );
	rl.in( L::Ev::on( 3, 67, 100 ) );
	CHECK( rl.offs().size() == 2 && rl.ons().size() == 3, "a new chord replaces it" );

	Rig gl;
	gl.set( 0, L::kFxGlide, { L::kGlideLinear, 50 } );
	gl.in( L::Ev::on( 1, 60, 100 ) );
	CHECK( gl.ons()[0].ev.glideType == L::kGlideLinear && gl.ons()[0].ev.glideMs == L::glideMs( 50 ), "glide tags the note" );
}

static void testModEffects()
{
	Rig l;
	l.set( 0, L::kFxLfo, { 0, 1, 1, 2, 40, 100, 0, 0 } );		// sine to MOD 2, synced, period 1 beat
	l.run( 2000 );
	int lo = 999, hi = -1;
	for ( auto& g : l.mods() ) { CHECK( g.ev.lane == 1, "LFO lane" ); lo = std::min( lo, (int)g.ev.value ); hi = std::max( hi, (int)g.ev.value ); }
	CHECK( lo <= 2 && hi >= 125, "LFO sweeps the lane (%d..%d)", lo, hi );
	int peakAt = 0, peak = -1;
	for ( auto& g : l.mods() ) if ( g.at < 1000 && g.ev.value > peak ) { peak = g.ev.value; peakAt = g.at; }
	CHECK( peakAt > 200 && peakAt < 300, "synced: the sine peaks a quarter beat in (%d)", peakAt );

	Rig e;
	e.set( 0, L::kFxEnvelope, { 0, 0, 30, 50, 30, 100, 0, 0, 0, -1 } );
	e.in( L::Ev::on( 1, 60, 100 ) );
	e.run( 4800 );
	int top = 0, now = 0;
	for ( auto& g : e.mods() ) { top = std::max( top, (int)g.ev.value ); now = g.ev.value; }
	CHECK( top >= 120 && std::abs( now - 64 ) <= 2, "envelope: attack to the top, decay to sustain (%d, %d)", top, now );
	e.in( L::Ev::off( 1 ) );
	e.run( 48000 );
	CHECK( e.mods().back().ev.value == 0, "release falls to zero (%d)", e.mods().back().ev.value );
	CHECK( e.ons().size() == 1 && e.offs().size() == 1, "and the note passed through" );

	Rig n;
	n.set( 0, L::kFxNoteToMod, { 2, 0, 0, 1 } );
	n.in( L::Ev::on( 1, 60, 77 ) );
	n.in( L::Ev::off( 1 ) );
	CHECK( n.mods().size() == 2 && n.mods()[0].ev.value == 77 && n.mods()[1].ev.value == 0 && n.mods()[0].ev.lane == 2,
		   "note to mod: velocity, then the default" );

	Rig m;
	m.set( 0, L::kFxModToNote, { 0, 48, 72, 0, 0 } );
	m.in( L::Ev::mod( 0, 127 ) );
	m.in( L::Ev::on( 1, 30, 100 ) );
	m.in( L::Ev::mod( 0, 0 ) );
	m.in( L::Ev::on( 2, 30, 100 ) );
	CHECK( m.ons().size() == 2 && m.ons()[0].ev.note == 72 && m.ons()[1].ev.note == 48 && m.mods().empty(),
		   "mod to note: the lane picks the pitch, and the mod stops here" );
}

// The invariant, under abuse.
static void testFuzzBalanced()
{
	uint32_t rs = 12345;
	auto rnd = [&]( int n ) { rs = rs * 1664525u + 1013904223u; return (int)( ( rs >> 8 ) % (uint32_t)n ); };
	int failures = 0;
	for ( int trial = 0; trial < 300; ++trial )
	{
		Rig r;
		r.spb = 200.f + rnd( 2000 );
		for ( int s = 0; s < L::kChainSlots; ++s )
		{
			if ( rnd( 3 ) == 0 ) continue;
			int type = 1 + rnd( L::kNumFxTypes - 1 );
			L::Fx& f = r.set( s, type );
			for ( int i = 0; i < f.desc().nParams; ++i )
				f.p[i] = f.pe[i] = (int16_t)( f.desc().p[i].min + rnd( f.desc().p[i].max - f.desc().p[i].min + 1 ) );
		}
		std::vector<uint16_t> down;
		uint16_t nextIn = 1;
		for ( int i = 0; i < 20000; ++i )
		{
			int a = rnd( 1000 );
			if ( a < 6 && down.size() < 10 ) { r.in( L::Ev::on( nextIn, 30 + rnd( 60 ), 1 + rnd( 127 ) ) ); down.push_back( nextIn++ ); }
			else if ( a < 12 && !down.empty() ) { int k = rnd( (int)down.size() ); r.in( L::Ev::off( down[k] ) ); down.erase( down.begin() + k ); }
			else if ( a == 12 ) r.in( L::Ev::mod( rnd( 6 ), rnd( 128 ) ) );
			else if ( a == 13 ) { int s = rnd( L::kChainSlots ); r.chain.setMuted( s, !r.chain.muted[s] ); }
			else if ( a == 14 ) { int s = rnd( L::kChainSlots ); r.set( s, rnd( L::kNumFxTypes ) ); }
			else if ( a == 15 ) { L::Fx& f = r.chain.fx[rnd( L::kChainSlots )]; if ( f.desc().nParams ) { int k = rnd( f.desc().nParams ); f.pe[k] = (int16_t)( f.desc().p[k].min + rnd( f.desc().p[k].max - f.desc().p[k].min + 1 ) ); } }
			r.run( 1 );
		}
		for ( uint16_t id : down ) r.in( L::Ev::off( id ) );
		r.run( (int)( r.spb * 40 ) );		// long enough for every echo and lengthened note
		r.chain.flushAll();					// and what clocked generators are still making
		std::string why;
		if ( !r.balanced( &why ) )
		{
			if ( failures++ < 5 )
			{
				std::printf( "  trial %d unbalanced (%s); chain:", trial, why.c_str() );
				for ( int s = 0; s < L::kChainSlots; ++s ) std::printf( " %s", r.chain.fx[s].desc().shortName );
				std::printf( "\n" );
			}
		}
	}
	CHECK( failures == 0, "%d of 300 fuzzed chains left a note hanging", failures );
}

// The event sources and voices.
static void testSourcesVoices()
{
	L::PatternSource ps;
	L::Pattern* p = new L::Pattern;
	p->clear();
	L::Note n1 = { 0, 12, 60, 100 }, n2 = { 6, 24, 64, 80 };
	p->add( n1 ); p->add( n2 );
	L::IdSource ids;
	std::vector<Got> got;
	uint32_t t = 0;
	ps.step( *p, 0, 480, false, 100, 1, 0, 0, ids );
	for ( ; t < 1000; ++t ) ps.tick( [&]( const L::Ev& e ) { got.push_back( Got{ t, e } ); } );
	CHECK( got.size() == 4 && got[0].at == 0 && got[1].at == 120 && got[2].at == 240 && got[3].at == 120 + 480,
		   "pattern source: on 0, on 120, off 240, off 600 (%zu events)", got.size() );
	delete p;

	L::GenSource gs;
	std::vector<L::Ev> ev;
	auto em = [&]( const L::Ev& e ) { ev.push_back( e ); };
	gs.tick( false, 60, 100, ids, em );
	gs.tick( true, 60, 100, ids, em );
	gs.tick( true, 60, 100, ids, em );
	gs.tick( true, 62, 100, ids, em );		// a tie
	gs.tick( false, 62, 100, ids, em );
	CHECK( ev.size() == 4 && ev[0].type == L::kEvNoteOn && ev[1].type == L::kEvNoteOn && ( ev[1].flags & L::kEvLegato )
		   && ev[2].type == L::kEvNoteOff && ev[2].id == ev[0].id && ev[3].id == ev[1].id, "generator source: on, legato on, off, off" );

	L::Voices v;
	v.init( 48000 );
	v.event( ev[0] );
	v.tick();
	v.event( ev[1] );
	v.event( ev[2] );
	bool stillHigh = v.gate( 0 );
	v.event( ev[3] );
	CHECK( stillHigh && !v.gate( 0 ) && v.voice[0].note == 62, "a tie through the voices is one gate" );

	L::Ev g = L::Ev::on( 50, 72, 100 );
	g.glideType = L::kGlideLinear;
	g.glideMs = 10;					// 480 samples
	v.event( g );
	for ( int i = 0; i < 240; ++i ) v.tick();
	CHECK( std::fabs( v.voice[0].pitch - 67.f ) < 0.1f, "linear glide is half way at half the time (%g)", v.voice[0].pitch );
	for ( int i = 0; i < 300; ++i ) v.tick();
	CHECK( v.voice[0].pitch == 72.f, "and arrives" );
}

int main()
{
	testPassThrough();
	testHarmonizerScaleFilter();
	testEcho();
	testArp();
	testRatchetEuclidRegister();
	testChanceBernoulliRandomizer();
	testSwingHoldGlide();
	testModEffects();
	testFuzzBalanced();
	testSourcesVoices();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
