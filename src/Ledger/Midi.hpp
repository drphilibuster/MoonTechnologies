#pragma once
// Ledger's MIDI, without Rack: what goes out of the two output ports, what comes in
// from the input, the clock it sends, and where a recorded note lands in a pattern.
//
// Out. A MidiPort is one output (Out A, Out B): a queue of messages the module sends
// at the end of each sample, and a count of how many sources hold each (channel,
// note). Two sources sounding the same note on the same channel each send their
// note-on; only the last of them to let go sends the note-off. allOff() ends every
// note the port has sounding, which is what a removed effect, a changed port or a
// panic needs.
//
// A MidiTap turns a stream of events (Events.hpp) into MIDI on one port and channel.
// A note-off carries only an id, so the tap remembers which note each id started;
// its flush() ends them. The track's own output is a tap at the end of the chain, and
// the MIDI output effect and Bernoulli's B path are taps in the middle of it.
//
// In. A LiveIn turns one track's channel messages into events with ids, the way a
// pattern's notes are: note-ons and note-offs, the mod CC as the MOD 1 lane, pitch
// bend as BEND and channel pressure as TOUCH. It holds note-offs under the sustain
// pedal.
//
// Pure C++11, no Rack types, no allocation.
#include <stdint.h>
#include <string.h>
#include "Events.hpp"

namespace ledger {

enum { kMidiPorts = 2, kModCCs = 4 };
static const char* const midiPortNames[kMidiPorts] = { "Out A", "Out B" };

// Lanes, as Pattern.hpp numbers them.
enum { kMidiLaneMod1 = 0, kMidiLaneBend = 4, kMidiLaneTouch = 5 };

struct MidiMsg
{
	uint8_t b[3];
	uint8_t n;
};

struct MidiPort
{
	enum { Q = 512 };
	MidiMsg		q[Q];
	int			n;
	bool		live;				// a device is listening; otherwise nothing is queued
	uint8_t		count[16][128];		// how many sources hold each note

	MidiPort() : n( 0 ), live( false ) { memset( count, 0, sizeof count ); }

	void raw( uint8_t b0, uint8_t b1, uint8_t b2, int len )
	{
		if ( !live ) return;
		// a full queue loses anything but a note-off; a note-off displaces the oldest
		// message that is not one
		bool isOff = ( b0 & 0xF0 ) == 0x80;
		if ( n >= Q )
		{
			if ( !isOff ) return;
			int victim = -1;
			for ( int i = 0; i < n; ++i ) if ( ( q[i].b[0] & 0xF0 ) != 0x80 ) { victim = i; break; }
			if ( victim < 0 ) return;
			for ( int i = victim; i < n - 1; ++i ) q[i] = q[i + 1];
			--n;
		}
		q[n].b[0] = b0; q[n].b[1] = b1; q[n].b[2] = b2; q[n].n = (uint8_t)len;
		++n;
	}

	bool noteOn( int ch, int note, int vel )
	{
		if ( !live ) return false;
		ch &= 15; note &= 127;
		if ( vel < 1 ) vel = 1;
		if ( vel > 127 ) vel = 127;
		if ( count[ch][note] < 255 ) count[ch][note]++;
		raw( (uint8_t)( 0x90 | ch ), (uint8_t)note, (uint8_t)vel, 3 );
		return true;
	}

	void noteOff( int ch, int note )
	{
		ch &= 15; note &= 127;
		if ( !count[ch][note] ) return;
		if ( --count[ch][note] == 0 )
			raw( (uint8_t)( 0x80 | ch ), (uint8_t)note, 0, 3 );
	}

	void cc( int ch, int num, int val )
	{
		raw( (uint8_t)( 0xB0 | ( ch & 15 ) ), (uint8_t)( num & 127 ), (uint8_t)( val < 0 ? 0 : val > 127 ? 127 : val ), 3 );
	}
	// value -8192..8191
	void bend( int ch, int value )
	{
		int v = value + 8192;
		if ( v < 0 ) v = 0;
		if ( v > 16383 ) v = 16383;
		raw( (uint8_t)( 0xE0 | ( ch & 15 ) ), (uint8_t)( v & 127 ), (uint8_t)( v >> 7 ), 3 );
	}
	void touch( int ch, int val )
	{
		raw( (uint8_t)( 0xD0 | ( ch & 15 ) ), (uint8_t)( val < 0 ? 0 : val > 127 ? 127 : val ), 0, 2 );
	}
	void realtime( uint8_t byte ) { raw( byte, 0, 0, 1 ); }
	void program( int ch, int pc ) { raw( (uint8_t)( 0xC0 | ( ch & 15 ) ), (uint8_t)( pc & 127 ), 0, 2 ); }

	// End every note this port has sounding.
	void allOff()
	{
		for ( int ch = 0; ch < 16; ++ch )
			for ( int nt = 0; nt < 128; ++nt )
				if ( count[ch][nt] )
				{
					count[ch][nt] = 0;
					raw( (uint8_t)( 0x80 | ch ), (uint8_t)nt, 0, 3 );
				}
	}
	int sounding() const
	{
		int s = 0;
		for ( int ch = 0; ch < 16; ++ch ) for ( int nt = 0; nt < 128; ++nt ) s += count[ch][nt];
		return s;
	}
	void clearQueue() { n = 0; }
};

// Where a track's MOD 1-4 lanes go as MIDI: a CC number each, or -1 for nowhere. BEND
// is pitch bend and TOUCH channel pressure.
struct MidiMap
{
	int8_t modCC[kModCCs];
	MidiMap() { modCC[0] = 1; modCC[1] = -1; modCC[2] = -1; modCC[3] = -1; }
};

struct MidiTap
{
	enum { N = 64 };
	struct E { uint16_t id; uint8_t port, ch, note; };
	E			e[N];
	int			n;
	int16_t		last[6];	// the value each lane last sent, so a repeat is not resent
	MidiPort*	ports;		// where its notes went, for flush()

	MidiTap() : ports( 0 ) { reset(); }
	void reset() { n = 0; for ( int l = 0; l < 6; ++l ) last[l] = INT16_MIN; }

	bool holds( uint16_t id ) const { for ( int i = 0; i < n; ++i ) if ( e[i].id == id ) return true; return false; }

	// Sound note-on `ev` on (port, ch) at velocity `vel`.
	void on( MidiPort* ports, int port, int ch, const Ev& ev, int vel )
	{
		if ( !ports || port < 0 || port >= kMidiPorts || n >= N ) return;
		if ( !ports[port].noteOn( ch, ev.note, vel ) ) return;
		this->ports = ports;
		e[n].id = ev.id; e[n].port = (uint8_t)port; e[n].ch = (uint8_t)( ch & 15 ); e[n].note = ev.note;
		++n;
	}
	// End the note `id` started; false if this tap never sounded it.
	bool off( MidiPort* ports, uint16_t id )
	{
		if ( !ports ) ports = this->ports;
		bool any = false;
		for ( int i = 0; i < n; )
			if ( e[i].id == id )
			{
				if ( ports ) ports[e[i].port].noteOff( e[i].ch, e[i].note );
				e[i] = e[--n];
				any = true;
			}
			else ++i;
		return any;
	}
	// A lane's value, as `map` sends it.
	void mod( MidiPort* ports, int port, int ch, const Ev& ev, const MidiMap* map )
	{
		if ( !ports || port < 0 || port >= kMidiPorts || ev.lane >= 6 ) return;
		if ( last[ev.lane] == ev.value ) return;
		last[ev.lane] = ev.value;
		MidiPort& p = ports[port];
		if ( ev.lane == kMidiLaneBend ) p.bend( ch, ev.value );
		else if ( ev.lane == kMidiLaneTouch ) p.touch( ch, ev.value );
		else if ( map && map->modCC[ev.lane] >= 0 ) p.cc( ch, map->modCC[ev.lane], ev.value );
	}
	// A whole event: a note-on at its own velocity scaled by `velPct`.
	void event( MidiPort* ports, int port, int ch, const Ev& ev, const MidiMap* map, bool notes = true,
				bool mods = true, int velPct = 100 )
	{
		if ( ev.type == kEvNoteOn ) { if ( notes ) on( ports, port, ch, ev, ( ev.vel * velPct + 50 ) / 100 ); }
		else if ( ev.type == kEvNoteOff ) off( ports, ev.id );
		else if ( mods ) mod( ports, port, ch, ev, map );
	}
	// End everything it has sounding (on `p`, or where its notes went).
	void flush( MidiPort* p = 0 )
	{
		if ( !p ) p = ports;
		for ( int i = 0; i < n; ++i )
			if ( p ) p[e[i].port].noteOff( e[i].ch, e[i].note );
		n = 0;
		for ( int l = 0; l < 6; ++l ) last[l] = INT16_MIN;
	}
};

// One track's live input.
struct LiveIn
{
	enum { N = 32 };
	struct H { uint8_t ch, note; uint16_t id; bool sustained; };
	H		h[N];
	int		n;
	bool	pedal[16];
	int		modCC;			// the CC that plays the MOD 1 lane (Hermod's INPUT MIDI MOD CC)

	LiveIn() : n( 0 ), modCC( 1 ) { for ( int c = 0; c < 16; ++c ) pedal[c] = false; }

	// One channel message. Emits note-ons, note-offs and lane values; returns false for
	// what it does not play (other CCs, program changes), which the caller handles.
	template <typename F>
	bool message( const uint8_t* b, IdSource& ids, F emit )
	{
		int st = b[0] & 0xF0, ch = b[0] & 15;
		switch ( st )
		{
			case 0x90:
				if ( b[2] > 0 )
				{
					// the same key again while held: end the old one first
					release( ch, b[1], emit, true );
					if ( n >= N ) return true;
					uint16_t id = ids.get();
					h[n].ch = (uint8_t)ch; h[n].note = b[1]; h[n].id = id; h[n].sustained = false;
					++n;
					emit( Ev::on( id, b[1], b[2] ) );
					return true;
				}
				// fall through: velocity 0 is a note-off
			case 0x80:
				release( ch, b[1], emit, false );
				return true;
			case 0xB0:
				if ( b[1] == 64 )
				{
					bool down = b[2] >= 64;
					if ( pedal[ch] && !down )
					{
						for ( int i = 0; i < n; )
						{
							if ( h[i].ch == ch && h[i].sustained ) { emit( Ev::off( h[i].id ) ); h[i] = h[--n]; }
							else ++i;
						}
					}
					pedal[ch] = down;
					return true;
				}
				if ( (int)b[1] == modCC ) { emit( Ev::mod( kMidiLaneMod1, b[2] ) ); return true; }
				return false;
			case 0xE0:
				emit( Ev::mod( kMidiLaneBend, ( b[1] | ( b[2] << 7 ) ) - 8192 ) );
				return true;
			case 0xD0:
				emit( Ev::mod( kMidiLaneTouch, b[1] ) );
				return true;
			default:
				return false;
		}
	}

	// Key (ch, note) let go. Under the pedal it keeps sounding; `force` ends it anyway.
	template <typename F>
	void release( int ch, int note, F emit, bool force )
	{
		for ( int i = 0; i < n; ++i )
			if ( h[i].ch == ch && h[i].note == note )
			{
				if ( pedal[ch] && !force ) { h[i].sustained = true; continue; }
				emit( Ev::off( h[i].id ) );
				h[i] = h[--n];
				return;
			}
	}

	// End every held note.
	template <typename F>
	void flush( F emit )
	{
		for ( int i = 0; i < n; ++i ) emit( Ev::off( h[i].id ) );
		n = 0;
		for ( int c = 0; c < 16; ++c ) pedal[c] = false;
	}
};

// MIDI clock out: 24 pulses a beat, the first on the master tick and the rest spread
// across the beat's measured length. A beat that ends early (the clock sped up) sends
// what it had left at once, so every beat is exactly 24 pulses and the far end never
// drifts.
struct ClockOut
{
	int			sent;
	uint32_t	beatStart;
	bool		started;

	ClockOut() { stop(); }
	void stop() { sent = 0; beatStart = 0; started = false; }

	template <typename F>
	void tick( bool newTick, uint32_t now, float period, F pulse )
	{
		if ( newTick )
		{
			if ( started ) while ( sent < 24 ) { pulse(); ++sent; }
			started = true;
			beatStart = now;
			pulse();
			sent = 1;
			return;
		}
		if ( !started ) return;
		double el = (double)( now - beatStart ) * 24.0;
		while ( sent < 24 && el >= (double)sent * period ) { pulse(); ++sent; }
	}
};

// Clock-out modes for a port (Hermod's per-port SYNC OUTPUT).
enum { kClkOutOff, kClkOutAll, kClkOutClock, kClkOutTransport, kNumClkOut };
static const char* const clkOutNames[kNumClkOut] = { "Off", "Clock and transport", "Clock only", "Transport only" };

// Recording.
enum RecMode { kRecOverdub, kRecReplace, kRecLooper, kNumRecModes };
static const char* const recModeNames[kNumRecModes] = {
	"Overdub (add to the pattern)", "Replace (each step passed is cleared first)",
	"Looper (a new loop, its length set when recording stops)",
};

// Where, in pattern ticks, a note played `elapsed` samples into step `step` (a step is
// `period` samples) lands.
static inline int recTick( int step, uint32_t elapsed, uint32_t period, int ticksPerStep )
{
	int f = period ? (int)( ( (uint64_t)elapsed * ticksPerStep ) / period ) : 0;
	if ( f >= ticksPerStep ) f = ticksPerStep - 1;
	return step * ticksPerStep + f;
}

// How many ticks a note held for `samples` lasts, at `period` samples a step.
static inline int recLen( uint32_t samples, uint32_t period, int ticksPerStep, int maxTicks )
{
	int len = period ? (int)( ( (uint64_t)samples * ticksPerStep + period / 2 ) / period ) : 1;
	if ( len < 1 ) len = 1;
	if ( len > maxTicks ) len = maxTicks;
	return len;
}

// The looper's length, in steps, when recording stops `steps` whole steps and `frac`
// of one into the loop: to the nearest step, 1..maxSteps.
static inline int looperLength( int steps, float frac, int maxSteps )
{
	int l = steps + ( frac >= 0.5f ? 1 : 0 );
	if ( l < 1 ) l = 1;
	if ( l > maxSteps ) l = maxSteps;
	return l;
}

}
