// Apportionment -- compiles MAME's ES5510 core (vendor/mame/es5510, BSD-3-Clause,
// Christian Brunschen) against the framework stand-in beside it, and wraps it in
// the dp4::Esp facade. See Esp.hpp. This is the only translation unit that sees
// MAME's headers; it must not include Rack.
//
// Standard headers first: MAME's private state is opened up for the facade
// with a #define that must not reach them.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../vendor/mame/es5510/emu.h"
#define private public
#define protected public
#include "../../vendor/mame/es5510/es5510.cpp"
#include "../../vendor/mame/es5510/es5510d.cpp"
#undef private
#undef protected

#include "Esp.hpp"

namespace dp4 {

static machine_config s_mconfig;
static address_space s_space;

struct Esp::Impl {
	es5510_device dev{s_mconfig, "esp", nullptr, 0};
};

Esp::Esp() : impl_(new Impl) {
	impl_->dev.device_start();
	impl_->dev.device_reset();
}

Esp::~Esp() = default;

void Esp::reset() {
	impl_->dev.device_reset();
	pending_ = false;
}

bool Esp::running() const { return impl_->dev.state == es5510_device::STATE_RUNNING; }
bool Esp::halted() const { return impl_->dev.state == es5510_device::STATE_HALTED; }
int Esp::pc() const { return impl_->dev.pc; }
int16_t Esp::serRead(int reg) const { return impl_->dev.ser_r(reg); }
void Esp::serWrite(int reg, int16_t v) { impl_->dev.ser_w(reg, v); }
void Esp::setHalt(bool a) { impl_->dev.set_HALT(a); }
int32_t Esp::gpr(int r) const { return impl_->dev.gpr[r % 0xc0]; }
int16_t Esp::dram(int a) const { return impl_->dev._dram(a); }

void Esp::completeTransfer() {
	impl_->dev.host_w(pendOffset_, pendData_);
	pending_ = false;
}

uint8_t Esp::hostRead(int o) {
	switch (o) {
	case 0x12: return uint8_t((impl_->dev.host_control & 0x03) | (pending_ ? 0x04 : 0x00));
	case 0x16: return running() ? impl_->dev.pc : 0;
	default:   return impl_->dev.host_r(s_space, o);
	}
}

void Esp::hostWrite(int o, uint8_t d) {
	const bool transfer = o == 0x80 || o == 0xa0 || o == 0xc0 || o == 0xe0;
	if (!transfer || !running()) {
		if (transfer && pending_) completeTransfer();
		impl_->dev.host_w(o, d);
		return;
	}
	if (pending_) {
		// The firmware strobes some write-selects twice in a row: same request.
		if (pendOffset_ == o && pendData_ == d) return;
		// A different request while busy "can upset" the transfer (spec 5.1.2).
		// Complete the older one rather than lose it; the firmware never does this.
		completeTransfer();
		collisions_++;
	}
	pending_ = true;
	pendOffset_ = uint8_t(o);
	pendData_ = d;
	deferred_++;
}

void Esp::run(int steps) {
	auto& d = impl_->dev;
	if (!pending_) { d.icount = steps; d.execute_run(); return; }
	// A transfer is waiting: step singly so it lands exactly on the END step.
	for (int n = 0; n < steps; n++) {
		const bool atEnd = running() && ((d.instr[d.pc % 160] >> 12) & 0x0f) == 0x0f;
		d.icount = 1;
		d.execute_run();
		if (pending_ && (atEnd || !running())) completeTransfer();
		if (!pending_) {
			if (n + 1 < steps) { d.icount = steps - n - 1; d.execute_run(); }
			return;
		}
	}
}

} // namespace dp4
