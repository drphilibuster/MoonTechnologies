#pragma once
// The original Shoal (vendor/shoal/shoal.cpp), built in its own translation unit
// (upstream.cpp) so its global enums and entry points never meet Ledger's port.
// The disting NT host is played by upstream.cpp: parameter writes go straight
// into v[] followed by parameterChanged(), as NT_setParameterFromAudio does.
#include <stdint.h>
#include <vector>

namespace upstream {

enum { kBusClock = 1, kBusReset, kBusReseed, kBusClkOut,
	   kBusGate0 = 5, kBusPitch0 = 13, kBusCurrent0 = 21, kBusEos0 = 29, kNumBusses = 36 };

struct MidiMsg { uint32_t dest; uint8_t b0, b1, b2; uint32_t frame; };

struct Instance;
Instance* create();						// 48 kHz, every output routed to its bus (Replace)
void destroy( Instance* );
int numParameters();
int16_t get( Instance*, int p );
void set( Instance*, int p, int16_t value );	// write + parameterChanged
void setSolo( Instance*, int t, bool on );
void midiRealtime( Instance*, uint8_t byte );
// busFrames: kNumBusses x numFrames, bus b (1-based) at (b-1)*numFrames.
void step( Instance*, float* busFrames, int numFrames );
std::vector<MidiMsg>& midiLog( Instance* );
uint32_t sampleRate();

}
