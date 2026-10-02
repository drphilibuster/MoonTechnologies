// Ledger's MIDI without Rack (Midi.hpp): the output ports and their note counting,
// taps, live input, the clock it sends, where a recorded note lands; and the two
// effects that send MIDI from the middle of a chain.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../src/Ledger/Midi.hpp"
#include "../../src/Ledger/Effects.hpp"

namespace L = ledger;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )

static bool msgIs( const L::MidiMsg& m, int b0, int b1, int b2 )
{
	return m.b[0] == b0 && ( m.n < 2 || m.b[1] == b1 ) && ( m.n < 3 || m.b[2] == b2 );
}

static void testPort()
{
	L::MidiPort p;
	CHECK( !p.noteOn( 0, 60, 100 ) && p.n == 0, "a port with no device sends nothing" );
	p.live = true;
	p.noteOn( 2, 60, 100 );
	p.noteOn( 2, 60, 90 );		// the same note from a second source
	p.noteOff( 2, 60 );
	CHECK( p.n == 2 && msgIs( p.q[0], 0x92, 60, 100 ) && msgIs( p.q[1], 0x92, 60, 90 ),
		   "two sources on one note: two note-ons, and the first to let go sends nothing (%d msgs)", p.n );
	p.noteOff( 2, 60 );
	CHECK( p.n == 3 && msgIs( p.q[2], 0x82, 60, 0 ) && p.sounding() == 0, "the last to let go ends it" );
	p.noteOff( 2, 60 );
	CHECK( p.n == 3, "a note-off for a note not sounding is not sent" );
	p.clearQueue();
	p.noteOn( 0, 10, 1 ); p.noteOn( 15, 127, 127 ); p.noteOn( 15, 127, 127 );
	p.clearQueue();
	p.allOff();
	CHECK( p.n == 2 && p.sounding() == 0 && msgIs( p.q[0], 0x80, 10, 0 ) && msgIs( p.q[1], 0x8F, 127, 0 ),
		   "all-off: one note-off per sounding note (%d)", p.n );
	p.clearQueue();
	p.bend( 3, 0 );
	p.bend( 3, -8192 );
	p.bend( 3, 8191 );
	p.touch( 1, 200 );
	p.cc( 4, 74, 64 );
	p.realtime( 0xF8 );
	CHECK( msgIs( p.q[0], 0xE3, 0, 64 ) && msgIs( p.q[1], 0xE3, 0, 0 ) && msgIs( p.q[2], 0xE3, 127, 127 )
		   && p.q[3].n == 2 && msgIs( p.q[3], 0xD1, 127, 0 ) && msgIs( p.q[4], 0xB4, 74, 64 ) && p.q[5].n == 1 && p.q[5].b[0] == 0xF8,
		   "bend centre/ends, pressure clamped, CC, realtime" );
	// a full queue: note-offs still get in
	p.clearQueue();
	p.noteOn( 0, 1, 100 );
	for ( int i = 0; i < L::MidiPort::Q; ++i ) p.cc( 0, 1, i & 127 );
	CHECK( p.n == L::MidiPort::Q, "the queue fills" );
	p.cc( 0, 2, 0 );
	p.noteOff( 0, 1 );
	CHECK( p.n == L::MidiPort::Q && msgIs( p.q[p.n - 1], 0x80, 1, 0 ), "a full queue drops a CC, never a note-off" );
}

static void testTap()
{
	L::MidiPort ports[2];
	ports[0].live = ports[1].live = true;
	L::MidiMap map;
	map.modCC[1] = 74;
	L::MidiTap tap;
	tap.event( ports, 1, 5, L::Ev::on( 9, 64, 100 ), &map, true, true, 50 );
	CHECK( ports[1].n == 1 && msgIs( ports[1].q[0], 0x95, 64, 50 ), "a tap sounds on its port and channel, velocity scaled" );
	tap.event( ports, 1, 5, L::Ev::off( 3 ), &map );
	CHECK( ports[1].n == 1, "an id it never sounded is not its to end" );
	tap.event( ports, 0, 0, L::Ev::off( 9 ), &map );
	CHECK( ports[1].n == 2 && msgIs( ports[1].q[1], 0x85, 64, 0 ) && ports[0].n == 0,
		   "a note-off ends the note where it was sounded, whatever port is asked now" );
	tap.event( ports, 0, 2, L::Ev::mod( 0, 33 ), &map );
	tap.event( ports, 0, 2, L::Ev::mod( 0, 33 ), &map );
	tap.event( ports, 0, 2, L::Ev::mod( 1, 100 ), &map );
	tap.event( ports, 0, 2, L::Ev::mod( 2, 100 ), &map );		// MOD 3 sends nothing by default
	tap.event( ports, 0, 2, L::Ev::mod( 4, -8192 ), &map );
	tap.event( ports, 0, 2, L::Ev::mod( 5, 77 ), &map );
	CHECK( ports[0].n == 4 && msgIs( ports[0].q[0], 0xB2, 1, 33 ) && msgIs( ports[0].q[1], 0xB2, 74, 100 )
		   && msgIs( ports[0].q[2], 0xE2, 0, 0 ) && msgIs( ports[0].q[3], 0xD2, 77, 0 ),
		   "lanes: MOD 1 CC1, MOD 2 its CC, repeats not resent, bend, pressure (%d msgs)", ports[0].n );
	ports[0].clearQueue();
	tap.on( ports, 0, 0, L::Ev::on( 20, 50, 100 ), 100 );
	tap.on( ports, 0, 0, L::Ev::on( 21, 51, 100 ), 100 );
	tap.flush();
	CHECK( ports[0].sounding() == 0 && ports[0].n == 4 && tap.n == 0, "flush ends everything the tap sounded" );
	L::MidiPort dead[2];
	L::MidiTap t2;
	t2.on( dead, 0, 0, L::Ev::on( 1, 60, 100 ), 100 );
	CHECK( t2.n == 0, "a note a dead port would not take is not remembered" );
}

static void testLiveIn()
{
	L::LiveIn in;
	L::IdSource ids;
	std::vector<L::Ev> ev;
	auto em = [&]( const L::Ev& e ) { ev.push_back( e ); };
	auto msg = [&]( int a, int b, int c ) { uint8_t m[3] = { (uint8_t)a, (uint8_t)b, (uint8_t)c }; return in.message( m, ids, em ); };
	msg( 0x90, 60, 100 );
	msg( 0x91, 60, 80 );		// the same key on another channel is another note
	msg( 0x90, 60, 0 );			// velocity 0: off
	msg( 0x81, 60, 0 );
	CHECK( ev.size() == 4 && ev[0].type == L::kEvNoteOn && ev[1].type == L::kEvNoteOn && ev[0].id != ev[1].id
		   && ev[2].type == L::kEvNoteOff && ev[2].id == ev[0].id && ev[3].id == ev[1].id, "notes pair by channel and key" );
	ev.clear();
	msg( 0x90, 62, 100 );
	msg( 0x90, 62, 90 );		// struck again while held
	msg( 0x80, 62, 0 );
	CHECK( ev.size() == 4 && ev[1].type == L::kEvNoteOff && ev[1].id == ev[0].id && ev[3].id == ev[2].id && in.n == 0,
		   "a key struck again ends its first note" );
	ev.clear();
	msg( 0xB0, 64, 127 );
	msg( 0x90, 64, 100 );
	msg( 0x80, 64, 0 );
	CHECK( ev.size() == 1, "under the pedal a released key keeps sounding" );
	msg( 0x90, 64, 100 );		// played again under the pedal
	CHECK( ev.size() == 3 && ev[1].type == L::kEvNoteOff && ev[1].id == ev[0].id, "and is ended when struck again" );
	msg( 0x90, 65, 100 );
	msg( 0x80, 65, 0 );
	msg( 0xB0, 64, 0 );
	CHECK( ev.size() == 5 && ev[4].type == L::kEvNoteOff && ev[4].id == ev[3].id, "pedal up ends the released notes (%zu)", ev.size() );
	CHECK( in.n == 1, "...but not a key still down" );
	msg( 0x80, 64, 0 );
	CHECK( in.n == 0, "which ends when let go" );
	ev.clear();
	bool modOk = msg( 0xB3, 1, 99 );
	bool other = msg( 0xB3, 74, 10 );
	msg( 0xE3, 0, 64 );
	msg( 0xE3, 0, 0 );
	msg( 0xD3, 55, 0 );
	CHECK( modOk && !other && ev.size() == 4 && ev[0].type == L::kEvMod && ev[0].lane == 0 && ev[0].value == 99
		   && ev[1].lane == 4 && ev[1].value == 0 && ev[2].value == -8192 && ev[3].lane == 5 && ev[3].value == 55,
		   "mod CC, bend, pressure become lanes; another CC is the caller's" );
	ev.clear();
	msg( 0x90, 40, 1 ); msg( 0x95, 41, 1 );
	in.flush( em );
	CHECK( ev.size() == 4 && in.n == 0, "flush ends every held note" );
}

static void testClock()
{
	L::ClockOut c;
	std::vector<uint32_t> at;
	float period = 1000.f;
	uint32_t now = 0;
	for ( int beat = 0; beat < 100; ++beat )
		for ( int s = 0; s < 1000; ++s, ++now )
			c.tick( s == 0, now, period, [&]() { at.push_back( now ); } );
	bool even = true;
	for ( size_t i = 0; i < at.size(); ++i )
	{
		uint32_t want = (uint32_t)( i / 24 ) * 1000 + (uint32_t)std::ceil( ( i % 24 ) * 1000.0 / 24.0 );
		if ( at[i] != want ) { even = false; std::printf( "  pulse %zu at %u, wanted %u\n", i, at[i], want ); break; }
	}
	CHECK( at.size() == 2400 && even, "24 pulses a beat, the first on the beat, the rest spread evenly (%zu)", at.size() );
	// the clock speeds up: a beat ends early; it still has 24
	L::ClockOut d;
	int n = 0;
	now = 0;
	for ( int beat = 0; beat < 50; ++beat )
	{
		int len = 1000 - beat * 15;
		for ( int s = 0; s < len; ++s, ++now )
			d.tick( s == 0, now, 1000.f, [&]() { ++n; } );		// a stale period: every beat is short
	}
	d.tick( true, now, 1000.f, [&]() { ++n; } );
	CHECK( n == 50 * 24 + 1, "a beat that ends early sends what it had left: %d pulses for 50 beats", n );
	// slows down: no 25th pulse
	L::ClockOut e;
	n = 0;
	now = 0;
	for ( int s = 0; s < 3000; ++s, ++now ) e.tick( s == 0, now, 1000.f, [&]() { ++n; } );
	CHECK( n == 24, "a late beat waits at 24, it never runs ahead (%d)", n );
}

static void testRecMath()
{
	CHECK( L::recTick( 0, 0, 480, 24 ) == 0 && L::recTick( 3, 240, 480, 24 ) == 3 * 24 + 12
		   && L::recTick( 2, 479, 480, 24 ) == 2 * 24 + 23 && L::recTick( 2, 600, 480, 24 ) == 2 * 24 + 23,
		   "a played note's tick: step and how far into it" );
	CHECK( L::recLen( 480, 480, 24, 1536 ) == 24 && L::recLen( 0, 480, 24, 1536 ) == 1 && L::recLen( 9, 480, 24, 1536 ) == 0 + 1
		   && L::recLen( 10, 480, 24, 1536 ) == 1 && L::recLen( 100000, 480, 24, 1536 ) == 1536,
		   "a held note's length, in ticks, rounded, at least one" );
	CHECK( L::looperLength( 7, 0.4f, 64 ) == 7 && L::looperLength( 7, 0.6f, 64 ) == 8 && L::looperLength( 0, 0.1f, 64 ) == 1
		   && L::looperLength( 70, 0.f, 64 ) == 64, "the looper's length is to the nearest step" );
}

// A chain with MIDI ports.
struct Rig
{
	L::Chain chain;
	L::IdSource ids;
	L::MidiPort ports[2];
	L::MidiMap map;
	std::vector<L::Ev> got;
	uint32_t now = 0;
	static void sink( void* self, const L::Ev& e ) { ( (Rig*)self )->got.push_back( e ); }
	Rig()
	{
		chain.ids = &ids;
		chain.sink = &Rig::sink;
		chain.sinkCtx = this;
		chain.ctx.midi = ports;
		chain.ctx.midiMap = &map;
		ports[0].live = ports[1].live = true;
		sync();
	}
	void sync()
	{
		chain.ctx.now = now;
		chain.ctx.beat = now / 1000.0;
		chain.ctx.spb = 1000.f;
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
	int notesOn() const { int n = 0; for ( auto& e : got ) n += e.type == L::kEvNoteOn; return n; }
};

static void testMidiEffects()
{
	{
		Rig r;
		r.set( 0, L::kFxHarmonizer, { 1, 7, 0, 0, 0 } );
		r.set( 1, L::kFxMidiOut, { 1, 10, 1, 1, 50 } );		// Out B, channel 10, notes, mod, half velocity
		r.set( 2, L::kFxHarmonizer, { 1, 12, 0, 0, 0 } );
		r.in( L::Ev::on( 1, 60, 100 ) );
		CHECK( r.ports[1].n == 2 && msgIs( r.ports[1].q[0], 0x99, 60, 50 ) && msgIs( r.ports[1].q[1], 0x99, 67, 50 )
			   && r.ports[0].n == 0, "MIDI output taps the chain where it sits: the fifth, not the octave (%d)", r.ports[1].n );
		CHECK( r.notesOn() == 4, "...and passes everything on (%d)", r.notesOn() );
		r.in( L::Ev::mod( 0, 20 ) );
		CHECK( r.ports[1].n == 3 && msgIs( r.ports[1].q[2], 0xB9, 1, 20 ), "lanes go out as the track maps them" );
		r.in( L::Ev::off( 1 ) );
		CHECK( r.ports[1].sounding() == 0, "the note-off ends what it sent" );
		r.in( L::Ev::on( 2, 62, 100 ) );
		r.chain.setMuted( 1, true );
		CHECK( r.ports[1].sounding() == 0, "muting it ends what it was sounding" );
		r.chain.setMuted( 1, false );
		r.in( L::Ev::off( 2 ) );
		r.in( L::Ev::on( 3, 62, 100 ) );
		r.chain.setType( 1, L::kFxNone );
		CHECK( r.ports[1].sounding() == 0, "so does removing it" );
		r.chain.setType( 1, L::kFxMidiOut );
		r.chain.fx[1].p[2] = r.chain.fx[1].pe[2] = 0;
		r.in( L::Ev::on( 4, 62, 100 ) );
		CHECK( r.ports[0].n == 0, "with notes off it sends no notes" );
	}
	{
		Rig r;
		r.set( 0, L::kFxBernoulli, { 100, 1, 3 } );		// everything to B: MIDI Out A, channel 3
		for ( int i = 0; i < 5; ++i ) r.in( L::Ev::on( (uint16_t)( 10 + i ), 60 + i, 100 ) );
		CHECK( r.notesOn() == 0 && r.ports[0].sounding() == 5 && msgIs( r.ports[0].q[0], 0x92, 60, 100 ),
			   "Bernoulli at 100%%: every note leaves for B's port, none goes on" );
		for ( int i = 0; i < 5; ++i ) r.in( L::Ev::off( (uint16_t)( 10 + i ) ) );
		CHECK( r.ports[0].sounding() == 0, "and their note-offs follow them there" );
		r.chain.fx[0].pe[0] = 0;
		r.in( L::Ev::on( 20, 70, 100 ) );
		r.in( L::Ev::off( 20 ) );
		CHECK( r.ports[0].n == 10 && r.notesOn() == 1 && r.got.back().type == L::kEvNoteOff, "at 0%% all of it goes on (path A)" );
		r.chain.fx[0].pe[0] = 50;
		int toB = 0;
		for ( int i = 0; i < 1000; ++i )
		{
			int before = r.ports[0].sounding();
			r.in( L::Ev::on( (uint16_t)( 100 + i ), 60, 100 ) );
			toB += r.ports[0].sounding() - before;
			r.in( L::Ev::off( (uint16_t)( 100 + i ) ) );
		}
		CHECK( toB > 430 && toB < 570 && r.ports[0].sounding() == 0, "at 50%% about half go to B (%d of 1000)", toB );
		r.chain.fx[0].pe[1] = 0;
		r.in( L::Ev::on( 2000, 60, 100 ) );
		CHECK( r.ports[0].sounding() == 0, "B dropped: nothing to MIDI" );
	}
	{
		// a port that has no device: the effects do nothing there and remember nothing
		Rig r;
		r.ports[0].live = false;
		r.set( 0, L::kFxMidiOut, { 0, 1, 1, 1, 100 } );
		r.in( L::Ev::on( 1, 60, 100 ) );
		CHECK( r.chain.fx[0].tap.n == 0 && r.notesOn() == 1, "no device: the tap holds nothing, the chain goes on" );
	}
}

// The fuzz: random chains with the MIDI effects in them, notes in and out, slots muted,
// swapped and changed -- every MIDI note sounded is ended.
static void testFuzz()
{
	uint32_t rs = 777;
	auto rnd = [&]( int n ) { rs = rs * 1664525u + 1013904223u; return (int)( ( rs >> 8 ) % (uint32_t)n ); };
	int failures = 0;
	const int kinds[] = { L::kFxMidiOut, L::kFxBernoulli, L::kFxHarmonizer, L::kFxEcho, L::kFxArp, L::kFxRatchet,
						  L::kFxHold, L::kFxRandomizer, L::kFxChance, L::kFxSwing, L::kFxEuclid, L::kFxRegister };
	for ( int trial = 0; trial < 200; ++trial )
	{
		Rig r;
		for ( int s = 0; s < L::kChainSlots; ++s )
		{
			if ( rnd( 3 ) == 0 ) continue;
			L::Fx& f = r.set( s, kinds[rnd( sizeof kinds / sizeof kinds[0] )] );
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
			else if ( a == 12 ) { int s = rnd( L::kChainSlots ); r.chain.setMuted( s, !r.chain.muted[s] ); }
			else if ( a == 13 ) { int s = rnd( L::kChainSlots ); r.set( s, kinds[rnd( sizeof kinds / sizeof kinds[0] )] ); }
			else if ( a == 14 ) { L::Fx& f = r.chain.fx[rnd( L::kChainSlots )]; if ( f.desc().nParams ) { int k = rnd( f.desc().nParams ); f.pe[k] = (int16_t)( f.desc().p[k].min + rnd( f.desc().p[k].max - f.desc().p[k].min + 1 ) ); } }
			else if ( a == 15 ) r.in( L::Ev::mod( rnd( 6 ), rnd( 128 ) ) );
			r.run( 1 );
			r.ports[0].clearQueue(); r.ports[1].clearQueue();
		}
		for ( uint16_t id : down ) r.in( L::Ev::off( id ) );
		r.run( 40000 );
		r.chain.flushAll();
		int left = r.ports[0].sounding() + r.ports[1].sounding();
		if ( left && failures++ < 5 )
		{
			std::printf( "  trial %d: %d MIDI notes left sounding; chain:", trial, left );
			for ( int s = 0; s < L::kChainSlots; ++s ) std::printf( " %s", r.chain.fx[s].desc().shortName );
			std::printf( "\n" );
		}
	}
	CHECK( failures == 0, "%d of 200 fuzzed chains left a MIDI note sounding", failures );
}

int main()
{
	testPort();
	testTap();
	testLiveIn();
	testClock();
	testRecMath();
	testMidiEffects();
	testFuzz();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
