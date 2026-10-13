// Pcm70HspJit.hpp: what the HSP's run loop and its native-code twin share (src/Pcm70Hsp.hpp, src/Depreciation/HspJit.cpp).
//
// The interpreter walks the decoded operation list with the register file in memory, so each operation waits for the store of the one before it to forward
// (about ten cycles for an add). The same list as native code keeps the eleven registers in vector registers and pays the arithmetic's own latency. The two read
// and write one `State`, so a sample can be run by either and the results compared; the interpreter stays as the fallback (no JIT on the platform, an allocation
// refused, a self-test that disagrees) and as the reference.
//
// This header is plain C++11 with no assembler types, so the module's own code, which is C++11, can include it. The code generator is in HspJit.cpp (C++17, asmjit).
#pragma once
#include <cstdint>
#include <cstddef>

namespace pcm70 { namespace hspjit {

enum { NREG = 11 };           // R0-R3 = 0-3, ACC 4, O 5, A 6, P 7, then the bus latch and the ALU's two temporaries 8-10
enum { MAXCARRY = 16 };

// the unfused operations; Hsp's own operation enum begins with these, in this order (it adds the fused pairs after them)
enum OpT : uint8_t { MOVR, MOV, SAT, HALF, ADD, SUB, NEG, ZERO, ALU34, ALUV, LD, ST, IN, DAC, CARRYIN, NUM_OPS };

struct Op {
	uint8_t t, d, a, b;
	uint16_t w;               // LD/ST/MOVR: the program word whose offset (or register select) the operation reads live
	uint16_t k;               // ALUV: which entry of the per-sample kind table it reads
};

/** Everything one sample of the HSP reads and writes. The native code addresses it by offsetof, so it stays standard-layout. */
struct State {
	double r[NREG] = {};
	double curIn = 0.0;
	double out[2] = {};
	double* mem = nullptr;               // the 64 K-word delay RAM
	const uint16_t* off = nullptr;       // the 128 words' memory offsets, live (the slave rewrites them continuously)
	const uint8_t* ras = nullptr;        // the 128 words' register selects, live
	const uint8_t* aluk = nullptr;       // ALUV kinds for this sample (see Hsp::aluKind)
	uint32_t ps = 0;                     // the sample position the offsets are added to
	int32_t ndac = 0;                    // DAC writes so far this sample (the first goes to the left output, the second to the right)
	int32_t ncarry = 0;                  // events issued late in the previous sample, still to run
	uint8_t ckind[MAXCARRY] = {}, cwa[MAXCARRY] = {}, cdue[MAXCARRY] = {};
	uint16_t caddr[MAXCARRY] = {};
};

typedef void (*Fn)(State*);

/** A compiled program. Move-only; releases the code when it goes. */
class Code {
public:
	Code() {}
	Code(Fn f, void* h) : fn_(f), h_(h) {}
	Code(Code&& o) : fn_(o.fn_), h_(o.h_) { o.fn_ = nullptr; o.h_ = nullptr; }
	Code& operator=(Code&& o) { if (this != &o) { reset(); fn_ = o.fn_; h_ = o.h_; o.fn_ = nullptr; o.h_ = nullptr; } return *this; }
	Code(const Code&) = delete; Code& operator=(const Code&) = delete;
	~Code() { reset(); }
	explicit operator bool() const { return fn_ != nullptr; }
	void operator()(State* s) const { fn_(s); }
	void reset();
private:
	Fn fn_ = nullptr; void* h_ = nullptr;
};

#ifdef PCM70_HSP_JIT
/** Native code for `ops` (n of them), or an empty Code when the platform will not give executable memory. `carry` runs the events due at the given tick of a
    sample (CARRYIN), `logic` is the ALU's AND-NOT (kind 6, and ALU34). Both are called with the host's C convention. */
Code compile(const Op* ops, size_t n, void (*carry)(State*, int), double (*logic)(double, double));
void release(void* handle);
inline void Code::reset() { if (h_) release(h_); fn_ = nullptr; h_ = nullptr; }
#else
inline void Code::reset() { fn_ = nullptr; h_ = nullptr; }
#endif

}}
