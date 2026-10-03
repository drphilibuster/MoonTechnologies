# Obfuscation — dispersion filter · FORM 1099-B

**A three-band dispersion matrix** in the manner of Kilohearts' Disperser. The
input is split into low, mid and high with Linkwitz-Riley 4 crossovers (250 Hz
and 2.5 kHz). Each band runs through up to **96 second-order allpass stages in
series** — no feedback — which leaves the magnitude flat but delays different
frequencies by different amounts (group delay). The bands are summed, saturated
(2x oversampled), tilted with BRIGHT, hard-clipped and boosted. Monophonic.

Dispersion is heard on transients (clicks become zaps and chirps) and when it
moves; a held tone through the allpasses alone sounds unchanged. The saturator
and clipper after it are what make the phase changes audible on sustained
material, because a phase-shifted waveform clips differently.

## The read-out

The screen is the matrix made visible: one lane per band (LO at the bottom),
every running stage's cutoff a tick on a log-frequency axis from 20 Hz to
20 kHz, the two crossovers (250 Hz, 2.5 kHz) marked in mint. SPREAD fans the
ticks out; RANDOM throws them about; FROZEN shows while the FREEZE gate is held.

The values on it are controls. F (FREQ), Q (PINCH) and N (STAGES) along the top
are dragged up and down -- the pointer turns into up/down arrows -- and RND and
CLK/ENV along the bottom are clicked to flip. FREQ keeps its knob as well,
because it is the one you play. Right-click any of them for the param's own menu.

## Controls

| Control | What it does |
|---|---|
| FREQ | Where the delay is centred, 40 Hz – 8 kHz (low band at 1/4, high band at 4x). CV via trimpot + jack. |
| PINCH | On the read-out (Q). The allpass Q (0.5 – ~30): concentrates the delay around FREQ. Low = broad, gentle; high = a sharp, long chirp. CV via trimpot + jack. |
| STAGES | On the read-out (N). 1 – 96 allpass stages per band (Disperser's Amount). Changes cross-fade one stage at a time. CV spans the whole range. |
| SPREAD | How far each stage's cutoff (up to ±2 octaves) and pinch stray from the knobs. At 0 every stage is tied to FREQ/PINCH. |
| RANDOM | On the read-out (RND). Enables randomization: every stage gets a new cutoff and pinch on each roll. |
| CLK/ENV | What triggers a roll: the CLOCK input, or peaks of the input signal. With nothing patched to CLOCK, peaks are used. |
| FREEZE gate | Acts as RANDOM-on while high, **and** freezes the current set: clock and peaks are ignored while it is held. Its rising edge rolls a new set. |
| DRIVE | Saturator amount. |
| BRIGHT | ±12 dB of everything above 3 kHz. |
| CLIP | Hard-clip threshold, 0.5 V – 5 V (fully up leaves only the 5 V ceiling). |
| BOOST | 0 – +24 dB, after the clipper. |

The LED flashes whenever the stages are re-rolled.

## Notes

- With RANDOM off, SPREAD still spreads the stages along a fixed golden-ratio
  ladder, so the result is repeatable.
- Rolls are slewed (~15 ms), so a new roll glides instead of zippering.
- At maximum pinch and 96 stages the chain holds hundreds of ms of group delay
  (longer at low FREQ), so a tail is expected; it always decays.
- Output is capped at ±12 V.
