#pragma once
// Rows, and the song that strings them together.
//
// A row is slot k of every track (Hermod+'s "sequence"). When a queued row takes over is
// a launch sync; the ones here are in master ticks (one tick, one beat):
//
//  - a track's loop lasts `len` steps at its rate num/den, which is len * den / num
//    ticks: as a whole number of ticks, loopTicks(), the first tick on which its loop
//    and the clock's grid line up again;
//  - MODULO is every playing track's loop ending together, the least common multiple of
//    their loopTicks (Hermod's MODULO); SHORTEST and LONGEST are the shortest and the
//    longest of them; TRACK n is track n's.
//
// The song is up to kSongMax entries, each a row and how many times it plays through.
// Each time the row's period comes round, the entry counts one; when it has played its
// times the next entry's row launches, on that same tick. The last entry goes back to
// the first.
//
// Pure C++11, no Rack types, no allocation.
#include <stdint.h>

namespace ledger {

enum { kSongMax = 32, kMaxRowTicks = 1 << 20 };

static inline uint64_t gcd64( uint64_t a, uint64_t b ) { while ( b ) { uint64_t t = a % b; a = b; b = t; } return a; }

// Least common multiple, held at `cap` (0 stays 0: "no constraint").
static inline uint64_t lcm64( uint64_t a, uint64_t b, uint64_t cap = kMaxRowTicks )
{
	if ( !a ) return b;
	if ( !b ) return a;
	uint64_t l = a / gcd64( a, b ) * b;
	return l > cap ? cap : l;
}

// How many master ticks before a loop of `len` steps at rate num/den lines up with the
// clock's grid again.
static inline uint64_t loopTicks( int len, uint32_t num, uint32_t den )
{
	if ( len < 1 ) len = 1;
	if ( !num ) num = 1;
	uint64_t a = (uint64_t)len * den;
	return a / gcd64( a, num );
}

enum RowSync { kRowModulo, kRowShortest, kRowLongest, kRowTrack };

// The period of a row, in ticks, from each track's loopTicks; `used` says which tracks
// are playing something. 0 when nothing is.
static inline uint64_t rowPeriod( int sync, const uint64_t* ticks, const bool* used, int n, int track )
{
	uint64_t p = 0;
	if ( sync == kRowTrack )
		return track >= 0 && track < n ? ticks[track] : 0;
	for ( int t = 0; t < n; ++t )
	{
		if ( !used[t] ) continue;
		if ( sync == kRowModulo ) p = lcm64( p, ticks[t] );
		else if ( sync == kRowShortest ) p = ( !p || ticks[t] < p ) ? ticks[t] : p;
		else p = ticks[t] > p ? ticks[t] : p;
	}
	return p;
}

struct SongEntry
{
	int8_t	row;		// 0..15
	uint8_t	times;		// 1..16
};

struct Song
{
	SongEntry	e[kSongMax];
	int			len;		// entries in use
	int			pos;		// the entry playing, -1 = not started
	int			left;		// passes left in it

	Song() { clear(); }
	void clear() { len = 0; pos = -1; left = 0; for ( int i = 0; i < kSongMax; ++i ) { e[i].row = 0; e[i].times = 1; } }
	void stop() { pos = -1; left = 0; }

	// Start from the first entry: the row to launch, or -1 for an empty song.
	int start()
	{
		if ( len <= 0 ) { stop(); return -1; }
		pos = 0;
		left = e[0].times < 1 ? 1 : e[0].times;
		return e[0].row;
	}

	// The playing row's period came round: the row to launch now, or -1 to stay.
	int boundary()
	{
		if ( len <= 0 ) { stop(); return -1; }
		if ( pos < 0 || pos >= len ) return start();
		if ( --left > 0 ) return -1;
		pos = ( pos + 1 ) % len;
		left = e[pos].times < 1 ? 1 : e[pos].times;
		return e[pos].row;
	}

	bool insert( int at, SongEntry s )
	{
		if ( len >= kSongMax ) return false;
		if ( at < 0 ) at = 0;
		if ( at > len ) at = len;
		for ( int i = len; i > at; --i ) e[i] = e[i - 1];
		e[at] = s;
		++len;
		if ( pos >= at ) ++pos;
		return true;
	}

	void remove( int at )
	{
		if ( at < 0 || at >= len ) return;
		for ( int i = at; i < len - 1; ++i ) e[i] = e[i + 1];
		--len;
		if ( pos > at ) --pos;
		else if ( pos == at ) { if ( pos >= len ) pos = len ? 0 : -1; left = pos >= 0 ? e[pos].times : 0; }
	}
};

}
