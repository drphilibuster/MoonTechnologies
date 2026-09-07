# Reconciliation

A just-intonation quantizer for VCV Rack 2. Schedule M-1 — income per books
against income per return: two records of the same thing, made to agree.

Part of the [Moon Technologies](../README.md) plugin.

Most quantizers answer one question — *which note of this scale is nearest?* —
and the only interesting thing about them is which scale you loaded. This one
splits that into two questions and puts a knob on each.

**Which pitches exist** is `BASIS`: Harry Partch's eleven-limit tonality diamond
and his 43-tone scale, one Otonality or Utonality hexad out of that diamond, two
of Erv Wilson's combination product sets, or the raw harmonic series.

**How the choice among them is made** is `RECONCILE`: five different published
answers to "which of these ratios is simpler", which *disagree with each other*,
plus plain nearest-neighbour to compare them against.

And it tells you what it did. The read-out names the ratio — `11/8`, not "a bit
flat of a tritone" — with its size in cents and how far that is from the
twelve-tone pitch it is nearest.

## Layout

21 HP. `BASIS` is which pitches exist, `RECONCILE` is how one gets chosen and
what happens to it on the way out, `ALLOWANCES` is what the CV inputs may take
off the four controls worth modulating. Polyphonic: `OUT`, `TRIG` and `PURITY`
follow `IN`'s channel count, up to sixteen.

## BASIS

| Control | Type | Description |
|---|---|---|
| **SET** | 6-position snap knob | Which structure — see below |
| **NEXUS** | 6-position snap knob | Which of Partch's identities the structure stands on: 1, 3, 5, 7, 9, 11 |
| **UTONAL** | switch | The mirror. Partch's major and minor |
| **PRIME** | 4-position snap knob | Prime limit: 3, 5, 7 or 11. Prunes the set to ratios whose largest prime factor is at or below it |

### SET

| Setting | What it is | Pitches |
|---|---|---|
| **DIAMOND** | Partch's eleven-limit tonality diamond: every ratio *i*/*j* with *i* and *j* drawn from 1, 3, 5, 7, 9 and 11 | 29 |
| **43-TONE** | Partch's complete scale — the diamond plus fourteen "multiple-number ratios" that fill its gaps | 43 |
| **HEXAD** | One row or one column of the diamond: the Otonality 4:5:6:7:9:11, or the Utonality that mirrors it | 6 |
| **HEXANY** | Erv Wilson's 2)4 combination product set on 1·3·5·7 — the products of the pairs | 6 |
| **EIKOSANY** | Wilson's 3)6 set on 1·3·5·7·9·11 — the products of the triples | 20 |
| **SERIES** | Harmonics 8 through 15, or the same eight undertones. The only place the 13th harmonic appears, and `PRIME` is what takes it away | 8 |

The diamond is thirty-six cells and twenty-nine distinct pitches: the six
unisons on its diagonal collapse to one, and 9/3 and 3/9 repeat 3/1 and 1/3. It
is symmetric by construction — every interval in it has its own inversion — and
so is the 43, which is why the `UTONAL` switch does nothing in those two SETs.
That is not an oversight; it is a fact about how Partch built them, and the
switch has plenty to do everywhere else.

Wilson's two sets are *uncentered*: a combination product set implies no tonic
at all, and the raw 1-3-5-7 hexany starts on 35/32 rather than a unison. A
quantizer has to put something at the root, so they are normalised onto their
own lowest member — the intervals inside them, which are what the sets are, are
untouched. The 1-3-5-7 hexany comes out as 1/1 8/7 6/5 48/35 8/5 12/7.

### NEXUS

Partch's numerary nexus: the identity a tonality is built on. An Otonality with
nexus 7 is `{1/7, 3/7, 5/7, 7/7, 9/7, 11/7}`, which is the Otonality on 1
transposed down by 7/4 — so `NEXUS` is implemented as a shift of the root, which
is not a shortcut but what a numerary nexus *is*. `UTONAL` flips the direction of
that shift, so between them the twelve tonalities of the diamond are two knob
positions apart.

In the other SETs the same control simply transposes the structure onto that
identity, which is a modulation to a harmonically related key rather than an
arbitrary one.

### PRIME

The one control that means the same thing in every SET. At the 11 limit
everything survives; at 7 the eleventh-partial ratios go; at 5 you are in
classical five-limit just intonation; at 3 you are in Pythagorean tuning and the
diamond has collapsed to five pitches — 1/1, 9/8, 4/3, 3/2, 16/9.

It is a *prime* limit rather than Partch's odd limit deliberately, because an
odd-limit filter set to 11 would throw away most of the 43-tone scale: 81/80 is
3⁴/(2⁴·5), an odd limit of 81 but a prime limit of 5.

## RECONCILE

| Control | Type | Description |
|---|---|---|
| **RULE** | 6-position snap knob | How the choice is made — see below |
| **BIAS** | large knob | How hard the rule pulls against plain distance. At zero every rule collapses to NEAREST |
| **WINDOW** | knob | How far, in cents, the rule may reach past the nearest pitch. Up to 150 |
| **DEGREE** | knob, snapped | Transpose by −12 to +12 steps *along the set* |
| **HYST** | knob | Dead band, as a fraction of the local step |
| **SLEW** | knob | Portamento, in seconds per octave |

`WINDOW` and `BIAS` are the two halves of one idea. The window is a hard bound —
nothing further from the input than that may be chosen — and the bias is how
strongly, inside it, the rule's preference outweighs being close. The nearest
pitch is always eligible whatever the window is, so there is always an answer.

### RULE

| Setting | Prefers | Source |
|---|---|---|
| **NEAREST** | the smallest distance in cents | the control the other five are worth comparing against |
| **TENNEY** | the lowest harmonic distance, log₂(*n·d*) | James Tenney |
| **BARLOW** | the highest harmonicity, from the indigestibility 2(*p*−1)²/*p* | Clarence Barlow |
| **EULER** | the lowest *gradus suavitatis* | Leonhard Euler, 1739 |
| **SETHARES** | the least sensory dissonance against the assumed timbre | Sethares, after Plomp and Levelt |
| **ADAPTIVE** | the purest interval **from the last note**, not from the root | adaptive just intonation |

They disagree, and that is the reason all five are here rather than one.
Tenney's measure rates a ratio by the size of its numbers; Barlow's rates it by
the primes *in* the numbers, so 9/8 beats 7/4 for him and loses to it for
Tenney. Euler's is older than either and counts prime factors with multiplicity.
Sethares' is the only one that knows anything about **timbre**, and that is his
whole argument: which intervals sound settled is a fact about the spectrum you
play them with. Change the assumed timbre in the context menu and the answers
move.

`NEAREST` ignores `BIAS` entirely — nearest means nearest — but it is still
scored, because `PURITY` reports how consonant the chosen ratio is and a jack
that reads a flat ten volts in one mode is a broken jack.

### ADAPTIVE, and the drift

`ADAPTIVE` measures each new interval from where the last one landed rather than
from the root. That is what keeps every interval pure, and it is also why the
tonal centre walks: play I–IV–V–I in pure just intonation and you come home a
syntonic comma flat. That is not a bug in the module — it is the oldest known
problem with just intonation, the reason temperament was invented, and `DRIFT`
is it as a voltage. `RESET` puts the centre back on the root; the drift is held
at one octave either way so it cannot walk out of hearing while your back is
turned.

Polyphonically, channel 0 is the anchor and the rest are tuned against the note
it landed on, so a chord comes out pure *within itself* rather than each voice
pure against a root that none of them are playing.

### DEGREE, HYST and SLEW

`DEGREE` walks along the set rather than across the keyboard: one step is
whatever the next ratio in this structure happens to be, which at `DIAMOND` is
anything from fourteen cents to a whole tone. Stepping off the end wraps into
the next octave. It is a different move from transposing the input, and with a
sequencer patched into its CV it is the most direct way to play the diamond as a
scale.

`HYST` is a dead band, given as a fraction of the gap between the two pitches
competing for the input rather than as a count of cents — because these sets are
wildly uneven, and any absolute setting wide enough to steady a 43-tone scale
would make a hexad unplayable. At its widest the dead band is half a step, which
is as far as it can go and still leave every degree reachable.

`SLEW` is in seconds per octave, so a wide leap glides for proportionally longer
than a narrow one, which is what a portamento does and what a fixed time
constant does not.

## Footer

| Jack | Direction | Description |
|---|---|---|
| **IN** | input, polyphonic | 1 V/oct |
| **ROOT** | input | The 1/1 that every ratio is measured from. Unpatched, 0 V |
| **RESET** | input | Returns ADAPTIVE's tonal centre to the root |
| **OUT** | output, polyphonic | The quantized pitch |
| **TRIG** | output, polyphonic | A trigger on every change of pitch |
| **PURITY** | output, polyphonic | 0–10 V: how consonant the chosen ratio is, under whichever rule is selected. Patch it at a filter and the patch opens up on the simple intervals |
| **DRIFT** | output | ADAPTIVE's tonal centre relative to the root. Zero in every other mode |

## Menu

* **Assumed timbre (Sethares)** — 4, 7 or 12 harmonic partials. Only `SETHARES`
  reads it, and changing it is the fastest way to hear his point: the consonant
  intervals move when the spectrum does.

## Patches to start from

**Hear the diamond.** `SET` on DIAMOND, `RULE` on NEAREST, a slow ramp into `IN`,
`OUT` to an oscillator. Twenty-nine pitches to the octave, all of them
rational. Now turn `PRIME` down through 7, 5 and 3 and listen to the scale
losing its strangeness one prime at a time.

**Partch's major and minor.** `SET` on HEXAD, a sequencer into `IN`. Flip
`UTONAL` and the same sequence becomes its own mirror image — this is exactly
what Partch meant by the two being analogues of major and minor. Then walk
`NEXUS` through the six identities to move between the twelve tonalities of the
diamond.

**Watch the rules disagree.** `SET` on 43-TONE, `WINDOW` around 60 cents,
`BIAS` up full, and a slow ramp into `IN` with `OUT` on a scope. Now step
`RULE`. The same input lands on different ratios under Tenney, Barlow and Euler,
and each of them will tell you it is the simple one.

**Timbre chooses the scale.** `RULE` on SETHARES, `BIAS` up, `WINDOW` wide.
Set the assumed timbre to 4 partials and play a line; set it to 12 and play the
same line. Different notes come out, because with more partials there are more
places for them to coincide.

**The comma pump.** `RULE` on ADAPTIVE, `SET` on DIAMOND, `DRIFT` to a scope.
Play a I–IV–V–I loop into `IN`. Every interval is pure and the whole thing sinks,
a syntonic comma per turn of the loop. Patch `DRIFT` back into `ROOT` through an
inverting attenuator and you have built a temperament.

**Purity as a modulator.** `PURITY` to a filter cutoff or a wavefolder. The
patch brightens on 3/2 and 5/4 and closes down on 11/9 and 14/11, which turns
harmonic complexity into timbre without your having to sequence it.

## Prior art

* H. Partch, *Genesis of a Music*, 2nd ed. (Da Capo, 1974) — the identities, the
  tonality diamond, Otonality and Utonality, and the 43-tone scale.
* E. Wilson's combination product sets — the hexany and the eikosany.
* L. Euler, *Tentamen novae theoriae musicae* (St Petersburg, 1739), ch. IV —
  the *gradus suavitatis*.
* J. Tenney, *A History of "Consonance" and "Dissonance"* (1988), and "John Cage
  and the Theory of Harmony" (1983) — harmonic distance.
* C. Barlow, "Bus Journey to Parametron" (*Feedback Papers* 21–23, 1980) and
  "On the Quantification of Harmony and Metre" (1987) — indigestibility and
  harmonicity. Barlow's harmonicity also carries a polarity; this module ranks
  by magnitude only, and says so rather than pretending to a convention it does
  not use.
* R. Plomp and W. J. M. Levelt, "Tonal consonance and critical bandwidth",
  *JASA* **38** (1965), 548–560; W. A. Sethares, "Local consonance and the
  relationship between timbre and scale", *JASA* **94** (1993), 1218–1228, and
  *Tuning, Timbre, Spectrum, Scale* (Springer, 1998).

## Notes

* The engine is [`src/Reconciliation/Tuning.hpp`](../src/Reconciliation/Tuning.hpp)
  and includes nothing from Rack, so
  [`tests/Reconciliation`](../tests/Reconciliation) checks the shipping tables
  with a host compiler. It does not check the ratios one at a time against a
  list copied from the same source — that would only prove the copy was
  faithful. It checks the properties their authors said they have: that no step
  in the 43 is smaller than 121/120 or larger than 45/44, that the diamond has
  29 pitches and is its own mirror, that the 43 contains it and adds exactly
  fourteen, and that the minima of the Sethares curve land on diamond ratios
  within two cents.
* The dissonance curve is tabulated once a cent and rebuilt only when the root
  moves materially or the timbre changes; the pitch set is rebuilt only when
  something it depends on moves; and a channel is re-quantized only when its
  input changes. None of that work happens per sample.
