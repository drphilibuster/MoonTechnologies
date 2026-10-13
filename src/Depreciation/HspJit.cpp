// HspJit.cpp: the HSP's decoded operation list as native code (see src/Pcm70HspJit.hpp for why).
//
// One function per decode, `void fn(State*)`: the eight registers that live across samples and the three temporaries are held in vector registers for the whole
// sample (loaded once, stored once), every operation is its one or two instructions, and the few that are not arithmetic -- the events carried over from the previous
// sample, and the AND-NOT of kind 6 -- spill the registers to the State, call out, and reload. Nothing here decides anything the interpreter does not: it is the same
// list, in the same order, with the same expressions (HALF is a multiply by 0.5, exact; SAT is max then min), and Hsp checks the result against the interpreter
// before it first runs the code.
//
// Two code generators, one per host architecture, share the walk over the operations in shape but not in code: AArch64 (Apple silicon, Linux ARM) and x86-64
// (System V and Win64). Anything else gets no JIT and the interpreter runs.
#define PCM70_HSP_JIT 1
#include "../Pcm70HspJit.hpp"

#include <cstring>
#include <cstddef>

#if defined(__aarch64__) || defined(_M_ARM64)
	#include "asmjit/a64.h"
	#define PCM70_JIT_A64 1
#elif defined(__x86_64__) || defined(_M_X64)
	#include "asmjit/x86.h"
	#define PCM70_JIT_X64 1
#endif

namespace pcm70 { namespace hspjit {

#if defined(PCM70_JIT_A64) || defined(PCM70_JIT_X64)

using namespace asmjit;

namespace {

JitRuntime& runtime() { static JitRuntime* rt = new JitRuntime(); return *rt; }   // never destroyed: a Hsp may outlive static destruction order

uint64_t bitsOf(double v) { uint64_t u; std::memcpy(&u, &v, 8); return u; }

const double kNegOne = -1.0, kSatMax = 0.99997, kHalf = 0.5;

#define OFS(member) ((int32_t)offsetof(State, member))
constexpr int32_t kRegs = 0;                       // State::r is the first member

}

#if defined(PCM70_JIT_A64)
// ------------------------------------------------------------------ AArch64 ---------------------------------------------------------------------------------
// x19 State, x20 delay RAM, x21 offsets, w22 position, x23 kind table, x24 register selects (all callee-saved, kept across the helper calls).
// d8 -1.0, d9 0.99997, d10 0.5 (callee-saved low halves, so they survive a call); registers 0..10 live in d16..d26; d30/d31 are scratch.
static bool generate(a64::Assembler& a, const Op* ops, size_t n, void (*carry)(State*, int), double (*logic)(double, double)) {
	using namespace a64;
	const Gp S = x19, MEM = x20, OFF = x21, ALK = x23, RAS = x24;
	const Gp PS = w22;
	auto V = [](int v) { return VecD(16 + v); };
	const VecD cNeg = d8, cSat = d9, cHalf = d10, t0 = d30, t1 = d31;
	enum { R0 = 0, O = 5, R3 = 3 };

	a.sub(sp, sp, 112);
	a.stp(x19, x20, ptr(sp, 0)); a.stp(x21, x22, ptr(sp, 16)); a.stp(x23, x24, ptr(sp, 32)); a.str(x30, ptr(sp, 48));
	a.stp(d8, d9, ptr(sp, 64)); a.str(d10, ptr(sp, 80));
	a.mov(S, x0);
	a.ldr(MEM, ptr(S, OFS(mem))); a.ldr(OFF, ptr(S, OFS(off))); a.ldr(RAS, ptr(S, OFS(ras))); a.ldr(ALK, ptr(S, OFS(aluk))); a.ldr(PS, ptr(S, OFS(ps)));
	a.mov(x9, Imm(bitsOf(kNegOne))); a.fmov(cNeg, x9);
	a.mov(x9, Imm(bitsOf(kSatMax))); a.fmov(cSat, x9);
	a.mov(x9, Imm(bitsOf(kHalf))); a.fmov(cHalf, x9);
	for (int i = 0; i < NREG; i++) a.ldr(V(i), ptr(S, kRegs + 8 * i));

	auto spill = [&]() { for (int i = 0; i < NREG; i++) a.str(V(i), ptr(S, kRegs + 8 * i)); };
	auto reload = [&]() { for (int i = 0; i < NREG; i++) a.ldr(V(i), ptr(S, kRegs + 8 * i)); };
	// d = 0.5 * logic(a, b), through memory so no register has to be in the right place; the registers are spilled before and reloaded after
	auto callLogic = [&](int d, int ra, int rb) {
		spill();
		a.ldr(d0, ptr(S, kRegs + 8 * ra)); a.ldr(d1, ptr(S, kRegs + 8 * rb));
		a.mov(x9, Imm((uint64_t)(uintptr_t)logic)); a.blr(x9);
		a.fmul(t1, d0, cHalf);
		reload();
		a.fmov(V(d), t1);
	};
	auto addrWord = [&](int w) {                       // x9 = (uint16_t)(position + offsets[w])
		a.ldrh(w9, ptr(OFF, 2 * w)); a.add(w9, w9, PS); a.uxth(w9, w9);
	};

	for (size_t i = 0; i < n; i++) {
		const Op& m = ops[i];
		switch (m.t) {
		case MOV: if (m.d != m.a) a.fmov(V(m.d), V(m.a)); break;
		case MOVR: {
			Label l0 = a.newLabel(), l1 = a.newLabel(), l2 = a.newLabel(), done = a.newLabel();
			a.ldrb(w9, ptr(RAS, m.w));
			a.cbz(w9, l0); a.cmp(w9, 1); a.b_eq(l1); a.cmp(w9, 2); a.b_eq(l2);
			a.fmov(V(m.d), V(R3)); a.b(done);
			a.bind(l0); a.fmov(V(m.d), V(R0)); a.b(done);
			a.bind(l1); a.fmov(V(m.d), V(1)); a.b(done);
			a.bind(l2); a.fmov(V(m.d), V(2));
			a.bind(done);
			break; }
		case SAT: a.fmax(t0, V(m.a), cNeg); a.fmin(V(m.d), t0, cSat); break;
		case HALF: a.fmul(V(m.d), V(m.a), cHalf); break;
		case ADD: a.fadd(V(m.d), V(m.a), V(m.b)); break;
		case SUB: a.fsub(V(m.d), V(m.a), V(m.b)); break;
		case NEG: a.fneg(V(m.d), V(m.b)); break;
		case ZERO: a.fmov(V(m.d), xzr); break;
		case ALU34: callLogic(m.d, m.a, m.b); break;
		case ALUV: {
			Label l4 = a.newLabel(), l1 = a.newLabel(), l3 = a.newLabel(), l5 = a.newLabel(), l6 = a.newLabel(), l0 = a.newLabel(), done = a.newLabel();
			a.ldrb(w9, ptr(ALK, m.k));
			a.cmp(w9, 2); a.b_ne(l4); a.fadd(V(m.d), V(m.a), V(m.b)); a.b(done);
			a.bind(l4); a.cmp(w9, 4); a.b_ne(l1); a.fsub(V(m.d), V(m.a), V(m.b)); a.b(done);
			a.bind(l1); a.cmp(w9, 1); a.b_ne(l3); if (m.d != m.a) a.fmov(V(m.d), V(m.a)); a.b(done);
			a.bind(l3); a.cmp(w9, 3); a.b_ne(l5); if (m.d != m.b) a.fmov(V(m.d), V(m.b)); a.b(done);
			a.bind(l5); a.cmp(w9, 5); a.b_ne(l6); a.fneg(V(m.d), V(m.b)); a.b(done);
			a.bind(l6); a.cmp(w9, 6); a.b_ne(l0); callLogic(m.d, m.a, m.b); a.b(done);
			a.bind(l0); a.fmov(V(m.d), xzr);
			a.bind(done);
			break; }
		case LD: addrWord(m.w); a.ldr(V(m.d), ptr(MEM, x9, lsl(3))); break;
		case ST: addrWord(m.w); a.str(V(O), ptr(MEM, x9, lsl(3))); break;
		case IN: a.ldr(V(m.d), ptr(S, OFS(curIn))); break;
		case DAC: {
			Label skip = a.newLabel();
			a.ldr(w9, ptr(S, OFS(ndac))); a.cmp(w9, 2); a.b_hs(skip);
			a.add(x10, S, OFS(out)); a.str(V(O), ptr(x10, x9, lsl(3)));
			a.bind(skip);
			a.add(w9, w9, 1); a.str(w9, ptr(S, OFS(ndac)));
			break; }
		case CARRYIN: {
			Label skip = a.newLabel();
			a.ldr(w9, ptr(S, OFS(ncarry))); a.cbz(w9, skip);
			spill();
			a.mov(x0, S); a.mov(w1, Imm(m.d)); a.mov(x9, Imm((uint64_t)(uintptr_t)carry)); a.blr(x9);
			reload();
			a.bind(skip);
			break; }
		default: return false;
		}
	}

	for (int i = 0; i < NREG; i++) a.str(V(i), ptr(S, kRegs + 8 * i));
	a.ldp(d8, d9, ptr(sp, 64)); a.ldr(d10, ptr(sp, 80));
	a.ldp(x19, x20, ptr(sp, 0)); a.ldp(x21, x22, ptr(sp, 16)); a.ldp(x23, x24, ptr(sp, 32)); a.ldr(x30, ptr(sp, 48));
	a.add(sp, sp, 112);
	a.ret(x30);
	return true;
}
#endif // PCM70_JIT_A64

#if defined(PCM70_JIT_X64)
// ------------------------------------------------------------------ x86-64 ----------------------------------------------------------------------------------
// rbx State, r12 delay RAM, r13 offsets, r14d position, r15 kind table, rbp register selects. Registers 0..10 live in xmm0..xmm10; xmm11 -1.0, xmm12 0.99997,
// xmm13 0.5, xmm14 the sign bit, xmm15 scratch. The constants are rebuilt after every call (System V keeps no xmm across one).
static bool generate(x86::Assembler& a, const Op* ops, size_t n, void (*carry)(State*, int), double (*logic)(double, double)) {
	using namespace x86;
#if defined(_WIN32)
	const bool win = true;
#else
	const bool win = false;
#endif
	auto X = [](int v) { return x86::xmm(v); };
	const Xmm cNeg = xmm11, cSat = xmm12, cHalf = xmm13, cSign = xmm14, tmp = xmm15;
	enum { O = 5 };
	const int frame = win ? 200 : 8;

	a.push(rbx); a.push(rbp); a.push(r12); a.push(r13); a.push(r14); a.push(r15);
	a.sub(rsp, frame);
	if (win) for (int j = 0; j < 10; j++) a.movups(x86::ptr(rsp, 32 + 16 * j), xmm(6 + j));
	a.mov(rbx, win ? rcx : rdi);
	a.mov(r12, x86::ptr(rbx, OFS(mem))); a.mov(r13, x86::ptr(rbx, OFS(off))); a.mov(rbp, x86::ptr(rbx, OFS(ras))); a.mov(r15, x86::ptr(rbx, OFS(aluk)));
	a.mov(r14d, x86::dword_ptr(rbx, OFS(ps)));

	auto consts = [&]() {
		a.mov(rax, Imm(bitsOf(kNegOne))); a.movq(cNeg, rax);
		a.mov(rax, Imm(bitsOf(kSatMax))); a.movq(cSat, rax);
		a.mov(rax, Imm(bitsOf(kHalf))); a.movq(cHalf, rax);
		a.mov(rax, Imm(0x8000000000000000ull)); a.movq(cSign, rax);
	};
	consts();
	for (int i = 0; i < NREG; i++) a.movsd(X(i), x86::ptr(rbx, kRegs + 8 * i));

	auto spill = [&]() { for (int i = 0; i < NREG; i++) a.movsd(x86::ptr(rbx, kRegs + 8 * i), X(i)); };
	auto reload = [&]() { for (int i = 0; i < NREG; i++) a.movsd(X(i), x86::ptr(rbx, kRegs + 8 * i)); };
	auto callLogic = [&](int d, int ra, int rb) {
		spill();
		a.movsd(xmm0, x86::ptr(rbx, kRegs + 8 * ra)); a.movsd(xmm1, x86::ptr(rbx, kRegs + 8 * rb));
		a.mov(rax, Imm((uint64_t)(uintptr_t)logic)); a.call(rax);
		consts();
		a.mulsd(xmm0, cHalf); a.movapd(tmp, xmm0);
		reload();
		a.movapd(X(d), tmp);
	};
	auto addrWord = [&](int w) {                       // rax = (uint16_t)(position + offsets[w])
		a.movzx(eax, x86::word_ptr(r13, 2 * w)); a.add(eax, r14d); a.movzx(eax, ax);
	};

	for (size_t i = 0; i < n; i++) {
		const Op& m = ops[i];
		const Xmm xd = X(m.d), xa = X(m.a), xb = X(m.b);
		switch (m.t) {
		case MOV: if (m.d != m.a) a.movapd(xd, xa); break;
		case MOVR: {
			Label l0 = a.newLabel(), l1 = a.newLabel(), l2 = a.newLabel(), done = a.newLabel();
			a.movzx(eax, x86::byte_ptr(rbp, m.w));
			a.test(eax, eax); a.je(l0); a.cmp(eax, 1); a.je(l1); a.cmp(eax, 2); a.je(l2);
			a.movapd(xd, X(3)); a.jmp(done);
			a.bind(l0); a.movapd(xd, X(0)); a.jmp(done);
			a.bind(l1); a.movapd(xd, X(1)); a.jmp(done);
			a.bind(l2); a.movapd(xd, X(2));
			a.bind(done);
			break; }
		case SAT: a.movapd(tmp, xa); a.maxsd(tmp, cNeg); a.minsd(tmp, cSat); a.movapd(xd, tmp); break;
		case HALF: if (m.d != m.a) a.movapd(xd, xa); a.mulsd(xd, cHalf); break;
		case ADD:
			if (m.d == m.a) a.addsd(xd, xb);
			else if (m.d == m.b) a.addsd(xd, xa);
			else { a.movapd(xd, xa); a.addsd(xd, xb); }
			break;
		case SUB:
			if (m.d == m.a) a.subsd(xd, xb);
			else if (m.d != m.b) { a.movapd(xd, xa); a.subsd(xd, xb); }
			else { a.movapd(tmp, xa); a.subsd(tmp, xb); a.movapd(xd, tmp); }
			break;
		case NEG: if (m.d != m.b) a.movapd(xd, xb); a.xorpd(xd, cSign); break;
		case ZERO: a.xorpd(xd, xd); break;
		case ALU34: callLogic(m.d, m.a, m.b); break;
		case ALUV: {
			Label l4 = a.newLabel(), l1 = a.newLabel(), l3 = a.newLabel(), l5 = a.newLabel(), l6 = a.newLabel(), l0 = a.newLabel(), done = a.newLabel();
			a.movzx(eax, x86::byte_ptr(r15, m.k));
			a.cmp(eax, 2); a.jne(l4);
			if (m.d == m.a) a.addsd(xd, xb); else if (m.d == m.b) a.addsd(xd, xa); else { a.movapd(xd, xa); a.addsd(xd, xb); }
			a.jmp(done);
			a.bind(l4); a.cmp(eax, 4); a.jne(l1);
			if (m.d == m.a) a.subsd(xd, xb); else if (m.d != m.b) { a.movapd(xd, xa); a.subsd(xd, xb); } else { a.movapd(tmp, xa); a.subsd(tmp, xb); a.movapd(xd, tmp); }
			a.jmp(done);
			a.bind(l1); a.cmp(eax, 1); a.jne(l3); if (m.d != m.a) a.movapd(xd, xa); a.jmp(done);
			a.bind(l3); a.cmp(eax, 3); a.jne(l5); if (m.d != m.b) a.movapd(xd, xb); a.jmp(done);
			a.bind(l5); a.cmp(eax, 5); a.jne(l6); if (m.d != m.b) a.movapd(xd, xb); a.xorpd(xd, cSign); a.jmp(done);
			a.bind(l6); a.cmp(eax, 6); a.jne(l0); callLogic(m.d, m.a, m.b); a.jmp(done);
			a.bind(l0); a.xorpd(xd, xd);
			a.bind(done);
			break; }
		case LD: addrWord(m.w); a.movsd(xd, x86::ptr(r12, rax, 3)); break;
		case ST: addrWord(m.w); a.movsd(x86::ptr(r12, rax, 3), X(O)); break;
		case IN: a.movsd(xd, x86::ptr(rbx, OFS(curIn))); break;
		case DAC: {
			Label skip = a.newLabel();
			a.mov(eax, x86::dword_ptr(rbx, OFS(ndac))); a.cmp(eax, 2); a.jae(skip);
			a.movsd(x86::ptr(rbx, rax, 3, OFS(out)), X(O));
			a.bind(skip);
			a.add(x86::dword_ptr(rbx, OFS(ndac)), 1);
			break; }
		case CARRYIN: {
			Label skip = a.newLabel();
			a.cmp(x86::dword_ptr(rbx, OFS(ncarry)), 0); a.je(skip);
			spill();
			a.mov(win ? rcx : rdi, rbx); a.mov(win ? edx : esi, Imm(m.d));
			a.mov(rax, Imm((uint64_t)(uintptr_t)carry)); a.call(rax);
			consts();
			reload();
			a.bind(skip);
			break; }
		default: return false;
		}
	}

	for (int i = 0; i < NREG; i++) a.movsd(x86::ptr(rbx, kRegs + 8 * i), X(i));
	if (win) for (int j = 0; j < 10; j++) a.movups(xmm(6 + j), x86::ptr(rsp, 32 + 16 * j));
	a.add(rsp, frame);
	a.pop(r15); a.pop(r14); a.pop(r13); a.pop(r12); a.pop(rbp); a.pop(rbx);
	a.ret();
	return true;
}
#endif // PCM70_JIT_X64

Code compile(const Op* ops, size_t n, void (*carry)(State*, int), double (*logic)(double, double)) {
	JitRuntime& rt = runtime();
	CodeHolder code;
	if (code.init(rt.environment(), rt.cpuFeatures()) != kErrorOk) return Code();
#if defined(PCM70_JIT_A64)
	a64::Assembler a(&code);
#else
	x86::Assembler a(&code);
#endif
	if (!generate(a, ops, n, carry, logic)) return Code();
	Fn fn = nullptr;
	if (rt.add(&fn, &code) != kErrorOk || !fn) return Code();
	return Code(fn, (void*)fn);
}

void release(void* handle) { if (handle) runtime().release((Fn)handle); }

#else // no code generator for this architecture: Hsp runs the interpreter

Code compile(const Op*, size_t, void (*)(State*, int), double (*)(double, double)) { return Code(); }
void release(void*) {}

#endif

}}
