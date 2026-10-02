#pragma once
// The sources at the head of a track's event path.
//
// PatternSource plays a written pattern. Shoal's walker hands it a step and how long a
// step is (Host::externalStep); it puts that step's notes in its queue as note-ons and
// note-offs at their exact samples, and they leave the queue, in order, as their sample
// comes. Its lanes leave as modulation values, followed as the playhead moves.
//
// GenSource reads a generator back as events, for a generator with effects on it: a
// gate rising is a note-on, falling a note-off, and a new pitch under a held gate (a
// Shoal tie) a legato note-on. A generator with no effects never goes through it: its
// gate and pitch go straight to the jacks, exactly as Shoal made them.
//
// Pure C++11, no Rack types, no allocation.
#include <stdint.h>
#include "Pattern.hpp"
#include "Events.hpp"

namespace ledger {

struct PatternSource
{
	EvQueue<160>	q;
	uint32_t		now;
	int16_t			laneLast[kNumLanes];

	PatternSource() { reset(); }

	void reset()
	{
		q.clear();
		now = 0;
		for ( int l = 0; l < kNumLanes; ++l ) laneLast[l] = kLaneUnset;
	}

	// End every sounding note now (their note-offs leave at the next tick) and drop
	// what was still to start.
	void releaseAll()
	{
		q.dropNoteOns();
		for ( int i = 0; i < q.n; ++i ) q.q[i].at = now;
	}

	// Schedule the notes of pattern step `step` (already shifted), which starts now and
	// lasts stepPeriod samples. `silent` (muted, soloed out, breathing) plays nothing.
	// CHANCE is rolled per note from (seed, pass, note), so a pass replays the same way
	// from a reset; `transpose` is in semitones.
	void step( const Pattern& p, int step, uint32_t stepPeriod, bool silent, int chance,
			   uint32_t seed, uint32_t pass, int transpose, IdSource& ids )
	{
		if ( silent || stepPeriod == 0 )
			return;
		int a = step * kTicksPerStep, b = a + kTicksPerStep;
		for ( int i = 0; i < p.count; ++i )
		{
			const Note& n = p.notes[i];
			if ( n.start < a ) continue;
			if ( n.start >= b ) break;
			if ( chance < 100
				 && (int)( shoal::hash3( seed ^ ( pass * 0x9E3779B1u ), (uint32_t)i, 0xC4A2u ) % 100 ) >= chance )
				continue;
			uint32_t delay = (uint32_t)( ( (uint64_t)( n.start - a ) * stepPeriod ) / kTicksPerStep );
			uint64_t len = ( (uint64_t)n.len * stepPeriod ) / kTicksPerStep;
			if ( len < 1 ) len = 1;
			if ( len > 0x7FFFFFFF ) len = 0x7FFFFFFF;
			uint16_t id = ids.get();
			if ( !q.push( now + delay, Ev::on( id, n.pitch + transpose, n.vel ) ) )
				continue;
			q.push( now + delay + (uint32_t)len, Ev::off( id ) );
		}
	}

	// The lanes at position x (in steps): a modulation event for each lane that has
	// points and whose value moved.
	template <typename F>
	void lanes( const Pattern& p, float x, int length, F emit )
	{
		for ( int l = 0; l < kNumLanes; ++l )
		{
			float v = laneValueAt( p, l, x, length );
			if ( v == (float)kLaneUnset ) continue;
			int16_t iv = (int16_t)lroundf( v );
			if ( iv != laneLast[l] ) { laneLast[l] = iv; emit( Ev::mod( l, iv ) ); }
		}
	}

	// One sample: everything due leaves, in order.
	template <typename F>
	void tick( F emit )
	{
		Ev e;
		while ( q.popDue( now, e ) ) emit( e );
		now += 1;
	}
};

struct GenSource
{
	bool		high;
	int			note;
	uint16_t	id;

	GenSource() : high( false ), note( -1 ), id( 0 ) {}
	void reset() { high = false; note = -1; }

	// `gateHigh` and `pitchNote` are what Shoal's track is doing this sample.
	template <typename F>
	void tick( bool gateHigh, int pitchNote, int vel, IdSource& ids, F emit )
	{
		if ( gateHigh && !high )
		{
			id = ids.get();
			emit( Ev::on( id, pitchNote, vel ) );
		}
		else if ( gateHigh && high && pitchNote != note )
		{
			// a tie: the next note arrives under the held gate
			uint16_t old = id;
			id = ids.get();
			Ev e = Ev::on( id, pitchNote, vel );
			e.flags = kEvLegato;
			emit( e );
			emit( Ev::off( old ) );
		}
		else if ( !gateHigh && high )
			emit( Ev::off( id ) );
		high = gateHigh;
		note = pitchNote;
	}
};

}
