// Hosts the original Shoal for the golden test: stub NT_* functions plus the
// plug-in itself, included whole and unmodified.
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "../../vendor/shoal/shoal.cpp"
#include "upstream.hpp"

extern const _NT_globals NT_globals = { 48000, 256, nullptr, 0, 0, 0 };
uint8_t NT_screen[128 * 64];

namespace upstream {

struct Instance
{
	_melodySeqAlgorithm* alg;
	void* sram;
	void* dtc;
	int16_t v[kNumParameters];
	std::vector<MidiMsg> midi;
	uint32_t frame;
};

static Instance* g_current = nullptr;		// the instance a stub call belongs to

}

// ---- the host side of the NT API, as much of it as shoal.cpp calls ----
int32_t NT_algorithmIndex( const _NT_algorithm* ) { return 0; }
uint32_t NT_parameterOffset( void ) { return 0; }
void NT_setParameterFromAudio( uint32_t, uint32_t p, int16_t value )
{
	upstream::Instance* in = upstream::g_current;
	in->v[p] = value;
	parameterChanged( in->alg, (int)p );
}
void NT_setParameterFromUi( uint32_t a, uint32_t p, int16_t value ) { NT_setParameterFromAudio( a, p, value ); }
void NT_sendMidi3ByteMessage( uint32_t dest, uint8_t b0, uint8_t b1, uint8_t b2 )
{
	upstream::Instance* in = upstream::g_current;
	upstream::MidiMsg m = { dest, b0, b1, b2, in->frame };
	in->midi.push_back( m );
}
void NT_drawText( int, int, const char*, int, _NT_textAlignment, _NT_textSize ) {}
void NT_drawShapeI( _NT_shape, int, int, int, int, int ) {}
int NT_intToString( char* buffer, int32_t value ) { return std::sprintf( buffer, "%d", (int)value ); }

namespace upstream {

uint32_t sampleRate() { return NT_globals.sampleRate; }
int numParameters() { return kNumParameters; }

Instance* create()
{
	Instance* in = new Instance();
	_NT_algorithmRequirements req;
	std::memset( &req, 0, sizeof req );
	calculateRequirements( req, nullptr );
	in->sram = std::calloc( 1, req.sram );
	in->dtc = std::calloc( 1, req.dtc );
	_NT_algorithmMemoryPtrs ptrs;
	std::memset( &ptrs, 0, sizeof ptrs );
	ptrs.sram = (uint8_t*)in->sram;
	ptrs.dtc = (uint8_t*)in->dtc;
	in->alg = (_melodySeqAlgorithm*)construct( ptrs, req, nullptr );
	for ( int p = 0; p < kNumParameters; ++p )
		in->v[p] = parameters[p].def;
	// every output on its own bus, Replace
	in->v[kGClockIn] = kBusClock;
	in->v[kGResetIn] = kBusReset;
	in->v[kGReseedIn] = kBusReseed;
	in->v[kGClockOut] = kBusClkOut;
	in->v[kGClockOutMode] = 1;
	for ( int t = 0; t < kNumTracks; ++t )
	{
		in->v[ RP( t, 0 ) ] = kBusGate0 + t;
		in->v[ RP( t, 1 ) ] = 1;
		in->v[ RP( t, 2 ) ] = kBusPitch0 + t;
		in->v[ RP( t, 3 ) ] = 1;
		in->v[ CP( t ) ] = kBusCurrent0 + t;
		in->v[ EP( t ) ] = kBusEos0 + t;
	}
	in->alg->v = in->v;
	in->alg->vIncludingCommon = in->v;
	in->frame = 0;
	g_current = in;
	for ( int p = 0; p < kNumParameters; ++p )
		parameterChanged( in->alg, p );
	return in;
}

void destroy( Instance* in )
{
	std::free( in->sram );
	std::free( in->dtc );
	delete in;
}

int16_t get( Instance* in, int p ) { return in->v[p]; }

void set( Instance* in, int p, int16_t value )
{
	g_current = in;
	in->v[p] = value;
	parameterChanged( in->alg, p );
}

void setSolo( Instance* in, int t, bool on ) { in->alg->solo[t] = on; }

void midiRealtime( Instance* in, uint8_t byte )
{
	g_current = in;
	::midiRealtime( in->alg, byte );
}

void step( Instance* in, float* busFrames, int numFrames )
{
	g_current = in;
	::step( in->alg, busFrames, numFrames / 4 );
	in->frame += (uint32_t)numFrames;
}

std::vector<MidiMsg>& midiLog( Instance* in ) { return in->midi; }

}
