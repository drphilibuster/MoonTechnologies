// Apportionment -- MC6803 shell around MAME's 6800-family opcode bodies. See M6803.hpp.
// The op bodies and tables are MAME's (BSD-3-Clause), included unmodified from
// vendor/mame/m6800; everything else in this file is Moon Technologies (GPL-3.0-or-later).
#include "M6803.hpp"

#include <cstdarg>
#include <cstdio>

namespace dp4 {

#include "../../vendor/mame/m6800/m6803_macros.inc"

// Route MAME's memory macros through the 6803's own decode (internal regs/RAM, then board).
#undef RM
#undef WM
#undef M_RDOP
#undef M_RDOP_ARG
#define RM(Addr) mem_r(u16(Addr))
#define WM(Addr, Value) mem_w(u16(Addr), u8(Value))
#define M_RDOP(Addr) mem_r(u16(Addr))
#define M_RDOP_ARG(Addr) mem_r(u16(Addr))

// MAME's op bodies report illegal opcodes through logerror. The DP/4 firmware
// never executes one; stay quiet on the audio thread if it ever did.
static void logerror(const char*, ...) {}

#include "../../vendor/mame/m6800/6800ops.hxx"

#define XX 4
#include "../../vendor/mame/m6800/m6803_tables.inc"
#undef XX

// TCSR / TRCSR bits
enum { OLVL = 0x01, IEDG = 0x02, ETOI = 0x04, EOCI = 0x08, EICI = 0x10, TOF = 0x20, OCF = 0x40, ICF = 0x80 };
enum { WU = 0x01, TE = 0x02, TIE = 0x04, RE = 0x08, RIE = 0x10, TDRE = 0x20, ORFE = 0x40, RDRF = 0x80 };

static constexpr int SCI_FRAME_CYCLES = 640;   // 10 bits at 31250 baud with a 2 MHz E clock

void m6800_cpu_device::reset()
{
	m_cc = 0xc0;
	SEI;
	m_wai_state = 0;
	ddr[0] = ddr[1] = 0;
	port_out[0] = port_out[1] = 0;
	tcsr = 0;
	frc = 0; ocr = 0xffff; icr = 0;
	rmcr = 0; trcsr = TDRE; rdr = tdr = 0;
	ramcr = 0x40;
	m_tx_busy = m_rx_wait = 0;
	PCD = RM16(0xfffe);
}

u32 m6800_cpu_device::RM16(u32 addr)
{
	u32 r = RM(addr) << 8;
	return r | RM((addr + 1) & 0xffff);
}

void m6800_cpu_device::WM16(u32 addr, PAIR *p)
{
	WM(addr, p->b.h);
	WM((addr + 1) & 0xffff, p->b.l);
}

u8 m6800_cpu_device::mem_r(u16 a)
{
	if (a < 0x20) return int_r(u8(a));
	if (a >= 0x80 && a < 0x100 && (ramcr & 0x40)) return internal_ram[a - 0x80];
	return bus->read(a);
}

void m6800_cpu_device::mem_w(u16 a, u8 v)
{
	if (a < 0x20) { int_w(u8(a), v); return; }
	if (a >= 0x80 && a < 0x100 && (ramcr & 0x40)) { internal_ram[a - 0x80] = v; return; }
	bus->write(a, v);
}

u8 m6800_cpu_device::int_r(u8 reg)
{
	switch (reg) {
	case 0x00: return ddr[0];
	case 0x01: return ddr[1];
	case 0x02: return (port_out[0] & ddr[0]) | (port_in[0] & ~ddr[0]);
	case 0x03: return (port_out[1] & ddr[1]) | (port_in[1] & ~ddr[1]);
	case 0x08:
		m_tsr_read_ocf = tcsr & OCF;
		m_tsr_read_tof = tcsr & TOF;
		return tcsr;
	case 0x09:
		if (m_tsr_read_tof) { tcsr &= ~TOF; m_tsr_read_tof = false; }
		m_latch09 = u8(frc);
		return u8(frc >> 8);
	case 0x0a: return m_latch09;
	case 0x0b: return u8(ocr >> 8);
	case 0x0c: return u8(ocr);
	case 0x0d: tcsr &= ~ICF; return u8(icr >> 8);
	case 0x0e: return u8(icr);
	case 0x10: return rmcr;
	case 0x11:
		m_tsr_read_rx = trcsr & (RDRF | ORFE);
		return trcsr;
	case 0x12:
		if (m_tsr_read_rx) { trcsr &= ~(RDRF | ORFE); m_tsr_read_rx = false; }
		return rdr;
	case 0x13: return tdr;
	case 0x14: return ramcr;
	default: return 0xff;
	}
}

void m6800_cpu_device::int_w(u8 reg, u8 v)
{
	switch (reg) {
	case 0x00: ddr[0] = v; break;
	case 0x01: ddr[1] = v; break;
	case 0x02: port_out[0] = v; break;
	case 0x03: port_out[1] = v; break;
	case 0x08: tcsr = (tcsr & 0xe0) | (v & 0x1f); break;
	case 0x09: frc = 0xfff8; break;                      // MC6801: write to counter presets $FFF8
	case 0x0b: ocr = u16((v << 8) | (ocr & 0xff)); if (m_tsr_read_ocf) { tcsr &= ~OCF; m_tsr_read_ocf = false; } break;
	case 0x0c: ocr = u16((ocr & 0xff00) | v);        if (m_tsr_read_ocf) { tcsr &= ~OCF; m_tsr_read_ocf = false; } break;
	case 0x10: rmcr = v & 0x0f; break;
	case 0x11: trcsr = (trcsr & 0xe0) | (v & 0x1f); break;
	case 0x13:
		tdr = v;
		trcsr &= ~TDRE;
		m_tx_busy = SCI_FRAME_CYCLES;
		bus->sciTx(v);
		break;
	case 0x14: ramcr = v & 0xc0; break;
	default: break;
	}
}

void m6800_cpu_device::tick(int n)
{
	total_cycles += n;
	const u32 old = frc;
	if (u16(ocr - old - 1) < u32(n)) tcsr |= OCF;
	if (old + n > 0xffff) tcsr |= TOF;
	frc = u16(old + n);

	if (m_tx_busy > 0 && (m_tx_busy -= n) <= 0) { m_tx_busy = 0; trcsr |= TDRE; }
	if (m_rx_wait > 0) m_rx_wait -= n;
	if (m_rx_wait <= 0 && !m_rxq.empty() && (trcsr & RE)) {
		if (trcsr & RDRF) trcsr |= ORFE;
		rdr = m_rxq.front(); m_rxq.pop_front();
		trcsr |= RDRF;
		m_rx_wait = SCI_FRAME_CYCLES;
	}
}

void m6800_cpu_device::increment_counter(int n)
{
	m_icount -= n;
	tick(n);
}

void m6800_cpu_device::enter_interrupt(const char *, u16 vector)
{
	int cycles = 0;
	if (m_wai_state & M6800_WAI) {
		cycles = 4;
		m_wai_state &= ~M6800_WAI;
	} else {
		PUSHWORD(pPC);
		PUSHWORD(pX);
		PUSHBYTE(A);
		PUSHBYTE(B);
		PUSHBYTE(CC);
		cycles = 12;
	}
	SEI;
	PCD = RM16(vector);
	increment_counter(cycles);
}

void m6800_cpu_device::check_irq_lines()
{
	if (m_irq1) {
		if (!(CC & 0x10)) enter_interrupt("IRQ1", 0xfff8);
	} else {
		check_irq2();
	}
}

void m6800_cpu_device::check_irq2()
{
	if (CC & 0x10) return;
	if ((tcsr & ICF) && (tcsr & EICI)) enter_interrupt("ICF", 0xfff6);
	else if ((tcsr & OCF) && (tcsr & EOCI)) enter_interrupt("OCF", 0xfff4);
	else if ((tcsr & TOF) && (tcsr & ETOI)) enter_interrupt("TOF", 0xfff2);
	else if (((trcsr & RIE) && (trcsr & (RDRF | ORFE))) || ((trcsr & TIE) && (trcsr & TDRE)))
		enter_interrupt("SCI", 0xfff0);
}

void m6800_cpu_device::execute_one()
{
	pPPC = pPC;
	u8 ireg = M_RDOP(PCD);
	PC++;
	(this->*m6803_insn[ireg])();
	increment_counter(cycles_6803[ireg]);
}

void m6800_cpu_device::run(int cycles)
{
	m_icount += cycles;
	while (m_icount > 0) {
		check_irq_lines();
		if (m_wai_state & M6800_WAI)
			increment_counter(m_icount < 8 ? m_icount : 8);
		else
			execute_one();
	}
}

} // namespace dp4
