#pragma once
// The Olegtron R2R as the linear network it is. No Rack dependency, so this can
// be checked on its own against the ratios printed in the manual.
//
// Topology, read off the schematic in the R2R manual:
//
//   GND --20k-- N1 --10k-- N2 --10k-- N3 --10k-- N4 --10k-- N5 --10k-- N6 --10k-- N7 --10k-- N8 --- I/O
//               |          |          |          |          |          |          |          |
//              20k        20k        20k        20k        20k        20k        20k        20k
//               |          |          |          |          |          |          |          |
//               1          2          4          8          16         32         64        128
//
// Eight ladder nodes N1..N8. Jack "1" hangs off N1 through 20k, "2" off N2, and
// so on up to "128" off N8; the I/O jack IS N8, with no branch resistor -- which
// is why the manual says I/O to 128 is "only a 20k impedance". N1 is tied to
// ground through the 20k termination, the standard R-2R end. All of that is
// confirmed by the three worked circuits in the manual: I/O in, 4 out is 40k to
// ground and 50k to I/O; 64 in, 32 out is 70k to ground, 20k branch, 30k on to
// 64; and the attenuator table 0.22 .. 1.00 for I/O in falls out of the same
// numbers (N_k / N8 = (10k + 10k*k) / 90k).
//
// A jack is one of three things: a voltage source (a cable is driving it), a
// short to ground (the official "normalize to ground" mod, on an unplugged jack),
// or open. Anything that is not a source reads its node's voltage unloaded --
// Rack inputs are ideal, so the 20k output impedance drops nothing.
//
// Given which jacks are sources and which are grounded, the node voltages are a
// fixed linear map of the source voltages. That map is solved once per
// connection pattern (Gauss-Jordan on an 8x8, with the source conductances as
// nine right-hand sides) and applied per sample as an 8x9 matrix-vector
// product. Nothing here allocates.

#include <cmath>
#include <cstdint>

namespace taxbracket {

struct Ladder {
	static const int NODES = 8;         // N1..N8
	static const int JACKS = 9;         // bits 0..7 (jacks 1..128), then I/O
	static const int IO = 8;

	float rSeries = 10000.f;            // between adjacent nodes
	float rBranch = 20000.f;            // node to its jack
	float rTerm   = 20000.f;            // N1 to ground

	/** T[node][source]: volts at the node per volt at the source, for the
	    current connection pattern. Columns of jacks that are not sources are
	    zero. */
	float T[NODES][JACKS];

	Ladder() { invalidate(); }

	/** Forget the cached pattern, so the next configure() re-solves. */
	void invalidate() {
		key = 0xFFFFFFFFu;
		for (int i = 0; i < NODES; i++)
			for (int j = 0; j < JACKS; j++)
				T[i][j] = 0.f;
	}

	/** `driven`: bit j set if jack j is a voltage source. `grounded`: bit j set
	    if jack j is tied to 0 V (through its branch for 1..128; directly for
	    I/O, which is the node). A jack that is both counts as driven. Re-solves
	    only when the pair changes. */
	void configure(unsigned driven, unsigned grounded) {
		driven &= (1u << JACKS) - 1;
		grounded &= (1u << JACKS) - 1;
		grounded &= ~driven;
		uint32_t k = driven | (grounded << JACKS);
		if (k == key)
			return;
		key = k;
		solve(driven, grounded);
	}

	/** Node voltages for one set of source voltages. src[j] is ignored for any
	    jack that is not a source. */
	void evaluate(const float* src, float* v) const {
		for (int i = 0; i < NODES; i++) {
			float s = 0.f;
			for (int j = 0; j < JACKS; j++)
				s += T[i][j] * src[j];
			v[i] = s;
		}
	}

	/** The node a jack reads: jack k (0..7) sits on N(k+1); I/O is N8. */
	static int nodeOf(int jack) { return jack == IO ? NODES - 1 : jack; }

private:
	uint32_t key;

	void solve(unsigned driven, unsigned grounded) {
		// Nodal analysis. G is the conductance matrix of the passive network
		// plus whatever the jacks add; B holds the conductance from each node
		// to each source, so G * V = B * Vsrc and T = G^-1 * B.
		double G[NODES][NODES];
		double B[NODES][JACKS];
		for (int i = 0; i < NODES; i++) {
			for (int j = 0; j < NODES; j++) G[i][j] = 0.0;
			for (int j = 0; j < JACKS; j++) B[i][j] = 0.0;
		}
		const double gs = 1.0 / rSeries;
		const double gb = 1.0 / rBranch;
		const double gt = 1.0 / rTerm;

		G[0][0] += gt;                                  // the termination
		for (int i = 0; i < NODES - 1; i++) {           // the string
			G[i][i]         += gs;
			G[i + 1][i + 1] += gs;
			G[i][i + 1]     -= gs;
			G[i + 1][i]     -= gs;
		}
		for (int k = 0; k < NODES; k++) {               // the branches
			bool src = (driven >> k) & 1u;
			bool gnd = (grounded >> k) & 1u;
			if (src || gnd)
				G[k][k] += gb;                          // 20k to something
			if (src)
				B[k][k] = gb;                           // ... and that something is a source
		}
		// The I/O jack is N8 itself. A source there pins the node outright; a
		// ground there pins it to zero. Either way row 8 stops being a KCL
		// equation and becomes the constraint V8 = whatever.
		const int last = NODES - 1;
		if (((driven >> IO) & 1u) || ((grounded >> IO) & 1u)) {
			for (int j = 0; j < NODES; j++) G[last][j] = 0.0;
			for (int j = 0; j < JACKS; j++) B[last][j] = 0.0;
			G[last][last] = 1.0;
			if ((driven >> IO) & 1u)
				B[last][IO] = 1.0;
		}

		// Gauss-Jordan with partial pivoting on [G | B]. G is an M-matrix (or
		// one with a unit row swapped in), so it is never singular and the
		// pivots stay comfortably away from zero.
		for (int c = 0; c < NODES; c++) {
			int p = c;
			for (int r = c + 1; r < NODES; r++)
				if (std::fabs(G[r][c]) > std::fabs(G[p][c]))
					p = r;
			if (p != c) {
				for (int j = 0; j < NODES; j++) { double t = G[c][j]; G[c][j] = G[p][j]; G[p][j] = t; }
				for (int j = 0; j < JACKS; j++) { double t = B[c][j]; B[c][j] = B[p][j]; B[p][j] = t; }
			}
			double inv = 1.0 / G[c][c];
			for (int j = 0; j < NODES; j++) G[c][j] *= inv;
			for (int j = 0; j < JACKS; j++) B[c][j] *= inv;
			for (int r = 0; r < NODES; r++) {
				if (r == c)
					continue;
				double f = G[r][c];
				if (f == 0.0)
					continue;
				for (int j = 0; j < NODES; j++) G[r][j] -= f * G[c][j];
				for (int j = 0; j < JACKS; j++) B[r][j] -= f * B[c][j];
			}
		}
		for (int i = 0; i < NODES; i++)
			for (int j = 0; j < JACKS; j++)
				T[i][j] = (float) B[i][j];
	}
};

} // namespace taxbracket
