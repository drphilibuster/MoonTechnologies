# Changelog

Versions follow the VCV convention: the major number is the Rack major version
these modules run on, so a Rack 2 plugin is always `2.x.y`.

## Unreleased

### Fixed: Amortization changed mode by fading into a tank that was switched off

The hardware does not crossfade. Run the feedback up until the loop is ringing,
change algorithm, and the energy already in one structure arrives in the other
and is rung as something it was never given -- metallic tails, delay trails,
tones that were not in the input. This module went quiet instead.

The comment in the code said the opposite of what the code did:

> Both always run, so a mode change is a crossfade between two live tails
> rather than a cut into a cold one

The input to each tank was scaled by the fade, so the *inactive* tank received
nothing and was silent. The outputs were then crossfaded as well -- a second
attenuation on top of the first, quarter gain at the midpoint. Every mode change
was a fade through a hole into a tank that had never been fed.

Three changes, in `Tanks.hpp` as a `ModeCollider` so they can be measured:
both tanks are fed the whole input at all times, so the one being arrived at is
already ringing; the fade is equal power; and during a change the two are
cross-fed, each tank's output driven into the other's input, scaled by how far
across the change we are and by how hot the loop already was. Zero at either
end, so it costs nothing when the mode is not moving.

At FEEDBACK 0.95, measured over the 200 ms after a change against the 200 ms
before it: **0.39x before this, 1.87x after**. The tail half a second later went
from nothing at all to 1.85x. Both figures come from a negative control that
puts the old path back.

Worth recording a wrong turn, because it was stated as a finding before it was
checked: the two tanks were first measured at the same *raw* parameter, where
TRONIC looked fifty times quieter, and that was reported as the cause. The
module never drives them that way -- FEEDBACK maps into `cDecay = 0.97 * fb` and
`cGain = 1.10 * (1 - (1 - fb)^2)` -- and at matched knob positions TRONIC is the
*louder* of the two above about 0.7. The measurement said nothing about the
module and the real fault was in the input scaling all along.

### Changed: Amortization is twice as loud

The tanks return between 0.23 and 0.50 of what goes in across the FEEDBACK
range. The output gain of 5 made that unity overall -- the input is scaled by
0.2 on the way in -- so a fully wet setting was always quieter than the signal it
replaced, which is not what the hardware does. Doubled.

`Tanks.hpp` no longer includes `<rack.hpp>`; it wanted one `clamp` from it. It
is testable on its own now, and `tests/Amortization` is ten checks against the
tanks and the collider.

### Added: Racketeer takes CV on RES and LAG

Resonance under CV is what turns the delay into a voice -- swept against the
feedback it whistles -- and the optocoupler lag under CV smears the pitch of
whatever it is whistling. Both get an attenuverter over a jack, as the four CV
inputs there already had.

Fitting them is the part worth writing down. A third row in SKIM overruns the
face by 1.2 mm *however wide the panel is made*, because the height is fixed and
the width does not buy any of it back. Nine jacks on one row instead costs four
HP. What worked was adding no new row at all: the three gate jacks that press
the buttons upstairs moved down to the footer band, which is external control
coming in and is what that band carries on every other panel. 17 -> 18 HP.

### Added: the main jacks are struck in gold

A panel with four voice outputs and a mix has one cable you reach for first; a
stereo pair has two. Those are gold now, against the brass of everything else --
`PortInMain`, `PortOutMain` and the two trigger variants. It is the collar that
changes rather than the throat, so the mint in/out band and the lime timing line
both still read: being the important one is a third fact about a port, not a
replacement for the other two.

Colour rather than a ring, because a ring costs 2.2 mm a side and this had to be
affordable on every panel in the family.

Thirty-nine ports across fifteen modules. Nine modules get none, deliberately:
Audit Logic's four equal gates, Tax Bracket's passive ladder where every jack is
an in/out pair, Volatility's six unrelated outputs. A panel where everything is
gold says the same as a panel where nothing is.

### Fixed: Gross had its inputs two thirds of the way along

The footer ran DRIVE, BIAS, WET, TONE, ENV, IN L, IN R, OUT L, OUT R -- so the
audio inputs sat next to the outputs they were about to become, and read as two
more modulation jacks. Signal in at the left edge, signal out at the right, CV
between. The rest of the family was audited for the same fault; Gross was the
only one.

### Added: BURST, and the ratios finally earn the top of their range

The RATIO knobs only meant anything at FILL 0, in grid mode, where the patterns
are switched off entirely. That made most of the range ornamental: a voice at
x64 with no pattern to play against is a drone, and there is no way to modulate
those knobs to make it into anything else.

BURST hands each voice's Euclidean steps to its own ratio. The pattern still
says *when* a voice speaks; the ratio says how fast it repeats while it is
speaking. Multiplying is a ratchet inside the step -- x4 is four even hits, the
first on the beat. Dividing spans steps: /5 ticks once every five and speaks
only when that tick lands on an onset the pattern lit, so two voices on coprime
divisions drift through a figure far longer than sixteen steps. At x1 it is one
hit per onset, which is what the module did before, so the switch changes
nothing it was not asked to.

Multiplication is counted off the step's own phase rather than a free-running
one. A phase of its own fires on its wraps, which fall at 1/N, 2/N ... 1 of the
step -- the first hit late by a sub-division and the last landing on the next
step. Reading floor(phase * N) puts them at 0, 1/N ... (N-1)/N, which is where a
ratchet belongs, and cannot drift or double-count however the samples fall.

The switch sits in the gutter between the ratio knobs and the gate outputs,
which was the one part of that gap the panel was not already using, so it costs
no width.

### Changed: SEED waits for the bar line

A new seed rotates every voice at once, and taking it the instant the knob moved
cut the figure off mid-bar and started another out of phase with it. Kickback
now holds the new seed and swaps at the top of the next bar, before step 0 is
armed, so the first thing heard of the new pattern is its own downbeat. FILL and
HUMAN stay live -- FILL only adds or removes onsets from the pattern already
playing, HUMAN is applied as a voice speaks, and those are the two you want to
hear yourself moving. A stopped module takes a seed at once; there is no bar to
wait for.

### Added: a wire from FILL's zero to the RATIO box

Grid mode was a mode you could only find by turning a knob to its stop and
noticing the module behaved differently, which is a mode nobody finds. A routed
wire now runs from FILL's own minimum mark, down the channel between the two
left-hand columns, to the box around the ratio knobs -- the thing that setting
hands the module to.

### Fixed: AuditLogic's run grouping was left over from a wider layout

`groups=(3, 3, 3, 3)` described four gates of three columns. When the indicator
lights stopped owning columns of their own the row became eight wide, and the
stale grouping put the gutters through the middle of gates rather than between
them. It is `(2, 2, 2, 2)`, and the panel is 26 HP rather than a 25 that had
never reserved the width it was using.

### Added: panelkit places a widget in a gutter, not only in a gap

`between=` already hung a widget in the gap between two columns inside a run.
A boundary *between* runs is also a gap, and a wider one -- but it was a single
figure shared by every boundary on the row, so nothing could be put in one.
Gutters are sized individually now.

Two faults found while using it, both the same mistake in different places: an
interstitial was sized and centred against the *columns* either side of it, and
a column's reach is the widest thing any row puts in it. On Kickback that is a
lit "TOM III" four rows above the switch and a side-label on the gate column --
23 mm of text the switch never has to clear. Sized off its own row it costs
nothing; sized off the columns it cost five HP, and centred off them it landed
on the label.

### Changed: panelkit sits at VCV's pitch, and the whole family got narrower

Measured against Rack's own Fundamental set rather than set by eye. Across all
thirty-three of those modules a row of jacks sits at a 10.81 mm pitch and a row
of knobs at 13.02; this kit could not go below 11.30 and 13.50, and every panel
in the family was paying that on every column.

Two causes, both in the art rather than the solver:

- **The jacks were drawing a size larger than everyone else's.** Rack's PJ301M
  sits on a 23.7 px canvas but draws its collar only to r = 11.10, leaving the
  edge of the canvas empty. Ours filled it to 11.85. On top of that the well
  drew a 0.69 mm recessed seat *outside* the art, so every jack on these panels
  wore a ring nobody else in the rack wears -- about thirty per cent wider than
  the same jack on the module next to it.
- **The seats were generous everywhere else too.** Trimmed to sit just inside
  VCV's floor: 10.48 mm minimum jack pitch, 12.90 knob.

The family went from 449 HP to 429. Nothing lost; several panels gained room.

A second fault fell out of it: made narrow enough, TaxBracket's masthead brand
and its form stub collided. `required_hp` checked that a panel was wide enough
for its *title* but not for the line under it, so the failure arrived later, in
the linter, looking like a spec error rather than a width one. It is a width
constraint now.

### Added: labels beside, and labels that name a run

Two ways to stop a label costing a row a line of text, which is the thing that
decides how much a 3U face holds.

- **`side="left"` / `side="right"`** puts a label alongside its widget instead
  of over or under it, on the widget's own centre line, costing the row no
  height at all.
- **`Row.span`** names a run of columns once -- `span=[(KICK, TOM3, "DECAY")]`
  -- instead of printing the same word over each of six identical trims. Unlike
  `Row.shared` it leaves the rest of the row to carry its own labels, which
  matters when the row is six of one thing and two of another. It also draws a
  box round the run, so the grouping is visible rather than inferred from a word
  sitting between two of the controls -- except on a *paired* row, where the
  label names the row below it as well and a box round only the upper one would
  say the opposite of what the pair idiom is for.

Kickback and SixFigures use both; Kickback dropped from 68 printed labels to 53
and SixFigures from 41 to 31. SixFigures names its CV and AUX rows once but not
its RATE row, because each RATE label carries that voice's LED -- a label with
an indicator on it is holding something up as well as saying something.

### Added: jacks say what they carry, not only which way they go

Four port arts on two axes. The wide throat band is the direction, brass in and
mint out, as before. A thin lime line inside the throat marks a port that deals
in *timing* -- a clock, a trigger, a gate, a reset -- as against one that deals
in levels: audio, CV, a pitch. `panel::PortTrigIn` and `panel::PortTrigOut`.

### Changed: Kickback's gates are six jacks, not one polyphonic bus

A drum machine that needs a split module to drive six external voices is not
self-contained, and polyphony belongs where the channels are notes of one voice
-- a quantizer, a V/oct bus -- not where they are six separate drums. The gate
column is six individual outputs, each held for GATE and scaled by that hit's
velocity, with their labels standing beside them: that is what made them fit
where the bus used to be.

### Added: tooling

- `make vcv-<Module>` renders one panel through Rack in about thirty seconds.
  `make vcv-preview` still does all twenty-four, and takes minutes.
- `tools/sync_hp.py` reports every HP figure in README.md and docs/ that
  disagrees with the generated headers, and `--write` fixes them. A panel's
  width is solved, so one panelkit change moved fourteen modules at once -- and
  every one of those numbers was also typed into prose, where nothing checked
  it.

### Added: Toll -- the bell that was pretending to be a snare

12 HP, Form 2290, monophonic. BaSnaHi's snare stage read as a struck metal pipe
rather than as a snare, which is a fine thing to be and a bad snare, so it left
Kickback and got a panel with controls for what it actually is.

Sixteen modal partials over one of four objects, chosen by **SET**:

- **MEMBRANE** -- the Bessel-zero ratios of a circular head.
- **BAR** -- free-free flexural modes, 1, 2.756, 5.404, 8.933 ..., spreading as
  the squares of the roots of the beam equation. A metallophone.
- **BELL** -- a tuned bell's own named partials: hum, prime, tierce, quint,
  nominal, deciem, undeciem, duodeciem. The tierce at 2.4 is the minor third
  that makes a bell sound like a bell rather than like a pipe.
- **HARMONIC** -- not an object at all, and the reason to have a mode bank on a
  panel rather than buried inside a drum.

**SPREAD** then bends whichever set is chosen away from itself: partial n moves
to n^(1+s). **STRIKE** is where it is hit -- on a membrane the Bessel weighting,
on a bar or bell the node rule every struck object obeys. **HARD** is the mallet,
three milliseconds of felt to a third of one of wood. **DAMP** is how much
faster the upper partials die than the fundamental, which is most of what
separates bronze from lead. **BUZZ** is the loose layer against the body, and
**CHOKE** is the hand on it -- damping rather than muting, because a damped
object still rings briefly and a gated one conspicuously does not.

The structural DSP moved to `src/Drum.hpp`, shared with Kickback: a struck
membrane and a struck bar want the same contact pulse, the same click, the same
attack layer and the same tension term, and only the mode ratios differ.

### Changed: Kickback loses the bell and gains a grid

Six voices now. The bell left for Toll, and its column became what the rest of
the rack sees: a polyphonic **GATES** bus with one velocity-scaled channel per
voice, a **VEL** output carrying the loudest velocity sounding, and the kick's
gate on its own. The pattern engine can now play things that are not in this
module.

**Grid mode**, at FILL's bottom stop. The pattern engine switches off, every
voice hits on every step, and each voice's own **RATIO** knob multiplies or
divides that -- thirty-nine detents from /256 to x256, symmetric about x1, in
steps of 2, 3, 5 and 7. The odd factors are the point: powers of two never
produce anything the Euclidean engine could not, while a voice on /5 against one
on /7 takes thirty-five steps to come round and one on /256 is a hit every
sixteen bars. Each voice free-runs its own phase rather than counting steps, so
those long patterns really drift; RST puts them back in line. x256 on a
sixteenth grid at 120 BPM is two kilohertz, which is where dividing a clock
stops being rhythm, and the engine clamps at a quarter of the sample rate.

It replaced the V/OCT row. Every voice still takes 1 V/oct internally, but
Kickback is a rhythm instrument and the pitched voice that wanted a keyboard is
Toll.

### Fixed, from playing it

- **The model selectors are toggles.** They were detented trims, which are
  smaller and fit better and are the wrong control: you cannot see where a trim
  is standing and you cannot flick it. Paid for by shrinking the TUNE row's
  seats -- RATE keeps the lime primary ring, so the panel still says which knob
  to reach for.
- **The pattern engine never played the toms.** Their roles started at FILL
  0.44, 0.54 and 0.64, and the module ships at 0.45, so all three computed to
  zero onsets at the default -- a voice silent at the default is a voice the
  module does not appear to have. All six now speak by the middle of the knob.
- **A tom's STRIKE at zero is silent now.** That end of the knob is a drum hit
  dead centre with a soft mallet, and a beater still audible there is the
  control failing at the one thing its bottom is for. The click comes back
  within a tenth of a turn rather than fading in over the first third.
- **MIX was ten decibels under a single voice's OUT.** Six drum voices rarely
  peak together, so summing them and dividing by six is the sum for six sine
  waves in phase, not for a kit. A full kit now lands within a couple of
  decibels of its loudest single voice.

### Changed: Kickback's roster, on listening

Seven voices now, not nine, and two of them are not the circuit their name
suggests -- which is the point.

- **The snare is three circuits under one selector.** BaSnaHi's snare stage, a
  modal shell with wires against it, read as a struck metal pipe rather than as
  a snare. So it moved to **BELL**, where being a struck metal pipe is exactly
  what is wanted and its amplitude-dependent collision layer is a feature: a
  soft strike rings clean, a hard one clatters. The SNARE column took the three
  circuits that *do* sound like snares -- the XOR bell's clangy crack, the
  vactrol noise voice's wash, and Karplus-Strong's rattle at the long end of its
  delay, which the 1983 paper itself calls "the effect of a snare drum".
- **The Tiny Dazzler is no longer a voice of its own.** Its two halves went
  where they belong: the long end of its delay line is the snare's DAZZLE mode,
  the short end -- "a brushed tom-tom", in the paper's words -- is the top of
  the hat's RATTLE.
- **The hat's RATTLE is a three-way sweep**, metal to noise to Dazzler, rather
  than a switch. The hat had no control to spare for a mode selector and RATTLE
  was already the texture control; the upside is that both boundaries are
  playable rather than stepped.
- **ACCENT opens the hat.** There is one hi-hat, so how hard it is struck is
  what decides whether it reads closed or open, as a pedal would: velocity
  multiplies the ring time better than three to one.
- **The three toms are three drums, not one transposed.** They were identical
  circuits at different default tunings, which is the wrong reading of a
  schematic whose three branches are captioned "change these 3 resistors as
  you'd like your sound." Each now has its own TUNE range -- 42-150, 80-290 and
  150-520 Hz, overlapping by about a fourth at each join so a kit tunes across
  them -- its own ring time, its own upper-mode damping, and its own beater
  hardness. The damping is most of it: a floor tom holds its overtones and a
  small rack tom is all fundamental and gone.

The panel is 34 HP, down from 41: nine columns, the payroll strip left of the
gutter and seven voices right of it. KICK's MODEL and SNARE's MODE are detented
trims rather than toggles, because a toggle is a millimetre and a half taller
than a trim seat and that row had neither to give.

### Fixed: a tom at full BEND never stopped ringing

A single strike was still at three and a half volts six seconds later, at any
DECAY past about 0.9. The tension term followed the instantaneous peak of the
mode bank with an instant attack, so it re-peaked on every cycle of the
waveform and modulated the resonator's frequency at the resonator's own
frequency -- which is a parametric amplifier, pumping energy in faster than the
decay took it out.

Berger's term is the *mean square* displacement, an average by definition, so
following the peak was wrong physics as well as unstable. Two poles of
mean-square smoothing at 8 Hz put the ripple 46 dB under the average. Every
voice that uses tension had it, not only the toms -- KICK and BELL both
self-oscillated at their own corners of the knob grid -- and `tests/Kickback`
now renders every voice at every setting and fails anything still audible six
seconds after one strike.

### Fixed: the wire bed's level followed the sample rate

Two normalisations, and the difference between them is the trap. `sin(w)` makes
a resonator *struck by an impulse* ring to a rate-independent height, which is
what the mode banks get from the strike pulse. The wires are not struck, they
are leaned on -- the collision force pushes for as long as the head is past
them -- so `sin(w)` alone left the bed proportional to the sample rate, 2.7x
over 44.1 to 192 kHz, while the full continuous-drive form `(1-r)*sin(w)`
overshot 1.8x the other way.

Neither is right, because a contact is a train of bursts of fixed duration in
seconds: between an impulse and a steady tone. The exponent was swept and
measured instead, and holds the bed flat to 1.15x. That is documented as an
empirical constant rather than dressed up as a derivation.

### Changed: Kickback rebuilt -- nine voices, a clock, and drums that sound struck

The complaint was the right one: the kick and the toms "sounded like bass
oscillators" with no hit on the front of them. They did, and the reason is in
the literature. A drum was a single two-pole resonator shocked with a one-sample
impulse -- and a single mode, with no attack layer and no pitch glide, *is* a
plucked bass note. Nothing about that could be fixed with a knob.

Every voice now sits on a shared physical vocabulary (`src/Kickback/Drum.hpp`):

- **A contact pulse, not an impulse.** Bilbao's raised cosine of duration T0
  (JASA 131(1), 2012), three milliseconds of felt to a third of a millisecond of
  wood, shortening further with velocity. Finite contact time is a lowpass on
  the excitation, so hitting harder gets brighter rather than only louder.
- **The click, summed in directly.** A mode bank is narrow resonators well under
  a kilohertz, so it filters the stick out of its own strike. The derivative of
  the contact pulse is one cycle of a sine at 1/T0 -- a felt beater thumps at
  330 Hz, a hard stick cracks at 2.9 kHz -- and it goes to the output on its own
  path, which is the transient component Shier et al. (Forum Acusticum 2023)
  argue has to be modelled rather than hoped for.
- **A stochastic attack layer**, standing in for the dense 1-8 kHz partials
  Kirby & Sandler (DAFx-20) find in a real tom's onset. Their listening test
  could not tell a modelled fundamental from a real one, and caught the loss of
  everything above 2 kHz 98.4% of the time.
- **Eight modes at the Bessel-zero ratios**, with strike position deciding which
  ones sound: dead centre wakes only the circular modes and is one boomy
  partial; toward the rim the radial modes come in and it goes hollow.
- **Pitch glide driven by the drum's own energy**, so a hard hit dives and a
  ghost note does not, rather than by a fixed envelope.

Measured through a 24 dB/oct band at 2 kHz, a default kick now has 165 times
more energy at the strike than a fifth of a second later. It had 1.1 times.

What else changed, and why:

- **KICK and SMURF are one voice with a MODEL switch.** A ringing filter and a
  starved oscillator both make a bass drum; which one a patch wants is a switch,
  not two columns of panel.
- **Three TOMs, all at once.** TomTomTom is three separate twin-T branches, and
  folding them into one voice with a range switch was the wrong fold -- a kit
  needs three toms sounding together, not one tom that can be moved.
- **Every voice has a V/OCT input.** Bell needed one most (an XOR bell played
  chromatically is a cowbell line) but the row is uniform, so a keyboard
  transposes the whole kit -- including the noise voices, where 1 V/oct moves a
  filter corner.
- **DAZZLER is Karplus-Strong.** The Tiny Dazzler's backwards-wired transistor
  read through the 1983 drum recurrence. Its sheet's own Snare/HiHat switch
  becomes MODE, picking which end of the delay length TUNE sweeps -- the paper
  itself calls large p a snare and small p a brushed tom-tom. BEND is the
  stretch factor S, and takes the blend factor b out toward the "plucked bottle"
  with it.
- **SNARE has wires.** Bilbao's one-sided power-law collision, so ghost notes
  stay pitched and hard hits sizzle, instead of a fixed noise crossfade that
  rattled identically at every velocity.
- **HAT has metal in it**, three multiplied squares against the noise tap, on
  two envelopes.
- **Every voice keeps its level across its tuning range.** A two-pole resonator
  rings at 1/sin(w), so the mode bank used to be eighteen decibels louder at the
  bottom of a sweep than the top: TUNE was a volume control with a pitch
  side-effect.
- **The nine voices are level-matched**, kick and snare loudest, hats and the
  colour voices below them, the way a kit sits.

### Fixed: Kickback sounded different at different sample rates

Three faults of the same shape, none of them audible at the rate they were
written at, all found by rendering every voice at 44.1, 48, 88.2, 96 and
192 kHz and comparing.

- A pole radius written as a constant is a decay per *sample*: the snare's wire
  rattle ran four times shorter at 192 kHz than at 44.1.
- A resonator struck by an impulse rings at 1/sin(w), so a drive written as a
  constant made the same wires seventeen decibels louder there. The mode banks
  had this too, in the same place their level used to follow their tuning.
- A PRNG has constant variance per sample, so its power spectral *density*
  halves each time the rate doubles. Where the noise ends in a filter with a
  corner in hertz -- the noise voice's vactrol, the Dazzler's snare-side
  lowpass -- that cost five decibels at 192 kHz. Where it ends in a highpass,
  whose band grows with the rate, the plain stream was already right and
  correcting it made things worse; both were measured rather than reasoned
  about, and the correction is applied only where it helps.

Every voice now holds within 1.2 dB from 44.1 kHz to 192 kHz; the noise voice
was 4.9 dB. `tests/Kickback` renders all nine at five rates and bounds both the
full-band level and the high band after the strike -- the second because the
first cannot see it: the snare's wires being seven times hot moved its total by
less than a decibel, since the shell buries them.

### Added: Kickback's payroll -- an internal clock and Euclidean patterns

Kickback plays on its own now. A voice whose TRIG jack is empty is played by the
pattern engine; a voice with something patched into its TRIG is played by that
and nothing else, so an unpatched module runs a kit and patching one jack takes
one voice over. Nothing has to be switched to move between them.

RUN, RATE (30-300 BPM), DIV (six subdivisions), SWING, and a CLK input that
measures the external period and subdivides it -- so a quarter-note clock still
drives a sixteenth-note grid -- plus RST in and a CLK output that makes Kickback
the rack's clock.

The patterns are Bjorklund's E(k,16). FILL sets every voice's k at once and is
monotone, so turning it up only ever adds onsets; SEED rotates each voice's
necklace by a different amount. Each voice has a role -- the FILL at which it
enters and the rotation that puts its default where a player would, E(4,16) for
four on the floor, E(2,16) turned by four for the backbeat -- so FILL fills a
kit out in the order a kit fills out rather than spreading nine identical
polyrhythms. HUMAN is velocity spread and microtiming together, with the
downbeats moving least.

The panel is 41 HP: eleven columns, the payroll strip at the left of a gutter
and one column per voice to the right of it, every column reading top to bottom
the same way.

### Added: Reconciliation -- a just-intonation quantizer

21 HP, Schedule M-1, polyphonic. Most quantizers answer one question and the
only interesting thing about them is which scale was loaded. This one splits it
in two and puts a knob on each.

Which pitches exist (BASIS): Partch's eleven-limit tonality diamond, his
43-tone scale, one Otonality or Utonality hexad out of that diamond, Erv
Wilson's hexany and eikosany, or the raw harmonic series -- transposed onto any
of Partch's six identities by NEXUS, mirrored by UTONAL, and pruned by prime
limit.

How one gets chosen (RECONCILE): five published measures of what "simpler"
means, which disagree with each other -- Euler's gradus suavitatis (1739),
Tenney's harmonic distance, Barlow's harmonicity, Sethares' sensory dissonance
against an assumed timbre, and adaptive tuning from the last note rather than
from the root -- plus plain NEAREST to compare them against. WINDOW bounds how
far a rule may reach; BIAS is how hard it pulls once it gets there, and at zero
every rule collapses to NEAREST.

* The read-out names the ratio -- 11/8, not "a bit flat of a tritone" -- with
  its cents and its distance from 12-TET.
* ADAPTIVE tunes every interval pure from where the last one landed, so the
  tonal centre walks; DRIFT is that comma as a voltage and RESET puts it back.
* PURITY reports how consonant the chosen ratio is, as CV.
* HYST is a dead band given as a fraction of the local step rather than in
  cents, because these sets are wildly uneven and any absolute setting wide
  enough to steady the 43 would make a hexad unplayable.

tests/Reconciliation checks the tables against the properties their authors
stated rather than against a copy of the same list: no step in the 43 smaller
than 121/120 or larger than 45/44, 29 distinct pitches in the diamond and every
inversion present, the 43 containing it and adding exactly fourteen, and -- for
Sethares -- every minimum of the dissonance curve landing on a diamond ratio
within two cents, with the count of minima following the assumed timbre (0 for
a sine, 3 at four partials, 5 at seven, 9 at twelve).


### Added: Collusion — six LFOs that listen to each other

21 HP, Form 211. Six phase oscillators as a *population* rather than six
independent modulators: each has its own natural rate, and COUPLING says how
hard each is pulled toward the others. The threshold at which they stop
drifting and lock is Kuramoto's, and it depends only on how far apart the
natural rates are — so COUPLING and SPREAD are a phase diagram rather than two
gain controls, and ORDER puts the order parameter itself out as CV.

* Four SCHEMEs: all-to-all (Kuramoto), a ring (travelling waves), a one-way
  cascade whose head is a clean LFO, and Mirollo–Strogatz pulse coupling.
* EVASION is the Sakaguchi phase lag, which buys clusters and partial order
  instead of all-or-nothing.
* SHAPE warps the cycle from a sine toward a relaxation spike, with a ceiling
  that falls with frequency so sweeping into the audio band grits rather than
  aliases.
* A Benjolin/Turing Machine rungler whose written bit comes from the whole
  population's mean field, so the register fills with noise below the
  transition and a repeating figure above it. LEVERAGE feeds it back into the
  rates, per filer rather than common mode, which is what stops a locked swarm
  staying locked.
* `tests/Collusion` measures the phase transition itself, not just finiteness:
  the order parameter has to sit near 1/sqrt(N) uncoupled and above 0.9 locked,
  a wide fan must fail to lock where a narrow one succeeds, and the cascade's
  head must not move at all.


### Fixed: Consolidation crashed Rack the moment it was added

`configBypass` was called once per mixer channel with the same `OUT` as the
destination. Rack allows each output to be bypass-routed exactly once and
asserts on the second (`Rack-SDK/include/engine/Module.hpp:240`), so adding the
module aborted the process — every time, on every platform. The mix output now
carries channel 1 through on bypass, and the two multiples fan their input out
to their own legs, which the one-route-per-output rule permits.

### Fixed: Gross output a constant 12 V from the moment it was created

An unpatched Gross sat at 12 V on both audio outputs with ENV pinned at 10 V,
and poisoned anything downstream of it.

Rack default-constructs a `BiquadFilter` by calling
`setParameters(LOWPASS, f=0, Q=0, V=1)`, and that branch computes
`1/(1 + K/Q + K*K)` — with `K = tan(0) = 0` and `Q = 0` that is `0/0`, so every
coefficient of a freshly constructed biquad is NaN
(`Rack-SDK/include/dsp/filter.hpp:307,335`). Gross designs its six filters in
`updateControls()`, which runs behind a `ClockDivider` of 8 and so does not fire
until the eighth sample. The seven samples before it were enough: NaN entered
the filters' state history and never left, and `clamp()` returns its upper bound
for NaN — hence exactly 12 V, forever, whatever was patched in.

The controls are now designed once before the first sample goes through them.

### Fixed: quitting Rack with a Repossession in the patch could segfault

Rack deletes the window before the scene (`Rack/src/context.cpp:19` and `:27`),
so `VideoScreen`'s destructor ran with an already-freed `NVGcontext` and called
`nvgDeleteImage` on it. The image is now released only while the window that
owns the context is still alive.

### Fixed: Consolidation started silent

The four channel levels defaulted to 0, so a fully patched mixer made no sound
and looked broken. Rack's convention is the opposite — every Fundamental level
defaults to unity — and it also matches the topology being modelled: 10k in
against 10k feedback is unity gain per channel. They now default to 100%.

### Faster: the per-sample audio paths no longer recompute what has not changed

Filter and envelope coefficients are expensive functions — `exp`, `tan`, `pow`,
`cos` — of things that barely move: a knob, or the sample rate. Written inline
in `process()` that cost was paid on every sample. `src/DspCache.hpp` adds a
one-float staleness check, so a transcendental call becomes a float compare
until its input actually moves, and an exact recompute the moment it does — a
knob sweep sounds precisely as it did before.

Kickback, which ran eight voices through several of these each sample, is the
clearest case: **0.875% of one core down to 0.477% at 96 kHz, 1.83x faster**,
with its output identical to four significant figures. SixFigures was
evaluating three `exp()` per voice per sample — eighteen per sample — for two
values that depend only on the sample rate. Racketeer had five `pow()` per
sample keyed on knobs; Deduction a `tan()` per polyphonic channel; Dividend and
Installment used `pow(2, x)` where `exp2` does. Gross, Diversified and
Amortization already updated at control rate behind a divider and are unchanged.

### New module: Schedule A, the Repossession expander

16 HP, eight rows, one per seized asset: discrete **SPEED / GAIN / START /
LENGTH** inputs and a per-step audio **OUT**, for the four controls Repossession
otherwise carries polyphonically. Attaches to Repossession's right.

- **A jack here wins over the host's poly jack only where a cable is in it.** A
  poly LFO can drive all eight steps while one hand-patched envelope takes over
  step 5 and nothing else. Summing them would make every unpatched jack a silent
  zero, which is why the message carries a `has` flag per step and not just a
  voltage.
- **The rows are named by colour, not by number.** Each row's light is sent the
  colour the host's own step button is wearing, disabled steps included, so the
  expander needs to know neither the palette nor the state.
- **No state, no menu, no `dataToJson`.** The patch cables are the whole
  configuration. A lone expander goes dark and silent rather than holding what
  it last saw.

### panelkit: a panel with no footer band now fills its face

`slack` — the room the justify pass shares out among the gaps — was only ever
computed inside the `if panel.footer:` branch. A panel without a footer got
`slack = 0`, never justified, and packed its rows against the masthead with the
whole lower face left empty. Schedule A's eight rows of jacks stopped
three-quarters of the way down the panel.

Footerless panels now measure their slack against the foot ribbon between the
bottom screws, like every other panel measures it against its band. Only two
panels in the family have no footer; PatchAudit had no slack to share, so its
artwork is byte-identical, and the other nineteen never entered this branch.

### Repossession: only the windows are in memory

The module held the whole decoded clip in RAM — 230 MB for ten minutes of stereo
float at 48 kHz, which is why the import length was a menu item rather than a
number. Almost none of it was ever played. The module plays eight windows cut out
of the clip; everything between them was memory spent on audio nobody asked for.

The decode still lands on disk as a `.pcm`. Only the windows are read into RAM,
and what is kept for the whole clip is its length, its rate and a
thousand-bucket waveform — about four kilobytes, whatever the source.

- **`Windows.hpp`.** A worker reads windows off the `.pcm` and hands each to the
  audio thread as an atomic pointer, the same handover `Media.hpp` uses. Newest
  request per slot wins, because dragging an edge queues one a frame and only
  the last matters. A step whose window has not arrived is silent rather than
  stalling.
- **`Media` no longer holds audio.** `scanPcm` reads the file once, forwards, in
  a fixed half-megabyte buffer, folding every frame into its peak bucket.
- **A budget, and a meter that reads it off the buffers.** 16 MB to 512 MB,
  default 64. The meter shows one segment per step in that step's own colour, so
  the bar says who is holding what.
- **The eight steps share the budget.** Trimming a step returns seconds to a
  common pool; growing one takes from it and simply stops when it is empty, the
  placed start staying put while the length gives. Disabling hands a whole share
  back. Re-arming takes up to an even share *of what is left* — and when nothing
  is left the step stays disabled and blinks. That refusal is the mechanic.
- **"Eight equal spans" follows the budget, not the clip.** Eight windows of the
  largest length the budget allows, spread evenly end to end. The old behaviour
  made window length a function of clip length, so a ten-minute video produced
  eight seventy-five-second regions whether or not there was memory for them.
- **The crossfade names the buffer it is fading out of.** A seam between two
  steps now reads two different buffers, so the old one is held past the longest
  fade rather than freed four samples later.
- **`tests/Repossession/test_windows.cpp`** — 63 checks under ASan/UBSan: the
  allocator's rules including the refusal, conservation of the pool across a
  20 000-operation random walk, windows read to the exact sample, reads past the
  end of file, a missing file, newest-wins under a 200-request burst, and a
  clean join with every slot still queued.

### Repossession: an instrument, not only a sequencer

The module could be clocked, and that was all it could be. Everything here is
additive — the clock input, the region editing and the whole existing patch
format still behave exactly as they did.

- **An internal clock.** TEMPO, 30–300 BPM. Patching CLOCK silences it rather
  than racing it, and pulling the cable hands the knob back; the light beside
  CLOCK follows whichever clock is actually in charge.
- **RUN is a three-position switch: RUN / STOP / LATCH.** Latching one step used
  to be reachable only by accident — patch a clock, stop it, and whichever step
  you landed on looped forever. It is a position now, and the selected step is
  the one it loops.
- **The step buttons are playable.** With the transport stopped, a tap fires a
  step; holding one past 250 ms loops it for as long as it is held, overriding
  the region's own LOOP and handing it back on release. Nothing that is saved
  changes.
- **Steps can be disabled.** Ctrl-click (cmd on a Mac) parks a step: it keeps
  its span, speed and gain, and the sequencer passes over it as if it were
  empty. Distinct from releasing a slot, which throws the region away.
- **Every step has its own colour**, a lime-to-mint ramp across the eight, drawn
  from one function so a step's button, its span on the timeline and its line in
  the report cannot disagree. Disabled steps go to clay *and* are hatched, since
  colour alone is the one channel a reader may not have.
- **The timeline zooms.** Scroll about the pointer, shift-scroll to pan, with a
  bar showing where the view sits in the clip. Rack's own zoom enlarges the
  whole panel and runs out long before a boundary can be placed accurately on a
  three-minute clip. The "dragged to nothing" threshold follows the zoom, so a
  span trimmed at 100× is not deleted for being small on screen.
- **Four per-step CV inputs — SPEED, GAIN, START, LENGTH.** Polyphonic, channel
  N addressing slot N, so one cable carries all eight; a monophonic cable
  applies to every step at once. START and LENGTH never write to the stored
  region, so a modulated window springs back when the cable is pulled.
- **A polyphonic STEPS output**, whichever step is sounding on its own channel.
- **`panelkit` gains `RgbLight`**, a light whose colour the module picks rather
  than the panel — the general capability the per-step colours needed, so it
  lives in the kit and every panel has it.

### Repossession: a tool that cannot start says so

`yt-dlp failed (exit 127)` was, in at least one real case, a lie. The launcher
forked, `execvp()` failed, and the child called `_exit(127)` — which is exactly
what a shell reports for a command it could not find, so the parent had no way
to tell "the tool ran and exited 127" from "the tool never ran at all". The
panel then quoted an exit code the tool had never chosen, next to the path it
had just successfully found on disk.

The case that produced it: a `yt-dlp` on `PATH` that was a `pip` console script
whose `#!` line named a Homebrew Python since removed. The file is present and
executable, so `which()` resolves it and the log prints its path; `execvp()`
then reports `ENOENT` about the *interpreter*, not the script. Being told that
path is "not found" sends you looking in the wrong place.

- **An exec-status pipe.** Its write end is `FD_CLOEXEC`, so a successful
  `execvp()` closes it and the parent reads EOF, while a failed one leaves the
  child alive just long enough to write its `errno` down it. A launch failure
  now leaves `started` false and fills `ProcessResult::execError`; the child is
  reaped rather than left as a zombie.
- **The message names the likely cause.** `ENOENT` on a file that is provably
  there and executable is reported as a broken `#!` wrapper, not as a missing
  file. `ENOEXEC` and `EACCES` get their own wording.
- **A program is still allowed to exit 127.** A tool that genuinely returns 127
  is reported as having run, unchanged — the distinction is the point.
- **`tests/Repossession/`.** The launcher tested against a host compiler through
  a five-call Rack stub: success, separate streams, non-zero exit, a genuine
  127, a missing file, a stale `#!` shim, a file without the executable bit,
  `execvp`'s documented `/bin/sh` fallback, and no zombies after fifty failures.

### Panels: every layout re-solved

Every panel in the family had some version of the same fault, and it was one
fault: the layout solver derived every vertical coordinate and no horizontal one.
Rows were given evenly spaced *centres* — `P.cols(n, margin)`, with the margin
typed by hand — which is the wrong quantity to hold constant, because a centre
says nothing about how much of the panel a widget actually covers. One margin
had to serve a big knob and a switch alike, so the knob hung over its own block
frame while the switches floated in dead air. Dividend's FREQ knob through the
left edge of PAYOUT was the clearest case; there were nineteen others.

- **The horizontal solver.** A section's rows share a column grid, solved from
  extents rather than counts: a column is as wide as the widest thing any row
  puts in it — well, ring, primary seal, or label — measured on each side
  separately, since a lit label's light hangs off one end only. Comparable
  columns are spaced evenly as a *run*; where a row changes gear the run breaks
  and the slack collects in a gutter between the runs.
- **`hp` defaults to `"auto"`.** The panel comes out exactly as wide as its rows
  need. Uncertainty Policy 16 → 12 HP, Diversified 18 → 15, Dividend 16 → 15;
  the panels that were quietly a millimetre short of their own contents grew.
- **Paired controls read as pairs.** A trimpot over its own jack now sets the
  label they share in the gap *between* them, equidistant from each, instead of
  above the trimpot where it could be read as naming the row above. Pairing is
  per control, so a jack sharing that row without owning anything below keeps its
  name over its head, where a cable cannot cover it.
- **Stepped knobs look stepped.** A knob that is really a selector carries a
  detent for every position and an arc through its real travel, engraved into the
  dark of its own well — so it costs the layout nothing.
- **Real minimum clearances, and a linter that knows them.** Label to well, label
  to label, ink to block frame: each has a floor that holds at both densities.
  The linter now measures gaps rather than only overlaps, and counts the primary
  ring as the ink it is — the check it was missing is exactly the one that let
  FREQ through its own frame.
- **The read-out well's geometry is the spec's.** `panel::GLASS_X/Y/W/H` are
  emitted into each `Panel.hpp`; nine modules had been positioning their display
  by re-typing the same millimetres in C++.
- **`make vcv-preview` builds after regenerating, not before.** It used to build
  from whatever headers were on disk and only then re-run the specs, so a spec
  change reached the artwork and not the widget positions — a panel whose jacks
  sat beside their own wells, for as long as it took to notice.

## 2.1.0

Seventeen new modules, and a new face for all of them.

### The look

The family is now a banknote rather than a green form: the five-colour "High Contrast" palette, a pale engraved face with dark masthead and footer bands, sage guilloche ribbons, a hairline frame with corner scrolls on every section, wells ringed like seals, a double ring on the primary control, and traces engraved as waves. Nothing in any panel spec changed; the solver tags each label's ground and the emitter resolves its ink.

### Modules

- **Dividend** (16 HP, Oscillator, Synth voice) — A pulsar-synthesis VCO after Curtis Roads: trains of pulsarets whose formant is set independently of the fundamental, six pulsaret waveforms, six windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.
- **Tax Bracket** (12 HP, Utility, Mixer, Attenuator) — The Olegtron R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.
- **Racketeer** (16 HP, Noise, Delay, Synth voice) — Wolfgang Spahn's PB701 Electric Intonarumori: a PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.
- **Gross** (18 HP, Distortion, Waveshaper, Effect) — A Wiener–Hammerstein distortion built from the Eichas–Zölzer piecewise-tanh mapping and the dynamic bias of Comunità, Steinmetz and Reiss: input EQ, drive, static and dynamic bias, knees and slopes per polarity, five curve families, output EQ, device presets, and a live transfer-curve read-out.
- **Amortization** (14 HP, Reverb, Effect) — A Verbtronic-style reverb: a Dattorro plate for VERB and an eight-line FDN with a limiter for TRONIC, tonal tilt inside the loop, feedback past unity, predelay, freeze, the mode gate, and wet-only outputs beside the mix.
- **Repossession** (34 HP, Sampler, Sequencer, Visual) — Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs.
- **Six Figures** (24 HP, Oscillator) — Modular in a Week, Day 1, folded into one bank: six voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix.
- **Garnishment** (10 HP, VCA, Low-pass gate, Dual) — Day 2: two VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount.
- **Consolidation** (12 HP, Mixer, Multiple, Utility) — Day 3: the ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.
- **Installment** (16 HP, Envelope generator, LFO, Function generator, Dual) — Days 4 and 5: two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the Day 12 tape-motor PWM driver with duty CV.
- **Volatility** (14 HP, Noise, Sample and hold, Random) — Day 6: an 18-bit 4006-style shift-register noise source, the YASH sample and hold, and the PHObos random gate, on one shared clock.
- **Deduction** (10 HP, Filter, Distortion) — Day 7: six filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.
- **Audit Logic** (22 HP, Logic, Switch, Clock modulator) — Day 8 and the 4066: four logic gates with selectable functions and the 0 V / 12 V reference, two gated switches, and the Emiz CV2 clock divider.
- **Kickback** (30 HP, Drum, Synth voice) — Day 9: eight drum voices with a mix — BaSnaHi kick, snare and hat, SmurfDrum, TomTomTom, XORbell, the percussive noise voice and the Tiny Dazzler.
- **Payment Schedule** (28 HP, Sequencer, Switch, Quantizer) — Day 10: the Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.
- **Sign Here** (20 HP, Controller, Utility) — Day 11: the button and pedal, the offset-scaler joystick as an XY pad, and four touch pads.
- **Diversified** (18 HP, Effect, Delay, Reverb, Chorus, Distortion) — Days 12 and 13: a stereo multi-effect with 106 programs — the DSP99 board's categories as twenty algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

### Under the hood

- `panelkit/palette.py` derives every colour from five anchors; `palette.ink(role, ground)` is the one place a label's hex is chosen.
- The layout solver measures every clearance from a widget's well and ring,
  not its art; rows carrying lit labels make room for the light; sparse
  panels are justified to the footer. The linter now catches wells that
  overlap, wells straddling a block frame, and anything entering the foot
  ribbon. Retroactive grew to 14 HP in the process.
- `panelkit/render.py` draws the ornament as polyline paths, which is what Rack's nanosvg keeps.
- Repossession spawns `yt-dlp` and `ffmpeg` through a small portable process wrapper; nothing new is linked, so the VCV Library rules still hold.

## 2.0.0

First release of Moon Technologies as a single plugin.

PatchAudit, Retroactive and Uncertainty Policy previously existed as three
separate Rack plugins sharing a brand. They are now one plugin with three
modules, which is how the VCV Library expects a brand to ship: one entry, one
install, one version.

**If you have patches built with the old separate plugins**, the module slugs are
unchanged but the plugin slug is not, so Rack will not resolve them
automatically — it shows a placeholder where each module was and keeps the rest
of the patch intact. Re-place the module and reconnect its cables.

### Modules

- **PatchAudit** (26 HP, Utility/Visual) — browse Patchstorage from inside Rack,
  audit patches against your installed modules before opening them, and import
  straight into your rack or save to disk.
- **Retroactive** (14 HP, Effect/Delay/Granular) — windowed sample permutation
  with eight modes, clock sync, subdivision, crossfade and freeze.
- **Uncertainty Policy** (16 HP, Utility/Random) — signal-aware knob and cable
  randomizer that auditions each roll and reverts the ones that kill the sound.

### Under the hood

- Windows builds now work. PatchAudit's optional libcurl fast path looked its
  symbols up through `dlsym(RTLD_DEFAULT, …)`, which does not exist on Windows;
  it now goes through a small shim that uses `GetProcAddress` over the loaded
  modules there. Where the lookup fails on any platform the audit downloads
  large zipped uploads instead of streaming them, exactly as before.
- Panels are generated into two headers rather than one: a shared
  `src/PanelTheme.hpp` holding the vocabulary every panel draws through, and a
  per-module `src/<Module>/Panel.hpp` holding that panel's own numbers. Three
  panels can now be linked into one plugin without their silkscreen tables
  colliding.
- Prebuilt binaries for `win-x64`, `mac-arm64`, `mac-x64` and `lin-x64`, built
  by CI on every push against the official Rack SDK.
