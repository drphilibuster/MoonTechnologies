# Obfuscation — FORM 1099-B

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

## Controls

| Control | What it does |
|---|---|
| FREQ | Where the delay is centred, 40 Hz – 8 kHz (low band at 1/4, high band at 4x). CV via trimpot + jack. |
| PINCH | The allpass Q (0.5 – ~30): concentrates the delay around FREQ. Low = broad, gentle; high = a sharp, long chirp. CV via trimpot + jack. |
| STAGES | 1 – 96 allpass stages per band (Disperser's Amount). Changes cross-fade one stage at a time. CV spans the whole range. |
| SPREAD | How far each stage's cutoff (up to ±2 octaves) and pinch stray from the knobs. At 0 every stage is tied to FREQ/PINCH. |
| RANDOM | Enables randomization: every stage gets a new cutoff and pinch on each roll. |
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
