// Nordic Banking's machine: gearmulator's Nord Lead 2X (vendor/gearmulator/n2xLib, GPLv3) -- the
// MC68331 on Musashi and two DSP56362 on dsp56300 -- behind the plain interface of Nord2x.hpp.
// Compiled as C++17 against the vendored sources. See NordLead2Research/NOTES.md for how each fact
// about the panel was found.
#include "Nord2x.hpp"

#include "n2xLib/n2xhardware.h"
#include "n2xLib/n2xrom.h"
#include "dsp56kBase/logging.h"
#include "dsp56kEmu/audio.h"
#include "mc68k/logging.h"
#include "synthLib/midiTypes.h"

#include <algorithm>
#include <cstring>
#include <mutex>

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
	void digit(int d) {
		digitSlots[d]++;
		for (int b = 0; b < 8; b++) digitOn[d][b] += !(data >> b & 1);
	}
};

Nord2x::Nord2x() : impl(new Impl) {}
Nord2x::~Nord2x() = default;

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
	// The rack unit: no wheel, no bend (the firmware takes both over MIDI).
	impl->hw->setKnobPosition(static_cast<n2x::KnobType>(0x70), 0);
	impl->hw->setKnobPosition(static_cast<n2x::KnobType>(0x71), 0);
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
