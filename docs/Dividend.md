# Dividend

A pulsar-synthesis VCO for VCV Rack 2. FORM 1099-DIV: the payout is a train of
pulsarets, and how many of them you actually receive is a matter of withholding.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

Pulsar synthesis is Curtis Roads' technique, described in *Microsound* (MIT
Press, 2001), chapter 4, and first implemented in his PulsarGenerator (with
Alberto de Campo, 1998–2000). It sits between the classic waveform oscillator
and granular synthesis: a **pulsar** is one period `p` of the fundamental,
inside which a short **pulsaret** of duration `d` sounds and the rest is
silence. The pulsaret is a few cycles of a waveform shaped by an envelope, the
pulsaret window.

Two independent frequencies fall out of that:

* the **fundamental** `1/p` — how often a pulsaret is issued;
* the **formant** `1/d` — the pitch of the waveform inside the pulsaret, which
  the ear hears as a resonant peak in the spectrum.

The **duty cycle** `d/p` is the ratio between them. When `d/p` is small the
train is a click train with a formant riding on it; as `d` approaches `p` the
pulsarets touch and it becomes an ordinary oscillator; past `p` they either
have to be cut short or allowed to overlap. Roads also describes **masking**:
withholding individual pulsars, in patterns (burst masking), at random
(stochastic masking), or by sending alternate pulsarets to different output
channels (channel masking). All of that is here.

This module's contribution is the modular plumbing — 1 V/oct, CV over the
formant and the masking, sync, a gate and envelope per pulsaret — and a
context-menu choice of how band-limiting and overlap are handled.

## Signal flow

```
FREQ + FINE + V/OCT + FM ──► pulsar clock ──► masking ──► pulsaret voice(s) ──► L / R
FORMANT (+ CV, tracked or absolute) ──┘               (burst, PROB, L/R)     TRIG, ENV
WAVE, WINDOW, CYCLES ────────────────────────────────────────────┘
```

Each pulsaret holds `CYCLES` cycles of the chosen waveform under the chosen
window, so its duration is `d = CYCLES / formant`. That keeps the formant
frequency where the spectral peak actually is: turning CYCLES up makes the
pulsaret longer and the formant narrower and more resonant, without moving it.

## Controls

### PAYOUT

| Control | Range | Default | What |
|---|---|---|---|
| FREQ | ±4 octaves around C4 (16 Hz – 4.2 kHz before CV) | C4, 261.6 Hz | The fundamental: the pulsar rate. The one control with the lime ring. |
| FINE | ±1 semitone | 0 | Fine tune. |
| FORMANT | −2 … +6 octaves | +2 | Formant frequency. With TRACK up it is a ratio to the fundamental (×0.25 … ×64, default ×4); with TRACK down it is absolute (65 Hz – 16.7 kHz, default 1046 Hz). The tooltip reports whichever applies. |
| TRACK | switch | up (tracks) | Up: the formant follows FREQ, V/OCT and FM, so the timbre stays put as the pitch moves. Down: the formant is fixed in Hz and the duty cycle changes with the pitch, the classic pulsar behaviour. |
| FM | switch | down (exp) | The law the FM input follows. Down: exponential, 1 V/oct through the FM attenuverter. Up: linear through-zero — `f = f0 × (1 + FM × amount / 5)`, so ±5 V at full amount sweeps the train through zero and out the other side, running backwards. |

### DISTRIBUTION

| Control | Range | Default | What |
|---|---|---|---|
| WAVE | 6 positions | Sine | The pulsaret waveform: **Sine**, **Sinc** (`sin πt / πt`, CYCLES lobes each side of the centre), **Saw**, **Square**, **Triangle**, **Cosine burst** (cosine starting at its peak, so the onset is a step). |
| WINDOW | 6 positions | Gaussian | The pulsaret envelope: **Rectangular**, **Gaussian** (σ = 0.15 of the pulsaret), **Hann**, **Exponential decay**, **Reverse exponential** (a swell), **Linear decay**. |
| CYCLES | 1 – 8, continuous | 1 | Cycles of the waveform per pulsaret. Fractional values are allowed; under a rectangular window they end mid-cycle, which is a step. |
| L/R | switch | down (off) | Channel masking. Down: mono, R follows L. Up: paid pulsarets alternate between OUT L and OUT R — the stereo pulsar. |

### WITHHOLDING

| Control | Range | Default | What |
|---|---|---|---|
| ON | 1 – 8 | 1 | Burst masking: pulsars paid per burst cycle. |
| OFF | 0 – 8 | 0 | Burst masking: pulsars withheld after them. `ON 3 / OFF 1` pays three, skips one. |
| PROB | 0 – 100 % | 100 % | Stochastic masking: the chance a pulsar that survives the burst pattern is actually paid. |
| HELD | light | | Lights for every pulsar withheld. A flash at LFO rates, a glow at audio rates. |
| FM, FMT, PROB, BURST | ±100 % | 0 | Attenuverters for the four CV inputs directly below them. |

Masking is applied in that order — burst pattern first, then the coin toss,
then (for the pulsars that are paid) alternation between channels — so PROB
thins a burst pattern rather than replacing it, and L/R alternation counts only
what was paid.

## Jacks

| Jack | Direction | Convention |
|---|---|---|
| V/OCT | in | 1 V/oct pitch, added to FREQ + FINE. Monophonic: a polyphonic cable contributes its first channel. |
| SYNC | in | Rising edge (Schmitt, 0.1 V / 2 V) hard-resets the train: the pulsar clock restarts at zero, the burst pattern restarts at its first step, anything still sounding is cut, and a new pulsar begins immediately. |
| FM | in | Frequency modulation through the FM attenuverter; exponential or linear through-zero per the FM switch. |
| FMT | in | Formant CV, 1 V/oct through its attenuverter, in both TRACK modes. |
| PROB | in | 0 – 10 V adds up to ±100 % to PROB through its attenuverter. |
| BURST | in | 0 – 10 V adds up to ±8 to ON through its attenuverter. |
| OUT L | out | ±5 V audio for a single pulsaret; overlapping pulsarets sum and are clamped at ±12 V. Mono when L/R is down. |
| OUT R | out | As above; follows L unless L/R is up. |
| TRIG | out | 0 / 10 V gate, high while any pulsaret is sounding — the duty cycle as a gate. With overlapping pulsarets longer than the period it stays high. |
| ENV | out | 0 – 10 V, the window of the most recently issued pulsaret. |

The pulsaret's loudness is what it is: at a small duty cycle the train is
mostly silence and its RMS level is low. That is the technique, not a fault; a
VCA or a limiter after it is the usual answer.

## Read-out

Two lines. `FUND` is the fundamental in Hz (or kHz) after V/OCT and FM, and
`TRACK` / `ABS` says which formant mode is in force. `FMT` is the resulting
formant frequency, `D/P` the duty cycle it makes against the period
(`FUND × CYCLES / FMT`), and `L/R` / `MONO` the channel-masking state. Values
above 99.99 read as 99.99.

## Context menu

* **Band-limiting** — Off (1×), **2× oversampling** (default), 4× oversampling.
  The steps at pulsaret boundaries (and inside saw, square, cosine-burst and
  rectangular-window pulsarets) alias. The core runs at the chosen multiple of
  the sample rate and is decimated through Rack's windowed-sinc FIR
  (`dsp::Decimator`, 16 taps per phase). This was chosen over MinBLEP because a
  windowed pulsaret's step heights vary with the window and with truncation, and
  oversampling handles every case uniformly; 2× is usually plenty, 4× is there
  for high formants with hard windows. Pulsar onsets are placed with sub-sample
  accuracy regardless of this setting, so it changes aliasing, not pitch.
* **Pulsarets longer than the period** — **Truncate at the period** (default):
  a pulsaret is cut when the next pulsar begins, whether or not that pulsar is
  paid, so `d/p > 1` behaves like a pulse-width control. **Overlap (8 voices)**:
  pulsarets are allowed to run past the period and sum; a pool of eight voices
  serves them and, if all eight are busy, the oldest is stolen.
* **DC blocker** — on by default: a 5 Hz one-pole high-pass on both outputs.
  Asymmetric windows over asymmetric waveforms carry a little DC; at rhythmic
  (sub-audio) pulsar rates the blocker passes the pulsarets untouched.

## Polyphony

Dividend is monophonic. Overlapping pulsarets already need a pool of voices per
train, and sixteen trains of eight would be a different module; the V/OCT
input reads channel 1 of a polyphonic cable.

## Notes

* The fundamental after all modulation is clamped to ±0.45 × sample rate, the
  formant to between 1 Hz and 0.45 × the oversampled rate.
* In linear through-zero mode the fundamental can be negative; the train then
  runs backwards (pulsars are issued as the phase crosses zero in either
  direction) while each pulsaret still plays forward. The read-out shows the
  magnitude.
* `onReset` clears the train and the voice pool; the stochastic mask is seeded
  once per module instance and is not saved.
