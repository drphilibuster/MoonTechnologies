// Apportionment -- the Ensoniq DP/4's host CPU, a Motorola MC68B03.
//
// The opcode bodies, flag tables and the 6803 opcode/cycle tables are MAME's
// (vendor/mame/m6800, BSD-3-Clause, Aaron Giles et al.), compiled unmodified by
// M6803.cpp. The class shell and the on-chip peripherals -- ports 1 and 2, the
// free-running timer with output compare and input capture, the SCI and the
// RAM control register -- are written here, because MAME's m6801.cpp is bound
// to its scheduler.
//
// The class keeps MAME's name, m6800_cpu_device, so the op bodies compile as
// they are; the namespace keeps it, and the u8/u16 aliases MAME's code wants,
// out of the plugin's global namespace.
#pragma once
#include <cstdint>
#include <deque>

namespace dp4 {

using u8 = uint8_t;   using s8 = int8_t;
using u16 = uint16_t; using s16 = int16_t;
using u32 = uint32_t; using s32 = int32_t;

union PAIR {
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	u32 d;
};

/** What the CPU sees outside itself. */
struct CpuBus {
	virtual ~CpuBus() = default;
	virtual u8 read(u16 addr) = 0;
	virtual void write(u16 addr, u8 value) = 0;
	virtual void sciTx(u8) {}
};

class m6800_cpu_device {
public:
	enum { M6800_WAI = 8, M6800_SLP = 0x10 };

	CpuBus* bus = nullptr;

	void reset();
	/** Run at least `cycles` E-clocks; overshoot carries into the next call. */
	void run(int cycles);
	void setIrq1(bool asserted) { m_irq1 = asserted; }
	void sciRx(u8 byte) { m_rxq.push_back(byte); }
	/** An edge on P20/TIN: the DP/4's data-entry knob. */
	void inputCapture() { icr = frc; tcsr |= 0x80; }
	u8 port_in[2] = { 0xff, 0xff };

	u16 pc() const { return m_pc.w.l; }
	uint64_t total_cycles = 0;
	u8 internal_ram[128] = {};
	u8 ddr[2] = {}, port_out[2] = {};
	u8 tcsr = 0;
	u16 frc = 0, ocr = 0xffff, icr = 0;
	u8 rmcr = 0, trcsr = 0x20, rdr = 0, tdr = 0, ramcr = 0x40;

	typedef void (m6800_cpu_device::*op_func)();

private:
	PAIR m_ppc{}, m_pc{}, m_s{}, m_x{}, m_d{}, m_ea{};
	u8 m_cc = 0;
	u8 m_wai_state = 0;
	bool m_irq1 = false;
	int m_icount = 0;
	std::deque<u8> m_rxq;
	int m_tx_busy = 0, m_rx_wait = 0;
	u8 m_latch09 = 0;
	bool m_tsr_read_ocf = false, m_tsr_read_tof = false, m_tsr_read_rx = false;

	static const u8 flags8i[256], flags8d[256];
	static const u8 cycles_6803[256];
	static const op_func m6803_insn[0x100];

	u8 mem_r(u16 a);
	void mem_w(u16 a, u8 v);
	u8 int_r(u8 reg);
	void int_w(u8 reg, u8 v);
	u32 RM16(u32 addr);
	void WM16(u32 addr, PAIR* p);
	void enter_interrupt(const char* msg, u16 vector);
	void check_irq_lines();
	void check_irq2();
	void take_trap() {}
	void increment_counter(int n);
	void eat_cycles() { if (m_icount > 0) increment_counter(m_icount); }
	void execute_one();
	void tick(int cycles);

#include "../../vendor/mame/m6800/m6803_ops_decl.inc"
};

using M6803 = m6800_cpu_device;

} // namespace dp4
