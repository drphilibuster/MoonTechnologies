#pragma once
// What moves between a track's source, its effects and its voices: note-ons, note-offs
// and modulation values, each stamped with when it is due.
//
// Every note carries an id. A source gives each note-on a fresh one and its note-off the
// same; an effect that makes new notes (a harmony, an echo, an arpeggio) gives those new
// ids and remembers which input they came from, so the input's note-off can end every
// one of them. Voices find the note a note-off ends by id, never by pitch: an effect is
// free to move a pitch without stranding its note-off.
//
// Pure C++11, no Rack types, no allocation.
#include <stdint.h>
#include <string.h>

namespace ledger {

struct MidiPort;
struct MidiMap;

enum EvType { kEvNoteOn, kEvNoteOff, kEvMod };

enum { kEvLegato = 1 };			// a note-on that continues a sounding voice (Shoal's tie)

struct Ev
{
	uint8_t		type;
	uint8_t		note;			// note-on: MIDI pitch
	uint8_t		vel;			// note-on: 1..127
	uint8_t		lane;			// mod: which lane (Lane in Pattern.hpp)
	uint16_t	id;				// notes: which note
	int16_t		value;			// mod: the lane's value (MOD/TOUCH 0..127, BEND -8192..8191)
	uint16_t	glideMs;		// note-on: glide into this note over this long (0 = jump)
	uint8_t		glideType;		// note-on: Glide's TYPE
	uint8_t		flags;			// kEvLegato

	static Ev on( uint16_t id, int note, int vel )
	{
		Ev e;
		memset( &e, 0, sizeof e );
		e.type = kEvNoteOn;
		e.id = id;
		e.note = (uint8_t)( note < 0 ? 0 : note > 127 ? 127 : note );
		e.vel = (uint8_t)( vel < 1 ? 1 : vel > 127 ? 127 : vel );
		return e;
	}
	static Ev off( uint16_t id )
	{
		Ev e;
		memset( &e, 0, sizeof e );
		e.type = kEvNoteOff;
		e.id = id;
		return e;
	}
	static Ev mod( int lane, int value )
	{
		Ev e;
		memset( &e, 0, sizeof e );
		e.type = kEvMod;
		e.lane = (uint8_t)lane;
		e.value = (int16_t)value;
		return e;
	}
};

// Events waiting for their sample. When full, a new note-on is dropped rather than a
// note-off: a lost note-on is a missed note, a lost note-off is a hung one.
template <int N>
struct EvQueue
{
	struct Item { uint32_t at; uint32_t seq; Ev ev; };
	Item		q[N];
	int			n;
	uint32_t	seq;

	EvQueue() : n( 0 ), seq( 0 ) {}
	void clear() { n = 0; }

	bool push( uint32_t at, const Ev& ev )
	{
		if ( n >= N )
		{
			if ( ev.type == kEvNoteOn ) return false;
			// make room: drop a waiting note-on (and so never a note-off)
			int victim = -1;
			for ( int i = 0; i < n; ++i ) if ( q[i].ev.type == kEvNoteOn ) { victim = i; break; }
			if ( victim < 0 ) return false;
			q[victim] = q[--n];
		}
		q[n].at = at;
		q[n].seq = seq++;
		q[n].ev = ev;
		++n;
		return true;
	}

	// The earliest due item (time, then order pushed), removed into `out`; false if none.
	bool popDue( uint32_t now, Ev& out )
	{
		int best = -1;
		for ( int i = 0; i < n; ++i )
		{
			if ( (int32_t)( q[i].at - now ) > 0 ) continue;
			if ( best < 0 || (int32_t)( q[i].at - q[best].at ) < 0
				 || ( q[i].at == q[best].at && (int32_t)( q[i].seq - q[best].seq ) < 0 ) )
				best = i;
		}
		if ( best < 0 ) return false;
		out = q[best].ev;
		q[best] = q[--n];
		return true;
	}

	// Remove every waiting note-on, and every waiting event for note `id` (or all ids).
	void dropNoteOns()
	{
		for ( int i = 0; i < n; )
			if ( q[i].ev.type == kEvNoteOn ) q[i] = q[--n]; else ++i;
	}
};

// What an effect knows about time: the sample, the master clock (one tick = one beat,
// a quarter note), and how long a beat is.
struct TimeCtx
{
	uint32_t	now;			// samples
	double		beat;			// master ticks since the clock started (fractional)
	float		spb;			// samples per beat
	uint32_t	sampleRate;
	int			root, scale;	// the books, for SCALE and the generator effects
	bool		running;
	MidiPort*		midi;		// Out A and Out B, for the MIDI output effect and Bernoulli (may be NULL)
	const MidiMap*	midiMap;	// the track's lane-to-CC map
};

// A source of fresh note ids, one per track.
struct IdSource
{
	uint16_t next;
	IdSource() : next( 1 ) {}
	uint16_t get() { uint16_t i = next++; if ( next == 0 ) next = 1; return i; }
};

}
