#pragma once
// The end of a track's event path: note-ons and note-offs become gates and pitches on
// 1..kMaxVoices voices, and modulation values become the track's lanes.
//
// Which voice a note takes is Hermod+'s allocator choice (Alloc). A note that lands on
// a voice still sounding is legato -- the gate stays high and the pitch moves -- unless
// GATE RETRIG is on, when the gate dips for 1 ms so an envelope restarts. A note-on may
// carry a glide (the Glide effect): the voice's pitch then travels from where it was.
//
// Pure C++11, no Rack types, no allocation.
#include <math.h>
#include <stdint.h>
#include "Events.hpp"

namespace ledger {

enum Alloc { kAllocLRU, kAllocPoly, kAllocFirst, kAllocCyclic, kAllocRandom, kNumAllocs };
static const char* const allocNames[kNumAllocs] = {
	"Longest free (POLYLRU)", "Same note keeps its voice (POLY)", "First free, else drop (FIRST)",
	"Round robin (CYCLIC)", "Random",
};

enum GlideType { kGlideNone, kGlideLinear, kGlideExp, kGlideSmooth, kNumGlides };
static const char* const glideNames[kNumGlides] = { "None", "Linear", "Exponential", "Smooth" };

enum { kMaxVoices = 8, kLanes = 6 };

struct Voice
{
	uint8_t		note;
	uint8_t		vel;
	bool		on;			// gate wanted high
	bool		used;		// has ever played: Freeze holds it
	uint16_t	id;			// the note it is playing
	uint32_t	onAt;		// when it last started (for stealing the oldest)
	uint32_t	offAt;		// when it last ended (for the longest-free)
	uint32_t	low;		// samples of forced-low gate left: a retrigger
	uint32_t	lowAt;		// the sample the dip began: it does not count down that sample
	// pitch, in semitones, and a glide towards `note`
	float		pitch;
	float		from;
	uint32_t	gElapsed, gDur;
	uint8_t		gType;
};

struct Voices
{
	Voice		voice[kMaxVoices];
	int			voices;
	int			alloc;
	bool		retrig;
	uint32_t	retrigGap;
	uint32_t	rng;
	int			cyc;
	uint32_t	now;
	uint8_t		lastVel;
	uint32_t	sampleRate;
	int16_t		lane[kLanes];	// the last value each lane reached; kLaneUnset-like INT16_MIN if never
	bool		laneSet[kLanes];

	Voices() { init( 48000 ); }

	void init( uint32_t sr )
	{
		voices = 1;
		alloc = kAllocLRU;
		retrig = false;
		rng = 0x2545F491u;
		cyc = -1;
		lastVel = 100;
		setSampleRate( sr );
		reset();
	}

	void setSampleRate( uint32_t sr )
	{
		sampleRate = sr ? sr : 48000;
		retrigGap = sampleRate / 1000;
		if ( retrigGap < 1 ) retrigGap = 1;
	}

	void reset()
	{
		now = 0;
		for ( int i = 0; i < kMaxVoices; ++i )
		{
			Voice& v = voice[i];
			memset( &v, 0, sizeof v );
			v.note = 48;
			v.pitch = v.from = 48.f;
		}
		for ( int l = 0; l < kLanes; ++l ) { lane[l] = 0; laneSet[l] = false; }
	}

	void releaseAll()
	{
		for ( int i = 0; i < kMaxVoices; ++i )
		{
			if ( voice[i].on ) voice[i].offAt = now;
			voice[i].on = false;
			voice[i].low = 0;
		}
	}

	void setVoices( int n )
	{
		if ( n < 1 ) n = 1;
		if ( n > kMaxVoices ) n = kMaxVoices;
		if ( n < voices )
			for ( int i = n; i < kMaxVoices; ++i ) { voice[i].on = false; voice[i].used = false; }
		voices = n;
	}

	uint32_t nextRand()
	{
		rng = rng * 1664525u + 1013904223u;
		return rng >> 16;
	}

	// Which voice a new note takes, or -1 to drop it.
	int pick( uint8_t note )
	{
		if ( voices <= 1 )
			return 0;
		int freeBest = -1, oldest = 0;
		for ( int i = 0; i < voices; ++i )
		{
			if ( !voice[i].on && ( freeBest < 0 || voice[i].offAt < voice[freeBest].offAt ) )
				freeBest = i;
			if ( voice[i].onAt < voice[oldest].onAt )
				oldest = i;
		}
		switch ( alloc )
		{
			case kAllocPoly:
				for ( int i = 0; i < voices; ++i )
					if ( voice[i].used && voice[i].note == note ) return i;
				return freeBest >= 0 ? freeBest : oldest;
			case kAllocFirst:
				for ( int i = 0; i < voices; ++i )
					if ( !voice[i].on ) return i;
				return -1;
			case kAllocCyclic:
				cyc = ( cyc + 1 ) % voices;
				return cyc;
			case kAllocRandom:
			{
				int freeList[kMaxVoices], nf = 0;
				for ( int i = 0; i < voices; ++i ) if ( !voice[i].on ) freeList[nf++] = i;
				if ( nf ) return freeList[nextRand() % (uint32_t)nf];
				return (int)( nextRand() % (uint32_t)voices );
			}
			default:
				return freeBest >= 0 ? freeBest : oldest;
		}
	}

	void event( const Ev& e )
	{
		if ( e.type == kEvMod )
		{
			if ( e.lane < kLanes ) { lane[e.lane] = e.value; laneSet[e.lane] = true; }
			return;
		}
		if ( e.type == kEvNoteOff )
		{
			for ( int i = 0; i < voices; ++i )
				if ( voice[i].on && voice[i].id == e.id )
				{
					voice[i].on = false;
					voice[i].offAt = now;
				}
			return;
		}
		int i = pick( e.note );
		if ( i < 0 )
			return;
		Voice& v = voice[i];
		if ( v.on && retrig && !( e.flags & kEvLegato ) )
		{
			v.low = retrigGap;		// exactly retrigGap samples low, this one included
			v.lowAt = now;
		}
		bool glide = e.glideType != kGlideNone && e.glideMs > 0 && v.used;
		v.from = v.pitch;
		v.note = e.note;
		v.vel = e.vel;
		v.id = e.id;
		v.on = true;
		v.used = true;
		v.onAt = now;
		lastVel = e.vel;
		if ( glide )
		{
			v.gType = e.glideType;
			v.gDur = (uint32_t)( (uint64_t)e.glideMs * sampleRate / 1000 );
			if ( v.gDur < 1 ) v.gDur = 1;
			v.gElapsed = 0;
		}
		else
		{
			v.gDur = 0;
			v.pitch = (float)e.note;
		}
	}

	// One sample: retrigger dips run down, glides move.
	void tick()
	{
		for ( int i = 0; i < voices; ++i )
		{
			Voice& v = voice[i];
			if ( v.low && v.lowAt != now ) v.low -= 1;
			if ( v.gDur )
			{
				v.gElapsed += 1;
				float t = (float)v.gElapsed / (float)v.gDur;
				if ( t >= 1.f ) { v.pitch = (float)v.note; v.gDur = 0; continue; }
				float target = (float)v.note;
				switch ( v.gType )
				{
					case kGlideExp:    v.pitch = target + ( v.from - target ) * expf( -5.f * t ); break;
					case kGlideSmooth: v.pitch = v.from + ( target - v.from ) * t * t * ( 3.f - 2.f * t ); break;
					default:           v.pitch = v.from + ( target - v.from ) * t; break;
				}
			}
		}
		now += 1;
	}

	bool gate( int i ) const { return voice[i].on && voice[i].low == 0; }
	// a voice's pitch as 1 V/oct volts, 0 V = C3
	float volts( int i ) const { return ( voice[i].pitch - 48.f ) * ( 1.f / 12.f ); }
	bool anyOn() const { for ( int i = 0; i < voices; ++i ) if ( voice[i].on ) return true; return false; }
};

}
