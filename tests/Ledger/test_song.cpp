// Ledger's rows and song without Rack (Song.hpp): how long a loop is in master ticks,
// the row syncs built from those, and the song stepping through its entries.
#include <cstdio>
#include <cstring>
#include "../../src/Ledger/Song.hpp"
#include "../../src/Ledger/Shoal.hpp"

namespace L = ledger;
namespace S = ledger::shoal;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )

static uint64_t loop( int len, int rate ) { return L::loopTicks( len, S::rateMult[rate], S::rateDiv[rate] ); }

static int rateIndex( int num, int den )
{
	for ( int r = 0; r < S::kNumRates; ++r ) if ( S::rateMult[r] == num && S::rateDiv[r] == den ) return r;
	return -1;
}

static void testLoopTicks()
{
	int x1 = S::kRateX1, x2 = rateIndex( 2, 1 ), d4 = rateIndex( 1, 4 ), x3_2 = rateIndex( 3, 2 );
	CHECK( x2 >= 0 && d4 >= 0 && x3_2 >= 0, "the rates the test uses exist" );
	CHECK( loop( 16, x1 ) == 16 && loop( 12, x1 ) == 12, "at x1 a loop is its length in ticks" );
	CHECK( loop( 16, x2 ) == 8 && loop( 7, x2 ) == 7, "x2: 16 steps are 8 ticks; 7 steps are 3.5, so 7 before it lines up" );
	CHECK( loop( 4, d4 ) == 16, "/4: 4 steps are 16 ticks" );
	CHECK( loop( 6, x3_2 ) == 4 && loop( 5, x3_2 ) == 10, "x1.5: 6 steps 4 ticks; 5 steps 10/3, so 10" );
	CHECK( loop( 0, x1 ) == 1, "a length below 1 counts as 1" );
	// every rate: the loop really does line up after loopTicks, and not on any tick before
	bool ok = true;
	for ( int r = 0; r < S::kNumRates && ok; ++r )
		for ( int len = 1; len <= 64 && ok; ++len )
		{
			uint64_t T = loop( len, r );
			uint64_t num = S::rateMult[r], den = S::rateDiv[r];
			// T ticks are T * num / den steps: whole, and a whole number of loops
			ok = ( T * num ) % den == 0 && ( ( T * num ) / den ) % len == 0;
			for ( uint64_t u = 1; u < T && ok; ++u )
				if ( ( u * num ) % den == 0 && ( ( u * num ) / den ) % len == 0 ) ok = false;
			if ( !ok ) std::printf( "  rate %d len %d: %llu\n", r, len, (unsigned long long)T );
		}
	CHECK( ok, "for every rate and length, loopTicks is the first tick the loop lines up" );
}

static void testRowPeriod()
{
	uint64_t ticks[8] = { 16, 12, 8, 6, 64, 3, 5, 7 };
	bool used[8] = { true, true, true, true, false, false, false, false };
	CHECK( L::rowPeriod( L::kRowModulo, ticks, used, 8, 0 ) == 48, "modulo of 16, 12, 8, 6 is 48" );
	CHECK( L::rowPeriod( L::kRowShortest, ticks, used, 8, 0 ) == 6 && L::rowPeriod( L::kRowLongest, ticks, used, 8, 0 ) == 16,
		   "shortest 6, longest 16 (the unused 64 does not count)" );
	CHECK( L::rowPeriod( L::kRowTrack, ticks, used, 8, 1 ) == 12, "track 2's loop" );
	bool none[8] = {};
	CHECK( L::rowPeriod( L::kRowModulo, ticks, none, 8, 0 ) == 0, "nothing playing: no period" );
	bool all[8] = { true, true, true, true, true, true, true, true };
	uint64_t primes[8] = { 61, 59, 53, 47, 43, 41, 37, 31 };
	CHECK( L::rowPeriod( L::kRowModulo, primes, all, 8, 0 ) == L::kMaxRowTicks, "a modulo too long to wait for is held at its cap" );
	CHECK( L::lcm64( 0, 5 ) == 5 && L::lcm64( 4, 6 ) == 12, "lcm" );
}

static void testSong()
{
	L::Song s;
	CHECK( s.start() == -1 && s.pos == -1, "an empty song starts nothing" );
	L::SongEntry a = { 2, 2 }, b = { 5, 1 }, c = { 0, 3 };
	s.insert( 0, a ); s.insert( 1, b ); s.insert( 2, c );
	int seq[12];
	seq[0] = s.boundary();		// not started: the first boundary starts it
	for ( int i = 1; i < 12; ++i ) seq[i] = s.boundary();
	int want[12] = { 2, -1, 5, 0, -1, -1, 2, -1, 5, 0, -1, -1 };
	bool ok = !std::memcmp( seq, want, sizeof seq );
	CHECK( ok, "row 3 twice, row 6 once, row 1 three times, round again (%d %d %d %d %d %d %d)",
		   seq[0], seq[1], seq[2], seq[3], seq[4], seq[5], seq[6] );
	// editing while it plays: the entry playing stays the one playing
	L::Song t;
	t.insert( 0, a ); t.insert( 1, b );
	t.start();
	t.boundary(); t.boundary();				// now on entry 1 (row 6)
	CHECK( t.pos == 1 && t.e[t.pos].row == 5, "on the second entry" );
	L::SongEntry d = { 9, 1 };
	t.insert( 0, d );
	CHECK( t.pos == 2 && t.e[t.pos].row == 5, "an entry put in before it moves it along" );
	t.remove( 0 );
	CHECK( t.pos == 1 && t.e[t.pos].row == 5, "and taken out moves it back" );
	t.remove( 1 );
	CHECK( t.pos == 0 && t.len == 1, "removing the playing entry goes on with what is there" );
	t.remove( 0 );
	CHECK( t.pos == -1 && t.len == 0 && t.boundary() == -1, "an emptied song stops" );
	L::Song u;
	for ( int i = 0; i < L::kSongMax; ++i ) u.insert( i, a );
	CHECK( !u.insert( 0, b ) && u.len == L::kSongMax, "a full song takes no more" );
}

int main()
{
	testLoopTicks();
	testRowPeriod();
	testSong();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
