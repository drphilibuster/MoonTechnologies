// The Ledger Module itself (src/Ledger/LedgerModule.hpp), run sample by sample against
// libRack: what the panel's jacks put out, not just what the engine computes.
//
//  - A Ledger with nothing patched is the bare Shoal engine: gates and pitches equal a
//    standalone engine's, frame for frame.
//  - CAPTURE writes a generator's loop down, and the pattern it launches replays the
//    generator sample for sample.
//  - A launch quantized to the loop takes over on the loop's first step.
//  - A chord on a three-voice pattern track comes out as a three-channel cable.
//  - A patch saved and loaded keeps its slots.
//  - MIDI, through a fake Rack MIDI driver: a track's notes out on its channel, live input
//    played and recorded (overdub, replace, looper), CC learn into the matrix, the
//    transpose leader, clock and transport out, clock in, program change, and the
//    settings surviving a save.
//  - Rows and the song: the launch syncs land on the tick they name, a launched slot
//    starts at its first step, the song plays its rows their number of times in order,
//    a reset takes it back to the top, and undo puts slots, effects and the song back.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <set>
#include "../../src/Ledger/LedgerModule.hpp"
#include "../../src/Ledger/LedgerUndo.hpp"

namespace U = ledgerUndo;

Plugin* pluginInstance = NULL;

static int g_fail = 0, g_checks = 0;
#define CHECK( c, ... ) do { ++g_checks; if ( !( c ) ) { ++g_fail; std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); std::printf( __VA_ARGS__ ); std::printf( "\n" ); } } while ( 0 )

static const float SR = 48000.f;

// A Ledger with every output patched: Rack keeps an unpatched output at 0 channels.
static Ledger* newLedger()
{
	Ledger* m = new Ledger;
	for ( int o = 0; o < Ledger::NUM_OUTPUTS; ++o ) m->outputs[o].channels = 1;
	return m;
}

static void tick( Ledger* m, int64_t frame )
{
	Module::ProcessArgs a;
	a.sampleRate = SR;
	a.sampleTime = 1.f / SR;
	a.frame = frame;
	m->process( a );
}

// A bare engine in the module's engine's exact state, to run beside it.
static void twin( S::Shoal& s, Ledger* m )
{
	s.dtcStore = *m->eng.dtc;
	s.dtc = &s.dtcStore;
	std::memcpy( s.v, m->eng.v, sizeof s.v );
	std::memcpy( s.solo, m->eng.solo, sizeof s.solo );
	std::memcpy( s.external, m->eng.external, sizeof s.external );
	s.pulseVolts = m->eng.pulseVolts;
	s.host = NULL;
}

struct BareIo
{
	float pitch[8], gate[8], cur[8], eos[8], clk, zero = 0.f;
	S::Io io;
	BareIo()
	{
		std::memset( &io, 0, sizeof io );
		io.clockIn = &zero; io.resetIn = &zero; io.reseedIn = &zero; io.clkOut = &clk;
		for ( int t = 0; t < 8; ++t )
		{
			io.pitchOut[t] = &pitch[t]; io.gateOut[t] = &gate[t];
			io.currentOut[t] = &cur[t]; io.eosOut[t] = &eos[t];
		}
	}
};

static void testUnpatchedIsShoal()
{
	Ledger* m = newLedger();
	tick( m, 0 );								// the first frame moves the knobs to track 1's books
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	m->params[Ledger::POT_PARAM + L::kDChance].setValue( 70.f );
	tick( m, 1 );
	for ( int i = 0; i < 1000; ++i ) tick( m, 2 );	// run a little, so the copy below is mid-flight								// and now they land in them
	S::Shoal s;
	twin( s, m );
	BareIo b;
	int diffs = 0, gates = 0;
	for ( int i = 2; i < 200000; ++i )
	{
		tick( m, i );
		S::step( &s, b.io, 1 );
		for ( int t = 0; t < 8; ++t )
		{
			if ( m->outputs[Ledger::GATE_OUTPUT + t].getVoltage() != b.gate[t]
				 || m->outputs[Ledger::PITCH_OUTPUT + t].getVoltage() != b.pitch[t] )
				++diffs;
			gates += b.gate[t] > 0.f;
		}
	}
	CHECK( gates > 1000 && diffs == 0, "unpatched Ledger = Shoal: %d differing frames (%d gate-high frames)", diffs, gates );
	delete m;
}

static void testCaptureReplays()
{
	Ledger* m = newLedger();
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 120.f );
	m->params[Ledger::POT_PARAM + L::kDChance].setValue( 65.f );
	m->params[Ledger::POT_PARAM + L::kDNote].setValue( 40.f );
	m->params[Ledger::POT_PARAM + L::kDTie].setValue( 25.f );
	m->params[Ledger::POT_PARAM + L::kDGate].setValue( 50.f );
	m->params[Ledger::POT_PARAM + L::kDLength].setValue( 12.f );
	m->launchQ = kLaunchLoop;
	tick( m, f++ );
	// the twin: the same generator, left running
	S::Shoal s;
	twin( s, m );
	BareIo b;
	for ( int i = 0; i < 24000 * 5; ++i ) { tick( m, f++ ); S::step( &s, b.io, 1 ); }

	m->requestCapture = true;
	tick( m, f++ ); S::step( &s, b.io, 1 );
	CHECK( m->slots[0][1].kind == L::kSlotPat && m->queued[0] == 1, "capture wrote slot 2 and queued it" );
	CHECK( m->slots[0][1].block[L::kBlockChance] == 100, "the captured slot does not filter again" );
	int launchedAt = -1;
	int diffs = 0, compared = 0;
	for ( int i = 0; i < 24000 * 40; ++i )
	{
		tick( m, f++ );
		S::step( &s, b.io, 1 );
		if ( launchedAt < 0 && m->active[0] == 1 )
		{
			launchedAt = i;
			// it took over at the end of a loop: the generator's next step is step 1
			CHECK( s.dtc->tracks[0].eosCount == 0 && s.dtc->tracks[0].pos == 11,
				   "launched just after the loop's last step (eos count %u, step %d)", s.dtc->tracks[0].eosCount, s.dtc->tracks[0].pos + 1 );
		}
		if ( launchedAt >= 0 && i > launchedAt )
		{
			++compared;
			if ( m->outputs[Ledger::GATE_OUTPUT].getVoltage() != b.gate[0]
				 || ( b.gate[0] > 0.f && m->outputs[Ledger::PITCH_OUTPUT].getVoltage() != b.pitch[0] ) )
				++diffs;
		}
	}
	CHECK( launchedAt >= 0 && m->eng.external[0], "the captured pattern launched" );
	CHECK( compared > 100000 && diffs == 0, "the pattern replays the generator: %d of %d frames differ", diffs, compared );
	delete m;
}

static void testChordAndEdits()
{
	Ledger* m = newLedger();
	m->params[Ledger::BPM_PARAM].setValue( 120.f );
	m->launchQ = kLaunchNow;
	int64_t f = 0;
	tick( m, f++ );
	// a pattern with one three-note chord on step 1, sent the way the display sends it
	SlotEdit* e = new SlotEdit;
	e->op = SlotEdit::PUT_SLOT;
	e->track = 2;
	e->slot = 3;
	e->data.clear();
	e->data.kind = L::kSlotPat;
	L::defaultBlock( e->data.block, 2, 5 );
	e->data.block[L::kBlockLength] = 4;
	L::Note n = { 0, 12, 60, 120 };
	e->data.pat.add( n ); n.pitch = 64; e->data.pat.add( n ); n.pitch = 67; e->data.pat.add( n );
	CHECK( m->sendEdit( *e ), "the edit ring takes an edit" );
	uint32_t v0 = m->slotVersion[2][3].load();
	tick( m, f++ );
	CHECK( m->slots[2][3].kind == L::kSlotPat && m->slots[2][3].pat.count == 3, "the edit arrived" );
	CHECK( m->slotVersion[2][3].load() != v0, "and the slot's version moved" );
	m->voices[2].setVoices( 3 );
	m->queued[2] = 3;
	int maxCh = 0, allHigh = 0;
	float p[3] = {};
	for ( int i = 0; i < 24000 * 8; ++i )
	{
		tick( m, f++ );
		Output& g = m->outputs[Ledger::GATE_OUTPUT + 2];
		maxCh = std::max( maxCh, g.getChannels() );
		if ( g.getChannels() == 3 && g.getVoltage( 0 ) > 0.f && g.getVoltage( 1 ) > 0.f && g.getVoltage( 2 ) > 0.f )
		{
			++allHigh;
			for ( int c = 0; c < 3; ++c ) p[c] = m->outputs[Ledger::PITCH_OUTPUT + 2].getVoltage( c );
		}
	}
	CHECK( maxCh == 3, "a three-voice track is a three-channel cable (%d)", maxCh );
	CHECK( allHigh > 1000, "the chord sounds on all three voices (%d frames)", allHigh );
	CHECK( std::fabs( p[0] - 1.f ) < 1e-5f && std::fabs( p[1] - 16.f / 12.f ) < 1e-5f && std::fabs( p[2] - 19.f / 12.f ) < 1e-5f,
		   "C4 E4 G4 = 1, 1.33, 1.58 V (%g %g %g)", p[0], p[1], p[2] );
	CHECK( std::fabs( m->outputs[Ledger::VEL_OUTPUT].getVoltage( 2 ) - 120.f * 10.f / 127.f ) < 1e-4f, "VEL carries the chord's velocity (%g)",
		   m->outputs[Ledger::VEL_OUTPUT].getVoltage( 2 ) );

	// saved and loaded
	json_t* j = m->dataToJson();
	Ledger* m2 = newLedger();
	m2->dataFromJson( j );
	json_decref( j );
	CHECK( m2->active[2] == 3 && m2->slots[2][3].kind == L::kSlotPat && m2->slots[2][3].pat.count == 3
		   && m2->voices[2].voices == 3 && m2->eng.external[2], "slots, voices and the active slot survive a save" );
	CHECK( !memcmp( m2->slots[2][3].pat.notes, m->slots[2][3].pat.notes, sizeof( L::Note ) * 3 ), "note for note" );
	CHECK( m2->slots[0][0].kind == L::kSlotGen && !m2->eng.external[0], "the untouched tracks are still generators" );
	delete m2;
	delete m;
	delete e;
}

// Effects on a generator: a transparent one (MIDI output, with no device, passes all) routes the
// track through source -> chain -> voices, and that must still be Shoal, frame for frame.
static void testRoutedGeneratorIsShoal()
{
	Ledger* m = newLedger();
	tick( m, 0 );
	m->params[Ledger::BPM_PARAM].setValue( 300.f );
	m->params[Ledger::POT_PARAM + L::kDChance].setValue( 80.f );
	m->params[Ledger::POT_PARAM + L::kDTie].setValue( 30.f );
	m->params[Ledger::POT_PARAM + L::kDNote].setValue( 60.f );
	FxCmd c = {};
	c.op = FxCmd::SET_TYPE; c.track = 0; c.fx = 2; c.value = L::kFxMidiOut;
	CHECK( m->sendFx( c ), "the fx ring takes a command" );
	tick( m, 1 );
	tick( m, 2 );
	CHECK( m->chains[0].fx[2].type == L::kFxMidiOut && m->routed[0], "track 1 now runs through its chain" );
	S::Shoal s;
	twin( s, m );
	BareIo b;
	int diffs = 0, gates = 0;
	for ( int i = 3; i < 300000; ++i )
	{
		tick( m, i );
		S::step( &s, b.io, 1 );
		if ( m->outputs[Ledger::GATE_OUTPUT].getVoltage() != b.gate[0]
			 || ( b.gate[0] > 0.f && std::fabs( m->outputs[Ledger::PITCH_OUTPUT].getVoltage() - b.pitch[0] ) > 1e-6f ) )
			++diffs;
		gates += b.gate[0] > 0.f;
	}
	CHECK( gates > 10000 && diffs == 0, "a generator through a transparent effect is still Shoal: %d differing frames", diffs );
	delete m;
}

static void testEffectsInTheModule()
{
	Ledger* m = newLedger();
	m->launchQ = kLaunchNow;
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	// track 2: a one-note pattern, one voice-per-note, a harmonizer adding a third and a fifth
	SlotEdit* e = new SlotEdit;
	e->op = SlotEdit::PUT_SLOT; e->track = 1; e->slot = 2;
	e->data.clear();
	e->data.kind = L::kSlotPat;
	L::defaultBlock( e->data.block, 1, 9 );
	e->data.block[L::kBlockLength] = 1;
	L::Note n = { 0, 12, 60, 100 };
	e->data.pat.add( n );
	m->sendEdit( *e );
	FxCmd c = {};
	c.op = FxCmd::SET_TYPE; c.track = 1; c.fx = 0; c.value = L::kFxHarmonizer;
	m->sendFx( c );
	tick( m, f++ );
	m->chains[1].fx[0].p[1] = 4;
	m->chains[1].fx[0].p[2] = 7;
	m->voices[1].setVoices( 3 );
	m->queued[1] = 2;
	int chord = 0;
	std::set<int> heard;
	for ( int i = 0; i < 12000 * 4; ++i )
	{
		tick( m, f++ );
		Output& g = m->outputs[Ledger::GATE_OUTPUT + 1];
		int high = 0;
		for ( int ch = 0; ch < g.getChannels(); ++ch ) if ( g.getVoltage( ch ) > 0.f )
		{
			++high;
			// (the first step: the generator's last note may still be finishing)
			if ( i >= 12000 ) heard.insert( (int)std::lround( m->outputs[Ledger::PITCH_OUTPUT + 1].getVoltage( ch ) * 12.f ) );
		}
		chord += high == 3;
	}
	CHECK( chord > 1000 && heard == std::set<int>( { 12, 16, 19 } ), "one note in, a C major chord out (%d frames, %zu pitches)", chord, heard.size() );

	// the slot's own value beats the track's
	c = FxCmd();
	c.op = FxCmd::OVERRIDE_SET; c.track = 1; c.fx = 0; c.slot = 2; c.param = 1; c.value = 3;
	m->sendFx( c );
	c.op = FxCmd::SLOT_MUTE; c.fx = 5; c.value = 1; c.slot = 2;
	m->sendFx( c );
	for ( int i = 0; i < 200; ++i ) tick( m, f++ );
	CHECK( m->chains[1].fx[0].pe[1] == 3 && m->chains[1].fx[0].p[1] == 4, "slot 3 plays its own harmony (%d over %d)",
		   m->chains[1].fx[0].pe[1], m->chains[1].fx[0].p[1] );

	// muting the harmonizer leaves the played note alone
	m->chains[1].fx[0].gmute = true;
	int single = 0, chordAt = -1;
	for ( int i = 0; i < 12000 * 2; ++i )
	{
		tick( m, f++ );
		Output& g = m->outputs[Ledger::GATE_OUTPUT + 1];
		int high = 0;
		for ( int ch = 0; ch < g.getChannels(); ++ch ) high += g.getVoltage( ch ) > 0.f;
		single += high == 1;
		// the mute lands at the next modulation pass (every 4 samples)
		if ( i >= 8 && high > 1 && chordAt < 0 ) chordAt = i;
	}
	CHECK( chordAt < 0, "muted: never a chord (one at %d)", chordAt );
	CHECK( single > 1000, "muted: the note alone (%d frames)", single );
	m->chains[1].fx[0].gmute = false;

	// saved and loaded: effects, their values, the slot's own value and mute
	json_t* j = m->dataToJson();
	Ledger* m2 = newLedger();
	m2->dataFromJson( j );
	json_decref( j );
	const L::Slot& s2 = m2->slots[1][2];
	CHECK( m2->chains[1].fx[0].type == L::kFxHarmonizer && m2->chains[1].fx[0].p[2] == 7
		   && s2.findOverride( 0, 1 ) >= 0 && s2.ov[s2.findOverride( 0, 1 )].value == 3 && ( s2.fxMute & ( 1 << 5 ) ),
		   "effects, slot values and slot mutes survive a save" );
	delete m2;
	delete e;
	delete m;
}

// ---- MIDI ------------------------------------------------------------------

struct FakeOut : midi::OutputDevice
{
	std::vector<std::vector<uint8_t> > got;
	std::string name;
	std::string getName() override { return name; }
	void sendMessage( const midi::Message& m ) override { got.push_back( m.bytes ); }
};
struct FakeIn : midi::InputDevice
{
	std::string getName() override { return "fake in"; }
};
struct FakeDriver : midi::Driver
{
	FakeIn in;
	FakeOut out[2];
	FakeDriver() { out[0].name = "fake out A"; out[1].name = "fake out B"; }
	std::string getName() override { return "fake"; }
	std::vector<int> getInputDeviceIds() override { return { 0 }; }
	std::string getInputDeviceName( int ) override { return "fake in"; }
	midi::InputDevice* subscribeInput( int, midi::Input* i ) override { in.subscribe( i ); return &in; }
	void unsubscribeInput( int, midi::Input* i ) override { in.unsubscribe( i ); }
	std::vector<int> getOutputDeviceIds() override { return { 0, 1 }; }
	std::string getOutputDeviceName( int id ) override { return id ? "fake out B" : "fake out A"; }
	midi::OutputDevice* subscribeOutput( int id, midi::Output* o ) override { out[id].subscribe( o ); return &out[id]; }
	void unsubscribeOutput( int id, midi::Output* o ) override { out[id].unsubscribe( o ); }
};
static FakeDriver* g_drv = NULL;
enum { kFakeDriver = 77 };

static float gateV( Ledger* m, int t ) { return m->outputs[Ledger::GATE_OUTPUT + t].getVoltage(); }

static void plugMidi( Ledger* m )
{
	m->midiIn.setDriverId( kFakeDriver );
	m->midiIn.setDeviceId( 0 );
	for ( int p = 0; p < 2; ++p ) { m->midiOut[p].setDriverId( kFakeDriver ); m->midiOut[p].setDeviceId( p ); }
	g_drv->out[0].got.clear();
	g_drv->out[1].got.clear();
}

static void midiIn( int a, int b = 0, int c = 0, int n = 3 )
{
	midi::Message msg;
	msg.setSize( n );
	msg.bytes[0] = (uint8_t)a;
	if ( n > 1 ) msg.bytes[1] = (uint8_t)b;
	if ( n > 2 ) msg.bytes[2] = (uint8_t)c;
	msg.setFrame( 0 );
	g_drv->in.onMessage( msg );
}

// The track's notes out: a generator that skips its chain (track 2) and one routed
// through it (track 1, which takes the input), each on its own channel of Out A.
static void testMidiOut()
{
	Ledger* m = newLedger();
	plugMidi( m );
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	m->setBase( S::TP( 0, S::kTChance ), 70 );
	m->setBase( S::TP( 1, S::kTChance ), 70 );
	m->outPort[0] = 0; m->outCh[0] = 3;
	m->outPort[1] = 0; m->outCh[1] = 9;
	tick( m, f++ );
	CHECK( m->takes[0] && m->routed[0] && !m->routed[1], "track 1 takes the input (so runs its chain); track 2 does not" );
	g_drv->out[0].got.clear();
	int sounding0 = m->mports[0].sounding();
	int edges[2] = {}, matched[2] = {}, mis = 0;
	float lastGate[2] = { gateV( m, 0 ), gateV( m, 1 ) };
	for ( int i = 0; i < 12000 * 40; ++i )
	{
		size_t before = g_drv->out[0].got.size();
		tick( m, f++ );
		for ( int t = 0; t < 2; ++t )
		{
			float g = m->outputs[Ledger::GATE_OUTPUT + t].getVoltage();
			if ( g > 0.f && lastGate[t] <= 0.f )
			{
				++edges[t];
				int note = (int)std::lround( m->outputs[Ledger::PITCH_OUTPUT + t].getVoltage() * 12.f ) + 48;
				int st = 0x90 | ( t == 0 ? 2 : 8 );
				bool found = false;
				for ( size_t k = before; k < g_drv->out[0].got.size(); ++k )
					if ( g_drv->out[0].got[k][0] == st && g_drv->out[0].got[k][1] == note ) found = true;
				if ( found ) ++matched[t]; else ++mis;
			}
			lastGate[t] = g;
		}
	}
	CHECK( edges[0] > 20 && edges[1] > 20 && matched[0] == edges[0] && matched[1] == edges[1] && mis == 0,
		   "every gate is a note-on of the same pitch, the same sample, on the track's channel (%d/%d, %d/%d)",
		   matched[0], edges[0], matched[1], edges[1] );
	int ons = 0, offs = 0;
	for ( auto& b : g_drv->out[0].got ) { ons += ( b[0] & 0xF0 ) == 0x90; offs += ( b[0] & 0xF0 ) == 0x80; }
	CHECK( ons - offs == m->mports[0].sounding() - sounding0 && m->mports[0].sounding() <= 2, "note-offs follow (%d on, %d off)", ons, offs );

	// a change of channel ends what the track had sounding, on the old channel
	while ( m->mports[0].sounding() == 0 ) tick( m, f++ );
	int sounding = m->mports[0].sounding();
	g_drv->out[0].got.clear();
	m->outCh[0] = 4; m->outCh[1] = 10;
	tick( m, f++ );
	int ended = 0;
	for ( auto& b : g_drv->out[0].got ) ended += b[0] == 0x82 || b[0] == 0x88;
	CHECK( ended == sounding, "a new channel: the old one's notes are ended there (%d of %d)", ended, sounding );

	// a device change: the audio thread ends everything first
	while ( m->mports[0].sounding() == 0 ) tick( m, f++ );
	m->outPanic[0] = true;
	tick( m, f++ );
	CHECK( m->mports[0].sounding() == 0 && !m->outPanic[0], "a port about to change device has nothing left sounding" );
	delete m;
}

static void testLiveInput()
{
	Ledger* m = newLedger();
	plugMidi( m );
	int64_t f = 0;
	tick( m, f++ );
	for ( int t = 0; t < 8; ++t ) m->setBase( S::TP( t, S::kTChance ), 0 );		// the generators keep quiet
	for ( int i = 0; i < 30000; ++i ) tick( m, f++ );	// past the gates their first step opened
	midiIn( 0x90, 64, 100 );
	tick( m, f++ );
	CHECK( gateV( m, 0 ) > 0.f && std::fabs( m->outputs[Ledger::PITCH_OUTPUT].getVoltage() - 16.f / 12.f ) < 1e-5f,
		   "a key played: the selected track's gate, at its pitch" );
	midiIn( 0x80, 64, 0 );
	tick( m, f++ );
	CHECK( gateV( m, 0 ) == 0.f, "let go: the gate falls" );
	// track 4 has an effect, so it runs its chain whether or not it takes the input
	FxCmd c = {};
	c.op = FxCmd::SET_TYPE; c.track = 3; c.fx = 0; c.value = L::kFxMidiOut;
	m->sendFx( c );
	m->select( 3 );
	midiIn( 0x9F, 60, 100 );
	tick( m, f++ );
	CHECK( gateV( m, 3 ) > 0.f && gateV( m, 0 ) == 0.f, "the selected track plays it, whatever the channel" );
	m->select( 4 );
	tick( m, f++ );
	CHECK( m->routed[3] && gateV( m, 3 ) == 0.f && m->live[3].n == 0, "a track that stops taking the input ends what it held" );
	m->inMode = Ledger::IN_BY_CHANNEL;
	m->inCh[2] = 5;
	m->inCh[4] = Ledger::CH_OFF;			// track 5 would hear channel 5 too
	midiIn( 0x94, 62, 100 );
	midiIn( 0x90, 50, 100 );
	tick( m, f++ );
	CHECK( gateV( m, 2 ) > 0.f && gateV( m, 0 ) > 0.f && gateV( m, 4 ) == 0.f && gateV( m, 1 ) == 0.f,
		   "by channel: channel 5 to track 3, channel 1 to track 1" );
	midiIn( 0x84, 62, 0 );
	midiIn( 0x80, 50, 0 );
	tick( m, f++ );
	// program change: that row, every track
	midiIn( 0xC0, 3, 0, 2 );
	tick( m, f++ );
	bool row = true;
	for ( int t = 0; t < 8; ++t ) row = row && ( m->queued[t] == 3 || m->active[t] == 3 );
	CHECK( row, "program change 3 queues row 4" );
	delete m;
}

static void testRecording()
{
	Ledger* m = newLedger();
	plugMidi( m );
	m->launchQ = kLaunchNow;
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );		// a step is 12000 samples
	m->queued[0] = 1;									// an empty slot
	for ( int i = 0; i < 10; ++i ) tick( m, f++ );
	CHECK( m->active[0] == 1 && m->slots[0][1].kind == L::kSlotEmpty && m->eng.external[0], "track 1 is on an empty slot" );
	m->recOn = true;
	int st = m->patStep[0];
	while ( m->patStep[0] == st ) tick( m, f++ );		// the start of a step
	for ( int i = 0; i < 2999; ++i ) tick( m, f++ );		// the note lands 3000 samples in: on a tick
	int recStep = m->patStep[0];
	int64_t playedAt = f;
	midiIn( 0x90, 67, 90 );
	for ( int i = 0; i < 6000; ++i ) tick( m, f++ );
	midiIn( 0x80, 67, 0 );
	tick( m, f++ );
	const L::Slot& s = m->slots[0][1];
	CHECK( s.kind == L::kSlotPat && s.pat.count == 1 && s.pat.notes[0].start == recStep * 24 + 6 && s.pat.notes[0].len == 12
		   && s.pat.notes[0].pitch == 67 && s.pat.notes[0].vel == 90,
		   "overdub: the note a quarter into step %d, half a step long (start %d len %d)", recStep,
		   s.pat.count ? s.pat.notes[0].start : -1, s.pat.count ? s.pat.notes[0].len : -1 );
	m->recOn = false;
	int64_t replayAt = -1;
	float last = gateV( m, 0 );
	for ( int i = 0; i < 12000 * 17 && replayAt < 0; ++i )
	{
		tick( m, f++ );
		float g = gateV( m, 0 );
		if ( g > 0.f && last <= 0.f ) replayAt = f - 1;
		last = g;
	}
	CHECK( replayAt - playedAt == 16 * 12000 && std::fabs( m->outputs[Ledger::PITCH_OUTPUT].getVoltage() - 19.f / 12.f ) < 1e-5f,
		   "it plays back a loop (16 steps) later, to the sample (%lld)", (long long)( replayAt - playedAt ) );

	// replace: a pass with REC on and nothing played clears the pattern
	m->recMode = L::kRecReplace;
	m->recOn = true;
	for ( int i = 0; i < 12000 * 17; ++i ) tick( m, f++ );
	CHECK( m->slots[0][1].pat.count == 0, "replace: a silent pass leaves nothing (%d notes)", m->slots[0][1].pat.count );
	m->recOn = false;

	// a generator is not written into
	m->queued[0] = 0;
	m->recMode = L::kRecOverdub;
	m->recOn = true;
	for ( int i = 0; i < 10; ++i ) tick( m, f++ );
	midiIn( 0x90, 70, 100 );
	for ( int i = 0; i < 100; ++i ) tick( m, f++ );
	midiIn( 0x80, 70, 0 );
	tick( m, f++ );
	CHECK( m->slots[0][0].kind == L::kSlotGen, "recording leaves a generator slot a generator" );
	m->recOn = false;
	tick( m, f++ );

	// punch in: armed until a note
	m->punch = true;
	m->recOn = true;
	tick( m, f++ );
	CHECK( !m->recording(), "punch-in waits" );
	midiIn( 0x90, 70, 100 );
	tick( m, f++ );
	CHECK( m->recording(), "and starts on the first note" );
	midiIn( 0x80, 70, 0 );
	m->recOn = false;
	m->punch = false;
	tick( m, f++ );
	delete m;
}

static void testLooper()
{
	Ledger* m = newLedger();
	plugMidi( m );
	m->launchQ = kLaunchNow;
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	m->queued[0] = 2;
	for ( int i = 0; i < 10; ++i ) tick( m, f++ );
	m->recMode = L::kRecLooper;
	m->recOn = true;
	auto toStep = [&]() { int st = m->patPos[0]; uint32_t a = m->eng.dtc->tracks[0].advances; (void)st; while ( m->eng.dtc->tracks[0].advances == a ) tick( m, f++ ); };
	toStep();
	for ( int i = 0; i < 999; ++i ) tick( m, f++ );
	int64_t aAt = f;
	midiIn( 0x90, 60, 100 );
	for ( int i = 0; i < 2000; ++i ) tick( m, f++ );
	midiIn( 0x80, 60, 0 );
	toStep(); toStep(); toStep();
	for ( int i = 0; i < 1000; ++i ) tick( m, f++ );
	midiIn( 0x90, 72, 100 );
	for ( int i = 0; i < 2000; ++i ) tick( m, f++ );
	midiIn( 0x80, 72, 0 );
	toStep(); toStep();
	for ( int i = 0; i < 7000; ++i ) tick( m, f++ );	// step 6 of the loop, more than half through
	m->recOn = false;
	tick( m, f++ );
	const L::Slot& s = m->slots[0][2];
	CHECK( m->base[S::TP( 0, S::kTLength )] == 6, "the loop is six steps: stopped past the middle of the sixth (%d)",
		   m->base[S::TP( 0, S::kTLength )] );
	CHECK( s.kind == L::kSlotPat && s.pat.count == 2 && s.pat.notes[0].start == 2 && s.pat.notes[1].start == 3 * 24 + 2,
		   "its notes from the loop's first step (%d: %d, %d)", s.pat.count, s.pat.count ? s.pat.notes[0].start : -1,
		   s.pat.count > 1 ? s.pat.notes[1].start : -1 );
	std::vector<int64_t> rises;
	float last = gateV( m, 0 );
	for ( int i = 0; i < 12000 * 13; ++i )
	{
		tick( m, f++ );
		float g = gateV( m, 0 );
		if ( g > 0.f && last <= 0.f && std::fabs( m->outputs[Ledger::PITCH_OUTPUT].getVoltage() - 1.f ) < 1e-5f ) rises.push_back( f - 1 );
		last = g;
	}
	CHECK( rises.size() >= 2 && rises[0] - aAt == 6 * 12000 && rises[1] - aAt == 12 * 12000,
		   "the first note comes round every six steps from when it was played (%zu, %lld)", rises.size(),
		   rises.empty() ? -1LL : (long long)( rises[0] - aAt ) );
	delete m;
}

static void testCcLearnAndTranspose()
{
	Ledger* m = newLedger();
	plugMidi( m );
	int64_t f = 0;
	tick( m, f++ );
	m->matrix[0][0].dest = L::kDChance;
	m->matrix[0][0].amount = -100;
	m->learn = 0;
	midiIn( 0xB0, 20, 127 );
	for ( int i = 0; i < 8; ++i ) tick( m, f++ );
	CHECK( m->matrix[0][0].source == L::kSrcCC && m->matrix[0][0].cc == 20 && m->learn == -1, "learn takes the CC moved" );
	CHECK( m->eng.v[S::TP( 0, S::kTChance )] == 0 && m->base[S::TP( 0, S::kTChance )] == 100,
		   "CC 20 at the top, -100%%: chance 0 (knob still 100)" );
	midiIn( 0xB0, 20, 64 );
	for ( int i = 0; i < 8; ++i ) tick( m, f++ );
	CHECK( m->eng.v[S::TP( 0, S::kTChance )] == 100, "CC at the middle is no change (bipolar) (%d)", m->eng.v[S::TP( 0, S::kTChance )] );

	// the transpose leader on channel 16: a generator that skips its chain follows
	m->transCh = 16;
	m->params[Ledger::BPM_PARAM].setValue( 300.f );
	midiIn( 0x9F, 62, 100 );
	tick( m, f++ );
	CHECK( m->transSemis == 2 && m->routed[1] == false && m->live[0].n == 0, "D4 on the leader's channel: +2, nothing played" );
	for ( int i = 0; i < 1000; ++i ) tick( m, f++ );
	S::Shoal s;
	twin( s, m );
	BareIo b;
	int diffs = 0, notes = 0;
	for ( int i = 0; i < 100000; ++i )
	{
		tick( m, f++ );
		S::step( &s, b.io, 1 );
		if ( b.gate[1] > 0.f )
		{
			++notes;
			if ( std::fabs( m->outputs[Ledger::PITCH_OUTPUT + 1].getVoltage() - ( b.pitch[1] + 2.f / 12.f ) ) > 1e-5f ) ++diffs;
		}
	}
	CHECK( notes > 1000 && diffs == 0, "track 2 is Shoal two semitones up (%d of %d frames differ)", diffs, notes );
	m->followTrans[1] = false;
	tick( m, f++ );
	S::step( &s, b.io, 1 );
	CHECK( std::fabs( m->outputs[Ledger::PITCH_OUTPUT + 1].getVoltage() - b.pitch[1] ) < 1e-6f, "one that does not follow is not moved" );
	m->transMode = 1;
	midiIn( 0x9F, 65, 100 );
	tick( m, f++ );
	CHECK( m->base[S::kGRoot] == 48 + 5 && (int)m->params[Ledger::ROOT_PARAM].getValue() == 5, "root mode: F sets the books to F" );
	delete m;
}

static void testClocks()
{
	Ledger* m = newLedger();
	plugMidi( m );
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );		// a beat is 12000 samples
	m->clkOut[0] = L::kClkOutAll;
	m->clkOut[1] = L::kClkOutTransport;
	m->setBase( S::kGRun, 0 );
	for ( int i = 0; i < 100; ++i ) tick( m, f++ );
	g_drv->out[0].got.clear();
	g_drv->out[1].got.clear();
	m->setBase( S::kGRun, 1 );
	std::vector<int64_t> pulses;
	int64_t t0 = f;
	for ( int i = 0; i < 12000 * 8; ++i )
	{
		size_t before = g_drv->out[0].got.size();
		tick( m, f++ );
		for ( size_t k = before; k < g_drv->out[0].got.size(); ++k )
			if ( g_drv->out[0].got[k][0] == 0xF8 ) pulses.push_back( f - 1 - t0 );
	}
	CHECK( !g_drv->out[0].got.empty() && g_drv->out[0].got[0][0] == 0xFB, "resuming in place: Continue first" );
	// Shoal's clock resumes where it stopped: the first beat comes when it is due
	bool even = pulses.size() >= 24 * 6;
	for ( size_t k = 1; even && k < pulses.size(); ++k ) even = pulses[k] - pulses[k - 1] == 500;
	CHECK( even, "then a clock every 500 samples: 24 a beat (%zu in 8 beats)", pulses.size() );
	CHECK( g_drv->out[1].got.size() == 1 && g_drv->out[1].got[0][0] == 0xFB, "transport only: no clocks" );
	m->setBase( S::kGRun, 0 );
	tick( m, f++ );
	CHECK( g_drv->out[0].got.back()[0] == 0xFC && g_drv->out[1].got.back()[0] == 0xFC, "Stop when it stops" );
	// a reset while stopped: running again starts from the top
	m->eng.dtc->resetPrime = true;
	g_drv->out[0].got.clear();
	m->setBase( S::kGRun, 1 );
	tick( m, f++ );
	CHECK( !g_drv->out[0].got.empty() && g_drv->out[0].got[0][0] == 0xFA, "after a reset: Start" );
	// a reset landing while it runs: Start, and a clock on the same sample
	for ( int i = 0; i < 30000; ++i ) tick( m, f++ );
	g_drv->out[0].got.clear();
	m->eng.dtc->resetPrime = true;
	bool startThenClock = false;
	for ( int i = 0; i < 13000 && !startThenClock; ++i )
	{
		tick( m, f++ );
		auto& g = g_drv->out[0].got;
		for ( size_t k = 0; k + 1 < g.size(); ++k ) if ( g[k][0] == 0xFA && g[k + 1][0] == 0xF8 ) startThenClock = true;
	}
	CHECK( startThenClock, "a reset while running: Start, then the downbeat's clock" );
	delete m;

	// clock in: 24 a beat moves the master clock one tick
	m = newLedger();
	plugMidi( m );
	f = 0;
	tick( m, f++ );
	m->clockFromMidi = true;
	tick( m, f++ );
	CHECK( m->eng.v[S::kGClockSource] == 2, "MIDI is the clock" );
	midiIn( 0xFA, 0, 0, 1 );
	tick( m, f++ );
	// the first beat after Start is the reset's own tick
	for ( int k = 0; k < 24; ++k ) { midiIn( 0xF8, 0, 0, 1 ); for ( int i = 0; i < 400; ++i ) tick( m, f++ ); }
	uint32_t tc0 = m->eng.dtc->tickCount;
	for ( int beat = 0; beat < 6; ++beat )
		for ( int k = 0; k < 24; ++k )
		{
			midiIn( 0xF8, 0, 0, 1 );
			for ( int i = 0; i < 400; ++i ) tick( m, f++ );
		}
	CHECK( m->eng.dtc->tickCount - tc0 == 6 && m->eng.dtc->midiPeriod == 9600,
		   "six beats of MIDI clock: six ticks, 9600 samples apart (%u, %u)", m->eng.dtc->tickCount - tc0, m->eng.dtc->midiPeriod );
	midiIn( 0xFC, 0, 0, 1 );
	tick( m, f++ );
	uint32_t tc1 = m->eng.dtc->tickCount;
	for ( int k = 0; k < 48; ++k ) { midiIn( 0xF8, 0, 0, 1 ); for ( int i = 0; i < 400; ++i ) tick( m, f++ ); }
	CHECK( m->eng.dtc->tickCount == tc1, "after Stop, clocks move nothing" );
	delete m;
}

static void testMidiSaved()
{
	Ledger* m = newLedger();
	plugMidi( m );
	tick( m, 0 );
	m->inMode = Ledger::IN_BY_CHANNEL;
	m->inCh[5] = Ledger::CH_ANY;
	m->outPort[5] = 1; m->outCh[5] = 12;
	m->mmap[5].modCC[2] = 74;
	m->live[5].modCC = 11;
	m->followTrans[5] = false;
	m->clockFromMidi = true;
	m->pcLaunches = false;
	m->clkOut[1] = L::kClkOutClock;
	m->transCh = 9; m->transMode = 1;
	m->recMode = L::kRecLooper; m->punch = true;
	m->matrix[5][2].source = L::kSrcCC; m->matrix[5][2].cc = 33;
	json_t* j = m->dataToJson();
	// a channel saved on a port (by an older Ledger, or by hand) must not survive
	json_t* outs = json_object_get( json_object_get( j, "midi" ), "out" );
	json_object_set_new( json_array_get( outs, 1 ), "channel", json_integer( 5 ) );
	Ledger* m2 = newLedger();
	m2->dataFromJson( j );
	json_decref( j );
	CHECK( m2->inMode == Ledger::IN_BY_CHANNEL && m2->inCh[5] == Ledger::CH_ANY && m2->outPort[5] == 1 && m2->outCh[5] == 12
		   && m2->mmap[5].modCC[2] == 74 && m2->live[5].modCC == 11 && !m2->followTrans[5] && m2->clockFromMidi
		   && !m2->pcLaunches && m2->clkOut[1] == L::kClkOutClock && m2->transCh == 9 && m2->transMode == 1
		   && m2->recMode == L::kRecLooper && m2->punch && m2->matrix[5][2].source == L::kSrcCC && m2->matrix[5][2].cc == 33,
		   "MIDI settings survive a save" );
	CHECK( m2->midiOut[1].getDeviceId() == 1 && m2->midiIn.getDeviceId() == 0, "the devices are found again by name" );
	CHECK( m2->midiOut[0].channel == -1 && m2->midiOut[1].channel == -1, "the ports' channel stays -1: each message carries its own" );
	Ledger* m3 = newLedger();
	CHECK( m3->inCh[2] == 3 && m3->outPort[2] == -1 && m3->outCh[2] == 3 && m3->mmap[2].modCC[0] == 1 && m3->pcLaunches,
		   "a new Ledger: track n listens on channel n, sends nowhere" );
	delete m3;
	delete m2;
	delete m;
}

// ---- rows and the song -------------------------------------------------------

static void putSlot( Ledger* m, int t, int k, int kind, int len )
{
	SlotEdit* e = new SlotEdit;
	e->op = SlotEdit::PUT_SLOT;
	e->track = t;
	e->slot = k;
	e->data.clear();
	e->data.kind = (uint8_t)kind;
	L::defaultBlock( e->data.block, t, 7 + k );
	e->data.block[L::kBlockLength] = (int16_t)len;
	m->sendEditWait( *e );
	delete e;
}

static void testLaunchSyncs()
{
	Ledger* m = newLedger();
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	m->setBase( S::TP( 1, S::kTLength ), 12 );		// track 2 loops in 12; the rest in 16
	putSlot( m, 0, 1, L::kSlotGen, 16 );
	putSlot( m, 1, 1, L::kSlotGen, 12 );
	m->launchQ = kLaunchModulo;
	tick( m, f++ );
	CHECK( m->syncPeriod( kLaunchModulo ) == 48, "modulo of 16 and 12: 48 ticks (%llu)", (unsigned long long)m->syncPeriod( kLaunchModulo ) );
	m->queued[0] = 1;
	m->queued[1] = 1;
	uint32_t at = 0;
	for ( int i = 0; i < 12000 * 60 && !at; ++i )
	{
		tick( m, f++ );
		if ( m->active[0] == 1 ) at = m->eng.dtc->tickCount;
	}
	CHECK( at == 48 && m->active[1] == 1, "the row lands on tick 48, both tracks together (%u)", at );
	CHECK( m->anchorTick == 0, "every loop was at its end: nothing restarted" );

	// one track's loop: track 2's, 12 ticks
	m->launchQ = kLaunchTrack;
	m->launchTrack = 1;
	m->queued[0] = 0;
	at = 0;
	for ( int i = 0; i < 12000 * 30 && !at; ++i )
	{
		tick( m, f++ );
		if ( m->active[0] == 0 ) at = m->eng.dtc->tickCount;
	}
	CHECK( at == 60, "at the end of track 2's loop: tick 60 (%u)", at );
	delete m;
}

static void testRestart()
{
	Ledger* m = newLedger();
	m->launchQ = kLaunchNow;
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	putSlot( m, 0, 1, L::kSlotPat, 8 );
	tick( m, f++ );
	const S::TrackState& tr = m->eng.dtc->tracks[0];
	while ( tr.pos != 5 ) tick( m, f++ );
	m->queued[0] = 1;
	tick( m, f++ );
	uint32_t a = tr.advances;
	while ( tr.advances == a ) tick( m, f++ );
	CHECK( tr.pos == 0 && m->patStep[0] == 0, "launched mid-loop, the slot starts at its first step (pos %d)", tr.pos );
	m->launchRestart = false;
	while ( tr.pos != 3 ) tick( m, f++ );
	m->queued[0] = 0;
	tick( m, f++ );
	a = tr.advances;
	while ( tr.advances == a ) tick( m, f++ );
	CHECK( tr.pos == 4, "with restarting off, it goes on from where the track was (pos %d)", tr.pos );
	delete m;
}

static void testSongPlays()
{
	Ledger* m = newLedger();
	int64_t f = 0;
	tick( m, f++ );
	m->params[Ledger::BPM_PARAM].setValue( 240.f );
	putSlot( m, 0, 1, L::kSlotGen, 4 );		// row 2: track 1 alone, 4 steps
	putSlot( m, 0, 2, L::kSlotGen, 8 );		// row 3: track 1 alone, 8 steps
	Ledger::SongCmd* c = new Ledger::SongCmd;
	std::memset( c, 0, sizeof *c );
	c->op = Ledger::SongCmd::INSERT; c->at = 0; c->e.row = 1; c->e.times = 2;
	m->sendSong( *c );
	c->at = 1; c->e.row = 2; c->e.times = 1;
	m->sendSong( *c );
	delete c;
	tick( m, f++ );
	CHECK( m->song.len == 2, "the song has two entries" );
	m->songOn = true;
	std::vector<std::pair<uint32_t, int> > launches;
	std::vector<int> firstPos;
	int last = m->active[0];
	bool watch = false;
	uint32_t a = 0;
	for ( int i = 0; i < 12000 * 50; ++i )
	{
		tick( m, f++ );
		const S::TrackState& tr = m->eng.dtc->tracks[0];
		if ( watch && tr.advances != a ) { firstPos.push_back( tr.pos ); watch = false; }
		if ( m->active[0] != last )
		{
			last = m->active[0];
			launches.push_back( std::make_pair( m->eng.dtc->tickCount, last ) );
			watch = true;
			a = tr.advances;
		}
	}
	bool order = launches.size() >= 5;
	const uint32_t wantTick[5] = { 16, 24, 32, 40, 48 };
	const int wantRow[5] = { 1, 2, 1, 2, 1 };
	for ( size_t i = 0; order && i < 5; ++i ) order = launches[i].first == wantTick[i] && launches[i].second == wantRow[i];
	CHECK( order, "row 2 twice (4 ticks each), row 3 once (8), round again: launches at 16 24 32 40 48 (%zu: %u:%d %u:%d %u:%d)",
		   launches.size(), launches.size() > 0 ? launches[0].first : 0, launches.size() > 0 ? launches[0].second : -1,
		   launches.size() > 1 ? launches[1].first : 0, launches.size() > 1 ? launches[1].second : -1,
		   launches.size() > 2 ? launches[2].first : 0, launches.size() > 2 ? launches[2].second : -1 );
	bool fromTop = !firstPos.empty();
	for ( int p : firstPos ) fromTop = fromTop && p == 0;
	CHECK( fromTop, "each row starts at its first step (%zu launches)", firstPos.size() );
	CHECK( m->active[3] == 1 || m->active[3] == 2, "the other tracks follow the row (to its empty slots)" );

	// a reset: the song from the top, before the reset's tick plays
	m->eng.dtc->resetPrime = true;
	tick( m, f++ );
	CHECK( m->song.pos == 0 && m->active[0] == 1, "reset: the first entry's row, at once (pos %d, row %d)", m->song.pos, m->active[0] );
	m->songOn = false;
	tick( m, f++ );
	CHECK( m->song.pos == -1 && m->active[0] == 1, "stopping the song leaves the row playing" );

	// started while the clock is stopped: the first row is there at once
	m->setBase( S::kGRun, 0 );
	m->queued[0] = 0;
	tick( m, f++ );
	m->songOn = true;
	tick( m, f++ );
	CHECK( m->active[0] == 1 && m->song.pos == 0, "stopped: the song's first row at once" );

	// saved and loaded
	m->launchQ = kLaunchLongest; m->launchTrack = 5; m->launchRestart = false;
	json_t* j = m->dataToJson();
	Ledger* m2 = newLedger();
	m2->dataFromJson( j );
	json_decref( j );
	CHECK( m2->song.len == 2 && m2->song.e[0].row == 1 && m2->song.e[0].times == 2 && m2->song.e[1].row == 2
		   && m2->songOn && m2->launchQ == kLaunchLongest && m2->launchTrack == 5 && !m2->launchRestart,
		   "the song and the launch settings survive a save" );
	delete m2;
	delete m;
}

static void testUndo()
{
	Ledger* m = newLedger();
	APP->engine->addModule( m );
	int64_t f = 0;
	tick( m, f++ );

	// a pattern written into an empty slot, undone and redone
	U::SlotUndo* su = new U::SlotUndo( m, 2, 5, false, "edit pattern" );
	SlotEdit* e = new SlotEdit;
	e->op = SlotEdit::PUT_PATTERN; e->track = 2; e->slot = 5;
	e->data.clear();
	L::Note n = { 0, 12, 60, 100 };
	e->data.pat.add( n );
	m->sendEdit( *e );
	delete e;
	tick( m, f++ );
	CHECK( m->slots[2][5].kind == L::kSlotPat && m->slots[2][5].pat.count == 1, "the pattern is written" );
	su->undo();
	tick( m, f++ );
	CHECK( m->slots[2][5].kind == L::kSlotEmpty && m->slots[2][5].pat.count == 0, "undo: the slot is empty again" );
	su->redo();
	tick( m, f++ );
	CHECK( m->slots[2][5].kind == L::kSlotPat && m->slots[2][5].pat.count == 1, "redo: the note is back" );
	delete su;

	// the playing slot: undo puts its pattern back and leaves the knobs alone
	m->setBase( S::TP( 0, S::kTChance ), 40 );
	U::SlotUndo* sw = new U::SlotUndo( m, 0, 0, true, "clear slot" );
	putSlot( m, 0, 0, L::kSlotEmpty, 16 );
	tick( m, f++ );
	m->setBase( S::TP( 0, S::kTChance ), 40 );
	sw->undo();
	tick( m, f++ );
	CHECK( m->slots[0][0].kind == L::kSlotGen && m->base[S::TP( 0, S::kTChance )] == 40 && !m->eng.external[0],
		   "a cleared generator comes back, with the settings it was playing with" );
	delete sw;

	// effects: a type, its value and a slot's own value -- which choosing a type clears
	FxCmd c = {};
	c.op = FxCmd::SET_TYPE; c.track = 1; c.fx = 0; c.value = L::kFxHarmonizer;
	m->sendFx( c );
	tick( m, f++ );
	c = FxCmd(); c.op = FxCmd::OVERRIDE_SET; c.track = 1; c.fx = 0; c.slot = 0; c.param = 1; c.value = 3;
	m->sendFx( c );
	m->chains[1].fx[0].p[2] = 5;
	tick( m, f++ );
	U::FxUndo* fu = new U::FxUndo( m, 1, "choose effect" );
	c = FxCmd(); c.op = FxCmd::SET_TYPE; c.track = 1; c.fx = 0; c.value = L::kFxEcho;
	m->sendFx( c );
	tick( m, f++ );
	CHECK( m->chains[1].fx[0].type == L::kFxEcho && m->slots[1][0].nOverrides == 0, "echo replaces it, and the slot's value goes" );
	c = FxCmd(); c.op = FxCmd::OVERRIDE_SET; c.track = 1; c.fx = 0; c.slot = 0; c.param = 1; c.value = 9;
	m->sendFx( c );								// echo's own slot value, where the old one was
	tick( m, f++ );
	CHECK( fu->changed( m ), "the effects did change" );
	fu->undo();
	tick( m, f++ );
	CHECK( m->chains[1].fx[0].type == L::kFxHarmonizer && m->chains[1].fx[0].p[2] == 5
		   && m->slots[1][0].findOverride( 0, 1 ) >= 0 && m->slots[1][0].ov[m->slots[1][0].findOverride( 0, 1 )].value == 3,
		   "undo: the harmonizer, its value and the slot's own value are back" );
	fu->redo();
	tick( m, f++ );
	CHECK( m->chains[1].fx[0].type == L::kFxEcho, "redo: echo again" );
	delete fu;
	U::FxUndo* same = new U::FxUndo( m, 1, "set effect parameter" );
	CHECK( !same->changed( m ), "a gesture that changed nothing is not an undo step" );
	delete same;

	// the song
	U::SongUndo* gu = new U::SongUndo( m, "add to song" );
	Ledger::SongCmd* sc = new Ledger::SongCmd;
	std::memset( sc, 0, sizeof *sc );
	sc->op = Ledger::SongCmd::INSERT; sc->at = 0; sc->e.row = 4; sc->e.times = 3;
	m->sendSong( *sc );
	delete sc;
	tick( m, f++ );
	CHECK( m->song.len == 1, "an entry added" );
	gu->undo();
	tick( m, f++ );
	CHECK( m->song.len == 0, "undo: gone" );
	gu->redo();
	tick( m, f++ );
	CHECK( m->song.len == 1 && m->song.e[0].row == 4 && m->song.e[0].times == 3, "redo: back" );
	delete gu;

	APP->engine->removeModule( m );
	delete m;
}

int main()
{
	random::init();
	contextSet( new Context );
	APP->engine = new engine::Engine;
	APP->engine->setSampleRate( SR );

	testUnpatchedIsShoal();
	testCaptureReplays();
	testChordAndEdits();
	testRoutedGeneratorIsShoal();
	testEffectsInTheModule();
	g_drv = new FakeDriver;
	midi::addDriver( kFakeDriver, g_drv );
	testMidiOut();
	testLiveInput();
	testRecording();
	testLooper();
	testCcLearnAndTranspose();
	testClocks();
	testMidiSaved();
	testLaunchSyncs();
	testRestart();
	testSongPlays();
	testUndo();
	std::printf( "%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail );
	return g_fail ? 1 : 0;
}
