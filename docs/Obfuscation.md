# Obfuscation — FORM 1099-B

**A three-band allpass matrix.** The input is split into low, mid and high with
Linkwitz-Riley 4 crossovers (250 Hz and 2.5 kHz). Each band runs through up to
**96 first-order allpass stages inside one feedback loop**; the bands are summed,
saturated (2x oversampled), tilted with BRIGHT, hard-clipped and boosted.
Monophonic.

## Controls

| Control | What it does |
|---|---|
| FREQ | Break frequency of the allpass stages, 40 Hz – 8 kHz (low band at 1/4, high band at 4x). CV via trimpot + jack. |
| RES | Loop feedback around each band's chain (soft-clipped, so it rings rather than blows up). CV via trimpot + jack. |
| STAGES | 1 – 96 allpass stages per band. Changes are cross-faded one stage at a time, so sweeping is click-free. CV spans the whole range. |
| SPREAD | How far each stage's cutoff (up to ±2 octaves) and each band's feedback stray from the knobs. At 0 every stage is tied to FREQ/RES — the plain version. |
| RANDOM | Enables randomization. |
| CLK/ENV | What triggers a new roll: the CLOCK input, or peaks of the input signal. With nothing patched to CLOCK, peaks are used. |
| FREEZE gate | Acts as RANDOM-on while high, **and** holds the matrix: the input to every loop is muted and feedback goes to ~unity, so the tail sustains. Its rising edge re-rolls. Releases over ~5 ms. |
| DRIVE | Saturator amount. |
| BRIGHT | ±12 dB of everything above 3 kHz. |
| CLIP | Hard-clip threshold, 0.5 V – 5 V (fully up leaves only the 5 V ceiling). |
| BOOST | 0 – +24 dB, after the clipper. |

The LED flashes whenever the stages are re-rolled.

## Notes

- With RANDOM off, SPREAD still spreads the stages along a fixed golden-ratio
  ladder, so the result is repeatable.
- Rolls are slewed (~15 ms), so a new roll glides instead of zippering.
- Output is capped at ±12 V.
