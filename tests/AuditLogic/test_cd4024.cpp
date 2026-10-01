// The CD4024B ripple counter (INSTALLMENTS), against its datasheet's function table.
// The oracle is arithmetic: after N negative-going clock edges, Qn is bit n-1 of N.

#include "../../src/Cd4024.hpp"

#include <cstdio>

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}

int main() {
	printf("CD4024B: 7-stage ripple counter\n");

	// Every Qn is bit n-1 of the number of falling edges so far, over three full
	// wraps (384 clocks), and the period of Qn is 2^n clocks.
	{
		cd4024::Cd4024 c;
		bool bits = true, period = true;
		bool lastQ[8] = {};
		int lastRise[8] = {};
		for (int n = 1; n <= 384; n++) {
			c.process(true, false);
			c.process(false, false);
			for (int s = 1; s <= 7; s++) {
				bool want = (n >> (s - 1)) & 1;
				if (c.q(s) != want) bits = false;
				if (c.q(s) && !lastQ[s]) {
					if (lastRise[s] && n - lastRise[s] != (1 << s)) period = false;
					lastRise[s] = n;
				}
				lastQ[s] = c.q(s);
			}
		}
		check("Qn is bit n-1 of the falling-edge count, three wraps", bits);
		check("Qn rises every 2^n clocks", period);
	}

	// Counts on the NEGATIVE-going edge only: the rising edge does nothing.
	{
		cd4024::Cd4024 c;
		bool a = c.process(true, false);
		check("a rising edge does not count", !a && c.count == 0);
		bool b = c.process(false, false);
		check("a falling edge counts", b && c.count == 1);
		bool d = c.process(false, false);
		check("a held low level does not count", !d && c.count == 1);
	}

	// RESET is a level: it zeroes the count and the clock is ignored while it is high.
	{
		cd4024::Cd4024 c;
		for (int i = 0; i < 37; i++) { c.process(true, false); c.process(false, false); }
		check("37 clocks", c.count == 37);
		c.process(true, true);
		check("RESET high clears the count", c.count == 0);
		bool any = false;
		for (int i = 0; i < 20; i++) { any |= c.process(true, true); any |= c.process(false, true); }
		check("clock falling edges are ignored while RESET is high", !any && c.count == 0);
		// Released with the clock low: the next full cycle counts.
		c.process(false, false);
		c.process(true, false);
		c.process(false, false);
		check("counts again once RESET is released", c.count == 1);
	}

	// An edge that falls during RESET does not count when RESET drops afterwards.
	{
		cd4024::Cd4024 c;
		c.process(true, true);
		c.process(false, true);                 // fell under RESET: ignored
		bool a = c.process(false, false);       // RESET released, clock still low
		check("no count from an edge swallowed by RESET", !a && c.count == 0);
	}

	// Negative control: a counter that counts on rising edges would differ from the
	// first falling edge onward, so the oracle above really does see the polarity.
	{
		cd4024::Cd4024 c;
		c.process(true, false);
		check("polarity control: after only a rising edge Q1 is still low", !c.q(1));
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
