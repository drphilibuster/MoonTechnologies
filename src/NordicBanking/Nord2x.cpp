// Nordic Banking's machine: gearmulator's Nord Lead 2X (vendor/gearmulator/n2xLib, GPLv3) -- the
// MC68331 on Musashi and two DSP56362 on dsp56300 -- behind the plain interface of Nord2x.hpp.
// Compiled as C++17 against the vendored sources. See NordLead2Research/NOTES.md for how each fact
// about the panel was found.
#include "Nord2x.hpp"

#include "n2xLib/n2xhardware.h"
#include "n2xLib/n2xrom.h"
#include "dsp56kBase/logging.h"
#include "dsp56kBase/threadtools.h"
#include "dsp56kEmu/audio.h"
#include "mc68k/logging.h"
#include "synthLib/midiTypes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace nb {

struct Nord2x::Impl {
	std::unique_ptr<n2x::Hardware> hw;

	// --- the panel multiplex, from the firmware's writes to CS6 (68331 thread) ---------------
	// Each refresh step puts a data byte on CS6+8, then selects a row (CS6+$C bits 0-5, active
	// low) or a digit (CS6+$C bit 6 / bit 7, or a rising CS6+$A bit 7). An LED's brightness is
	// the share of its own row's or digit's slots in which its bit is lit: rows light on 1,
	// digits on 0. Counting slots rather than time is exact here: the firmware gives every slot
	// the same period (one refresh call each).
	mutable std::mutex ledMutex;
	uint8_t data = 0, lastA = 0;
	long rowSlots[6] = {}, rowOn[6][8] = {};
	long digitSlots[3] = {}, digitOn[3][8] = {};

	void panelWrite(uint32_t offset, uint8_t v) {
		std::lock_guard<std::mutex> lock(ledMutex);
		if (offset == 0x8) { data = v; return; }
		if (offset == 0xC) {
			for (int r = 0; r < 6; r++)
				if (!(v >> r & 1)) { rowSlots[r]++; for (int b = 0; b < 8; b++) rowOn[r][b] += data >> b & 1; }
			if (v & 0x40) digit(0);
			if (v & 0x80) digit(1);
			return;
		}
		if (offset == 0xA) {
			// Bits 0-6 select the knob the ADC reads; bit 7 rising latches the third digit.
			if ((v & 0x80) && !(lastA & 0x80)) digit(2);
			lastA = v;
		}
	}
	// --- the stream (see Nord2x.hpp) ---------------------------------------------------------
	static constexpr uint32_t BLOCK = 128;           // frames the worker runs at a time
	static constexpr uint32_t RING = 32768;          // frames, a power of two
	std::vector<float> ring = std::vector<float>(4 * size_t(RING), 0.f);
	std::atomic<uint64_t> head{0}, tail{0};          // frames ever written / read
	std::atomic<bool> run{false}, primed{false};
	std::atomic<uint32_t> target{0};
	std::atomic<uint64_t> underruns{0}, starved{0};
	std::atomic<uint32_t> worstBlockUs{0}, minFill{0xFFFFFFFFu};
	std::thread worker;

	void digit(int d) {
		digitSlots[d]++;
		for (int b = 0; b < 8; b++) digitOn[d][b] += !(data >> b & 1);
	}
};

Nord2x::Nord2x() : impl(new Impl) {}
Nord2x::~Nord2x() { stopStream(); }

void Nord2x::startStream(uint32_t targetFrames, std::function<void(const std::string&)> log) {
	Impl& m = *impl;
	if (m.worker.joinable() || !m.hw) return;
	setStreamTarget(targetFrames);
	m.run = true;
	m.worker = std::thread([this, log]() {
		Impl& m = *impl;
		dsp56k::ThreadTools::setCurrentThreadName("NordicBanking");
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
		float blk[4][Impl::BLOCK];
		float* const bp[4] = { blk[0], blk[1], blk[2], blk[3] };
		auto lastLog = std::chrono::steady_clock::now();
		uint64_t loggedUnderruns = 0;
		while (m.run) {
			const uint64_t h = m.head.load(std::memory_order_relaxed), t = m.tail.load(std::memory_order_acquire);
			if (h - t + Impl::BLOCK <= m.target.load(std::memory_order_relaxed)) {
				const auto t0 = std::chrono::steady_clock::now();
				process(bp, int(Impl::BLOCK));
				const uint32_t us = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
				if (us > m.worstBlockUs.load(std::memory_order_relaxed)) m.worstBlockUs = us;
				for (int c = 0; c < 4; c++)
					for (uint32_t i = 0; i < Impl::BLOCK; i++) m.ring[size_t(c) * Impl::RING + size_t((h + i) & (Impl::RING - 1))] = blk[c][i];
				m.head.store(h + Impl::BLOCK, std::memory_order_release);
			} else {
				m.primed = true;
				std::this_thread::sleep_for(std::chrono::microseconds(250));
			}
			const auto now = std::chrono::steady_clock::now();
			if (log && now - lastLog > std::chrono::seconds(2)) {
				lastLog = now;
				const uint64_t u = m.underruns.load();
				if (u != loggedUnderruns) {
					const StreamStats s = streamStats();
					log("audio ran short " + std::to_string(u - loggedUnderruns) + " time(s) in 2 s (" + std::to_string(s.starvedFrames) +
						" frames in all); the buffer's low point was " + std::to_string(s.minFill) + " of " + std::to_string(s.target) +
						" frames, the worst block took " + std::to_string(s.worstBlockMs) + " ms");
					loggedUnderruns = u;
				}
			}
		}
	});
}

void Nord2x::stopStream() {
	Impl& m = *impl;
	m.run = false;
	if (m.worker.joinable()) m.worker.join();
}

void Nord2x::setStreamTarget(uint32_t targetFrames) {
	impl->target = std::min(std::max(targetFrames, Impl::BLOCK), Impl::RING - Impl::BLOCK);
}

size_t Nord2x::read(float* const out[4], size_t frames) {
	Impl& m = *impl;
	if (!m.primed.load(std::memory_order_relaxed)) return 0;
	const uint64_t t = m.tail.load(std::memory_order_relaxed), h = m.head.load(std::memory_order_acquire);
	const size_t got = std::min(frames, size_t(h - t));
	for (int c = 0; c < 4; c++)
		for (size_t i = 0; i < got; i++) out[c][i] = m.ring[size_t(c) * Impl::RING + size_t((t + i) & (Impl::RING - 1))];
	m.tail.store(t + got, std::memory_order_release);
	if (got < frames) { m.underruns.fetch_add(1, std::memory_order_relaxed); m.starved.fetch_add(frames - got, std::memory_order_relaxed); }
	const uint32_t fill = uint32_t(h - t - got);
	if (fill < m.minFill.load(std::memory_order_relaxed)) m.minFill.store(fill, std::memory_order_relaxed);
	return got;
}

Nord2x::StreamStats Nord2x::streamStats(bool reset) {
	Impl& m = *impl;
	StreamStats s;
	s.underruns = m.underruns;
	s.starvedFrames = m.starved;
	s.worstBlockMs = m.worstBlockUs / 1000.0;
	const uint32_t mf = m.minFill;
	s.fill = uint32_t(m.head.load() - m.tail.load());
	s.minFill = mf == 0xFFFFFFFFu ? s.fill : mf;
	s.target = m.target;
	if (reset) { m.underruns = 0; m.starved = 0; m.worstBlockUs = 0; m.minFill = 0xFFFFFFFFu; }
	return s;
}

std::string Nord2x::check(const std::vector<uint8_t>& rom) {
	if (rom.size() != ROM_SIZE) return "not a Nord Lead 2X OS image (expected 512 KB)";
	if (!n2x::Rom::isValidRom(rom)) return "not a Nord Lead 2X OS image (the NL2X signature is missing)";
	return {};
}

bool Nord2x::boot(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& flash) {
	if (!check(rom).empty()) return false;
	// Both emulators log freely; a plugin keeps quiet.
	Logging::setLogFunc([](const std::string&) {});
	mc68k::setLogSink([](const std::string&) {});
	const std::vector<uint8_t> f = flash.size() == FLASH_SIZE ? flash : std::vector<uint8_t>();
	impl->hw.reset(new n2x::Hardware(rom, "nord_lead_2x", f));
	Impl* m = impl.get();
	impl->hw->getUC().getFrontPanel().cs6().setWriteObserver([m](uint32_t o, uint8_t v) { m->panelWrite(o, v); });
	if (impl->hw->bootTimedOut()) { impl->hw.reset(); return false; }
	return impl->hw->isValid();
}

void Nord2x::process(float* const out[4], int frames) {
	if (!impl->hw) {
		for (int c = 0; c < 4; c++) std::fill(out[c], out[c] + frames, 0.f);
		return;
	}
	impl->hw->processAudio(uint32_t(frames), uint32_t(frames));
	const auto& outs = impl->hw->getAudioOutputs();
	for (int c = 0; c < 4; c++)
		for (int i = 0; i < frames; i++) out[c][i] = dsp56k::dsp2sample<float>(outs[size_t(c)][size_t(i)]);
}

void Nord2x::midi(const uint8_t* bytes, size_t length) {
	if (!impl->hw || !length) return;
	synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);
	if (bytes[0] == 0xF0) ev.sysex.assign(bytes, bytes + length);
	else { ev.a = bytes[0]; ev.b = length > 1 ? bytes[1] : 0; ev.c = length > 2 ? bytes[2] : 0; }
	impl->hw->sendMidi(ev);
}

void Nord2x::setKnob(uint8_t channel, uint8_t value) {
	if (impl->hw) impl->hw->setKnobPosition(static_cast<n2x::KnobType>(channel), value);
}

void Nord2x::setButton(uint16_t id, bool down) {
	if (impl->hw) impl->hw->setButtonState(static_cast<n2x::ButtonType>(id), down);
}

void Nord2x::ram(uint32_t offset, uint8_t* out, size_t length) const {
	if (!impl->hw) { std::fill(out, out + length, uint8_t(0)); return; }
	for (size_t i = 0; i < length; i++) out[i] = impl->hw->getUC().read8(0x100000u + offset + uint32_t(i));
}

void Nord2x::leds(float rows[6][8], float digits[3][8]) {
	Impl& m = *impl;
	std::lock_guard<std::mutex> lock(m.ledMutex);
	for (int r = 0; r < 6; r++) {
		for (int b = 0; b < 8; b++) rows[r][b] = m.rowSlots[r] ? float(m.rowOn[r][b]) / float(m.rowSlots[r]) : 0.f;
		m.rowSlots[r] = 0;
		std::memset(m.rowOn[r], 0, sizeof m.rowOn[r]);
	}
	for (int d = 0; d < 3; d++) {
		for (int b = 0; b < 8; b++) digits[d][b] = m.digitSlots[d] ? float(m.digitOn[d][b]) / float(m.digitSlots[d]) : 0.f;
		m.digitSlots[d] = 0;
		std::memset(m.digitOn[d], 0, sizeof m.digitOn[d]);
	}
}

void Nord2x::copyFlash(std::vector<uint8_t>& out) const {
	out.clear();
	if (!impl->hw) return;
	const auto& d = impl->hw->getUC().getFlash().getData();
	out.assign(d.begin(), d.end());
}

} // namespace nb
