// CvMidi: cables to MIDI bytes, with synthetic voltages and no Rack.
#include "../../src/Contagion/CvMidi.hpp"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Out {
	std::vector<std::vector<int>> msgs;
	void operator()(int s, int a, int b, int n) { msgs.push_back(n == 1 ? std::vector<int>{ s } : n == 2 ? std::vector<int>{ s, a } : std::vector<int>{ s, a, b }); }
	int count(int status) const { int k = 0; for (auto& m : msgs) k += m[0] == status; return k; }
};

static void step(vc::CvMidi& c, const vc::CvMidi::In& in, Out& o, int n = 1) { for (int i = 0; i < n; i++) c.process(in, o); }

int main() {
	{   // a gate is a note-on at the pitch, a note-off when it falls
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.voices = 1;
		in.pitch[0] = 0.f;      // C4
		in.gate[0] = 10.f;
		step(c, in, o, 4);
		CHECK(o.msgs.size() == 1 && o.msgs[0] == (std::vector<int>{ 0x90, 60, 100 }), "note on, got %zu msgs", o.msgs.size());
		in.gate[0] = 0.f;
		step(c, in, o, 2);
		CHECK(o.msgs.size() == 2 && o.msgs[1] == (std::vector<int>{ 0x80, 60, 0 }), "note off");
	}
	{   // the pitch is read a sample after the gate, so a sequencer that moves both together is heard right
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.voices = 1;
		in.pitch[0] = 0.f;
		step(c, in, o);
		in.gate[0] = 10.f;
		in.pitch[0] = 1.f;      // pitch lands with the gate
		step(c, in, o, 3);
		CHECK(o.msgs.size() == 1 && o.msgs[0][1] == 72, "pitch 1 V is note 72, got %d", o.msgs.empty() ? -1 : o.msgs[0][1]);
	}
	{   // velocity from the cable, and a legato change under a held gate
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.voices = 1;
		in.velConnected = true;
		in.vel[0] = 10.f;
		in.gate[0] = 5.f;
		step(c, in, o, 4);
		CHECK(o.msgs[0][2] == 127, "full velocity");
		in.pitch[0] = 2.f / 12.f;
		step(c, in, o, 2);
		CHECK(o.msgs.size() == 3 && o.msgs[1][0] == 0x90 && o.msgs[1][1] == 62 && o.msgs[2][0] == 0x80 && o.msgs[2][1] == 60,
			"legato: new note on, then the old off (%zu msgs)", o.msgs.size());
	}
	{   // eight voices, and cable channel to MIDI channel
		vc::CvMidi c;
		c.polyToChannels = true;
		vc::CvMidi::In in;
		Out o;
		in.voices = 3;
		for (int i = 0; i < 3; i++) { in.gate[i] = 10.f; in.pitch[i] = i / 12.f; }
		step(c, in, o, 3);
		CHECK(o.count(0x90) == 1 && o.count(0x91) == 1 && o.count(0x92) == 1, "one note on each of channels 0-2");
		in.voices = 1;      // two voices disappear: they are released on the channels they sounded on
		step(c, in, o);
		CHECK(o.count(0x81) == 1 && o.count(0x82) == 1 && o.count(0x80) == 0, "orphan voices released");
	}
	{   // unpatching the cable releases the notes
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.voices = 1;
		in.gate[0] = 10.f;
		step(c, in, o, 3);
		in.voices = 0;
		step(c, in, o);
		CHECK(o.count(0x80) == 1, "note released when the cable goes");
	}
	{   // wheels: sent when they move, reset when unpatched
		vc::CvMidi c;
		c.channel = 5;
		vc::CvMidi::In in;
		Out o;
		in.bendConnected = true;
		in.bend = 0.f;
		step(c, in, o, 3);
		CHECK(o.msgs.size() == 1 && o.msgs[0] == (std::vector<int>{ 0xE5, 0, 64 }), "bend centre, once");
		in.bend = 5.f;
		step(c, in, o);
		CHECK(o.msgs.back()[0] == 0xE5 && o.msgs.back()[2] == 127, "bend up");
		in.bendConnected = false;
		step(c, in, o);
		CHECK(o.msgs.back() == (std::vector<int>{ 0xE5, 0, 64 }), "bend recentred when unpatched");
		in.modConnected = true;
		in.mod = 10.f;
		step(c, in, o);
		CHECK(o.msgs.back() == (std::vector<int>{ 0xB5, 1, 127 }), "mod wheel is CC 1");
		in.atConnected = true;
		in.at = 5.f;
		step(c, in, o);
		CHECK(o.msgs.back()[0] == 0xD5 && o.msgs.back().size() == 2, "aftertouch is two bytes");
		in.susConnected = true;
		in.sus = 10.f;
		step(c, in, o);
		CHECK(o.msgs.back() == (std::vector<int>{ 0xB5, 64, 127 }), "sustain is CC 64");
	}
	{   // clock: one pulse per 16th (4 ppqn) is six ticks, spread across the interval, run starts and stops
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.clkConnected = true;
		in.runConnected = true;
		in.run = 10.f;
		const int period = 600;
		for (int pulse = 0; pulse < 6; pulse++)
			for (int s = 0; s < period; s++) { in.clk = s < 10 ? 10.f : 0.f; step(c, in, o); }
		CHECK(o.count(0xFA) == 1, "start once, got %d", o.count(0xFA));
		// the first interval has no period yet; every later pulse yields all six of its ticks in time
		const int ticks = o.count(0xF8);
		CHECK(ticks >= 6 * 5 && ticks <= 6 * 6, "ticks %d", ticks);
		in.run = 0.f;
		step(c, in, o);
		CHECK(o.count(0xFC) == 1, "stop");
		// timing: ticks after the first beat are evenly spaced
		vc::CvMidi d;
		std::vector<int> at;
		int t = 0;
		struct Rec { std::vector<int>* v; int* t; void operator()(int s, int, int, int) { if (s == 0xF8) v->push_back(*t); } } rec{ &at, &t };
		for (int pulse = 0; pulse < 4; pulse++)
			for (int s = 0; s < period; s++, t++) { in.clk = s < 10 ? 10.f : 0.f; in.run = 10.f; d.process(in, rec); }
		bool even = at.size() > 12;
		for (size_t i = 8; i + 1 < at.size() && i < 14; i++) even = even && std::abs((at[i + 1] - at[i]) - period / 6) <= 1;
		CHECK(even, "ticks evenly spaced");
	}
	{   // no run cable: the first clock starts it; reset restarts
		vc::CvMidi c;
		vc::CvMidi::In in;
		Out o;
		in.clkConnected = true;
		in.rstConnected = true;
		for (int s = 0; s < 100; s++) { in.clk = s < 10 ? 10.f : 0.f; step(c, in, o); }
		CHECK(o.count(0xFA) == 1, "clock alone starts");
		in.rst = 10.f;
		step(c, in, o);
		CHECK(o.count(0xFA) == 2, "reset starts again");
	}
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("CvMidi: ok\n");
	return 0;
}
