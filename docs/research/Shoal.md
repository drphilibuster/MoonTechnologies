# Shoal (Ormer Modular): research notes for Ledger

Sources, read in full on 2026-10-02:
- VCV manual v2.0.6, <https://ormermodular.github.io/shoal-vcv/>
- Hardware (disting NT) manual v1.2.0, <https://ormermodular.github.io/shoal/>
- Hardware source, MIT licensed: <https://github.com/ormermodular/shoal> (`src/shoal.cpp`, README, release notes v1.0.0–v1.2.1)
- VCV Library listing: <https://library.vcvrack.com/OrmerModular/Shoal> (v2.0.4, US$20; tags Sequencer, Polyphonic, Random)

The VCV source is closed; the `shoal-vcv` repo returns 404. The manual says the VCV build uses the
same engine as the hardware ("same seeds produce the same melodies on both"), so `src/shoal.cpp`
is the reference for every algorithm below.

## 1. Concept

- An 8-track **generative** melody sequencer. Tagline: "Shoal plays melodies you didn't write - but
  that always sound like you meant them." README motto: "Grown, not written."
- Each track's melody comes entirely from a **seed (0–999)**, "a recipe number, not a random amount."
  The same seed plays the same melody on every loop, restart and reload.
- **Nothing is stored.** Each step is a pure function of (seed, step index, evolve epoch, parameters).
  Every edit is non-destructive: turn a control back and the original line returns exactly.
- **One global scale.** Notes are built from scale degrees rather than quantised afterwards, so nothing
  is ever out of key.
- Tracks can **follow** each other ("like fish"). A reseed is "the shoal turning."
- The generative feel comes from polymetre (per-track length and rate) plus seeded chance, Evolve
  and Breathe.
- The VCV module is 24 HP, with two optional 4 HP expanders.

## 2. Panel (VCV)

The manual gives names but no coordinates.

**Display.** A 256×64 OLED-style "tank" (§8). TRACK buttons sit under it (presumably 8) and select
which track the knobs address. The knobs are real Rack params and jump to the new track's values;
there is no soft takeover in VCV.

**Big knobs ("performance trio")**

| Knob | Range | Function |
|---|---|---|
| CHANCE | 0–100% | Probability a step plays |
| NOTE | ±100% | Random scale-step variation; distance from centre = how often and how far, sign = direction |
| OCTAVE | ±100% | Random octave leaps, same principle |

**Small knobs**

| Knob | Range | Function |
|---|---|---|
| LENG | 1–64 | Loop length in steps |
| RATE | 29 values, /64…×64 | Speed relative to master clock |
| DIRN | 15 walks | Direction |
| TRNS | −7…+7 | Fixed shift in scale degrees (+2 on a follower = parallel thirds) |
| SHFT | −63…+63 | Non-destructive rotation of the whole pattern (canon on a follower) |
| OCTA | −3…+3 | Fixed octave shift |

The big OCTAVE knob is random leaps; the small OCTA knob is a fixed transpose. The manual flags the
naming trap itself.

**Buttons**

| Button | Scope | Function |
|---|---|---|
| FRZE | global, latching | Freeze; lit while frozen, including from CV |
| MUTE | selected track | Silences the gate; the pattern keeps running |
| SOLO | selected track | Mutes the others; soloing another track moves it; pressing again clears |
| RUN | global, latching | Play/pause |
| RSET | global | All tracks restart at step 1 on the next tick |
| RSED | selected track | Reseed: rolls a random seed, armed to land at the loop origin; lit while armed |

**Jacks.** Outputs GATE 1–8, PITCH 1–8 (pairs along the bottom), CLK OUT. Inputs CLK IN, RST IN,
RESEED, FREEZE, SEED. No RUN input in VCV (the hardware maps Run to CV).

Panel finish: Follow Rack / Silver / Black.

## 3. Tracks

8 tracks, each monophonic (one pitch CV + one gate), each with its own length 1–64 and rate.
`kMaxSteps = 64`.

## 4. Per-track parameters

There are no per-step parameters at all.

| Param | Range | Default |
|---|---|---|
| Length | 1–64 | 16 |
| Rate | 29-entry enum | ×1 (index 13) |
| Direction | 0–14 | Forwards |
| Chance | 0–100 | 100 |
| Breathe | 0–100 | 0 |
| Note | −100…+100 | 0 |
| Oct | −100…+100 | 0 |
| Evolve | 0–100 | 0 |
| Gate length | VCV 5–100%, hardware 5–95% | 50 |
| Tie | 0–100 | 0 |
| Slop | 0–100 | 0 |
| Octave | −3…+3 | 0 |
| Transpose | −7…+7 | 0 |
| Follow ("Sample source" on hardware) | Off / Track 1–8 | Off |
| Mute | Off/On | Off |
| Seed | 0–999 | 1, 98, 195, 292, 389, 486, 583, 680 (hardware; spaced 97) |
| Shift | ±63 | 0 |
| Gate level | 1–10 V | VCV 10 V, hardware 5 V |
| Pitch scale | 5–200% | 100 |
| Pitch offset | ±10 V, 0.1 V steps | 0 |

## 5. Step generation (`evalStep`)

All randomness is `hash3(seed, stepKey, salt)`, an integer avalanche hash
(`h^=h>>16; h*=0x7FEB352D; h^=h>>15; h*=0x846CA68B; h^=h>>16`).

1. **Follow.** Walk the Follow chain to the terminal track (max depth 8; self-reference ignored).
   Use that track's `activeSeed` and its Evolve epoch array, so followers evolve with their source.
2. **Shift.** `step = (step + shift) mod length`. The lookup rotates; the playhead and walk do not.
3. **Tide.** In Tide mode the pitch lookup uses `(step + rot) mod length`; rhythm uses the unrotated step.
4. **Keys.** Rhythm key = `step | epoch[step] << 8`; pitch key = `pitchStep | epoch[pitchStep] << 8`.
5. **Fire.** `hash(seed, keyR, 0xF17E) % 100 < Chance`.
6. **Tie.** `hash(seed, keyR, 0x071E) % 100 < Tie`. Rests return here.
7. **Base degree.** With k = notes in the scale, `degree = floor(rand10bit × 2k / 1024) − k`: two
   octaves of degrees centred on the root (−k…k−1). Rolled as a fraction, so the contour survives a
   scale change.
8. **NOTE.** With probability |Note|%, add d = 1…reach scale steps, reach = 1 + |Note|×11/100 (12 at
   full). Sign from the knob.
9. **OCT.** With probability |Oct|%, add 1…reachO octaves, reachO = 1 + |Oct|×4/100 (5 at full).
10. **Weight (global).** With probability Weight%, snap to the nearest stable degree (root, "third",
    "fifth") in this octave or the root above. Stable degrees: the root, the degree nearest 3–4
    semitones, the degree nearest 7 semitones.
11. **Transpose.** `degree += Transpose` (after Weight).
12. **Note.** `note = Root + 12×(Octave + q + o) + scalePc[r]`, folded by octaves into MIDI 0–127,
    keeping the pitch class.
13. **Voltage.** `(note − 48)/12` V, then × Pitch scale + Pitch offset. 0 V = C3 = MIDI 48.

Display bar height (1–5) is the degree normalised to its possible range.

**Gate.** Normally `stepPeriod × Gate% / 100` (minimum 1 sample). A tie holds the gate for 1.5 step
periods, overlapping the next step with no retrigger; a tie into a rest closes the gate.

**Slop.** A seeded per-note delay, `hash(effectiveSeed, nextPos+shift, 0x5107)`, scaled so 0…Slop%
maps to 0…½ step. Deterministic, keyed to the shifted position. If the clock speeds up while a
delayed step is pending, it fires immediately before the next one.

**Pitch hold.** Pitch CV updates only on a firing note and holds through rests and mutes.

## 6. Follow

There are no track types; every track is a generator. A follower mirrors its source's material
through its own Chance, Note, Oct, Octave, Rate, Length, Gate, Shift, Transpose, Direction and so on.
Chains resolve to the terminal track. A follower's Current comes from its own seed. Turning Follow off
leaves the track's own seed untouched. Displayed as `<n`.

## 7. The 15 walks

Enum order: Forwards, Reverse, Pendulum, Random, Drunk, Pong, Tide, Shuffle, Pools, Stride, Gravity,
Converge, Diverge, Skitter, Anchor.

| Mode | Behaviour | Deterministic | Reseed lands at |
|---|---|---|---|
| Forwards | +1 | yes | step 1 |
| Reverse | −1, starts at the last step | yes | last step |
| Pendulum | bounce, endpoints play twice | yes | step 1 |
| Random | uniform | no (shared LCG) | immediately |
| Drunk | 50% +1, 25% repeat, 25% −1, wraps | no | immediately |
| Pong | bounce, endpoints once | yes | step 1 |
| Tide | forwards; pitch rotates +1 per pass, rhythm anchored; realigns after `length` passes | yes | pass boundary |
| Shuffle | seeded Fisher–Yates per pass (`hash(seed, pass)`) | yes | pass boundary |
| Pools | pocket of 3–4 adjacent steps, 2–4 laps, then hop | no | immediately |
| Stride | 1,3,2,4,3,5…; period 2×Length | yes | step 1 |
| Gravity | forwards, each step may snap to step 1; per-seed strength 20–60%, rolled per (pass, pos) | yes | step 1 |
| Converge | 1, last, 2, last−1, … | yes | step 1 |
| Diverge | from the middle (len−1)/2 zigzag out | yes | middle |
| Skitter | random, never the same step twice running | no | immediately |
| Anchor | 1,2,1,3,1,4… ("pedal point"); period 2×(len−1) | yes | step 1 |

At the loop origin (`atOrigin`): an armed reseed lands, the pass counter resets, epochs clear and
breathing clears; otherwise the pass counter increments, Shuffle re-deals, and Evolve and Breathe roll.
Unordered modes (Random, Drunk, Pools, Skitter) are at origin on every step, so Evolve and Breathe
roll every step there (code implication, undocumented).

Random, Drunk, Pools, Gravity and Skitter draw from one shared LCG (`rng*1664525+1013904223`) across
all tracks. Matching Shoal means matching the initial state and draw order.

## 8. Clock and rate

- Internal BPM 20–300 (default 120); ×1 = one step per beat.
- CLK IN: patching switches to external at once. Rising edge > ~1 V, re-arm < 0.1 V. One pulse = one
  ×1 step. Step period for gate maths comes from the measured interval (initial fallback ¼ s).
- **29 rates:** `/64 /32 /16 /8 /7 /6 /5.3 /5 /4 /3 /2.6 /2 /1.5 ×1 ×1.25 ×1.3 ×1.5 ×2 ×2.6 ×3 ×4 ×5
  ×5.3 ×6 ×7 ×8 ×16 ×32 ×64`. As ratios: /5.3 = 10/53, /2.6 = 5/13, /1.5 = 2/3, ×1.25 = 5/4,
  ×1.3 = 13/10, ×1.5 = 3/2, ×2.6 = 13/5, ×5.3 = 53/10.
- **Scheduler (Bresenham):** per master tick, `count = floor((tick+1)·num/den) − floor(tick·num/den)`
  advances, extra advances spaced at `period·den/num`. Phase-locked to a global tick counter, so rate
  changes never drift.
- v1.2.1: divided rates land on the downbeat. (From the floor formula alone, /4 would seem to advance
  on tick 0 and then tick 3; check the actual code.)
- **Reset** zeroes the global tick and clears pending sub-steps, slop, ties and the EOS counter.
- **No swing**; Slop is the only timing feel.
- **CLK OUT:** 10 V (VCV), 50% duty at ×1, follows Run, keeps running during Freeze by default.

## 9. Display

- **Lanes:** 8, one per track; selected track has a bright marker. Bar height 1–5 = pitch, flat tick =
  rest, bright column = playhead. Density adapts: ≤16 steps chunky cells, 17–32 half width, 33–64 fine.
  The whole pattern is always visible. Dim lane without M = breathing. The lanes are the generated
  pattern itself, so turning a knob visibly reshapes them.
- **Right panel:** `T3`, SHOAL wordmark and animated fish; CH/NT/OC bright; DIR/SD/EV dim; RATE, LEN,
  OCT, TR, follow. Gate, Tie and Slop are not shown.
- **Bottom right:** key (root + scale short name, e.g. "C NMIN"), replaced by a blinking RESEED ARM;
  FRZ while frozen.
- **Amber = CV.** Modulated tracks' lane numbers and modulated values turn amber and show the
  modulated value. Knobs show what's set; the display shows what's playing.
- **Screensaver** (Off / 1 / 5 min): 8 fish dart when their track plays. Wake touch is swallowed; CV
  does not wake it.

## 10. Generative features

- **Reseed track:** random seed 0–999, armed to the walk's origin.
- **Reseed all:** writes a random base to a global param; each seed = `hash(base, t, 0x5EED) % 1000`,
  written into the Seed params so presets capture them. If the new base equals the old, +1. Arms during
  Freeze and lands after.
- **SEED CV:** 0–10 V → 0–999 for the selected track (~10 mV per seed), armed.
- **Evolve (0–100%):** each pass, each step's epoch increments with probability Evolve%
  (`hash(seed, pass<<8|s)`), re-hashing its pitch and rhythm. Repeatable lineage; reseed starts fresh;
  followers share epochs. Epoch is `uint8` (wraps at 255).
- **Breathe (0–100%):** chance a whole pass rests (`hash(seed, pass)`).
- **Weight, Slop, Follow, Gravity/Shuffle/Tide/Pools/Skitter** as above.
- **Freeze:** nothing advances; every started track's gate goes high at once ("bloom"), last pitches
  hang as a chord; muted tracks stay silent. Clock out Runs (default) or Stops. While Runs, the tick
  counter keeps advancing so divided tracks resume in phase.

## 11. Scales (global)

| Scale | Semitones |
|---|---|
| Chromatic | all 12 |
| Major | 0 2 4 5 7 9 11 |
| Natural minor | 0 2 3 5 7 8 10 |
| Harmonic minor | 0 2 3 5 7 8 11 |
| Dorian | 0 2 3 5 7 9 10 |
| Phrygian | 0 1 3 5 7 8 10 |
| Lydian | 0 2 4 6 7 9 11 |
| Mixolydian | 0 2 4 5 7 9 10 |
| Major pentatonic | 0 2 4 7 9 |
| Minor pentatonic | 0 3 5 7 10 |
| Blues | 0 3 5 6 7 10 |
| Hirajoshi | 0 2 3 7 8 |
| In-Sen | 0 1 5 7 10 |

Root: VCV C…B; hardware a MIDI note (default 48). A key change reaches each track on its next note.

## 12. Expanders (VCV only)

**Shoal Expander** (4 HP). TRACK knob picks the track its inputs address.
- Per-track inputs: CHANCE, NOTE, OCTAVE, EVOLVE, BREATHE, SLOP, TRNS, SHFT, OCTA, GATE.
- Global: WEIGHT, ROOT.
- Additive and clamped: 0–10 V spans unipolar params; ±5 V spans bipolar ones (NOTE, OCTAVE, TRNS,
  SHFT, OCTA). ROOT is absolute 1 V/oct, 0 V = C.
- Never moves knobs, never saved; shown amber.
- Up to 8 chained, either side; CV on the same track sums; nearest patched ROOT wins.
- Not modulatable: RATE, LENG, DIRN, Tie, Follow, Seed.

**Shoal Outs** (4 HP), one per Shoal.
- **Current** per track: a new seeded target on every advance (fired or not, muted or not),
  `hash(own seed, pos | epoch<<8, 0xC4E7) % 1001 × 0.01` V, approached by smoothstep `t²(3−2t)` over
  one step period. 0–10 V or ±5 V. Holds through Freeze.
- **EOS** per track: 10 V, half a step, every Length advances regardless of direction; not gated by
  Chance or Mute; silent in Freeze; a reset realigns it.

## 13. Poly routing and voltage

- **Poly output routing:** Off / Pairs (jacks 2,4,6,8 carry 2 ch) / Split 4+4 (jacks 4 and 8 carry
  4 ch) / All 8 (jack 8 carries 8 ch). Mono jacks keep working; poly only adds channels to the hub.
- Gate level 1–10 V; pitch scale 5–200% (120% for 1.2 V/oct); offset ±10 V after scale.

## 14. Mute, solo, save

- Mute silences the gate only; the pattern keeps running; pitch holds. Saved.
- Solo: VCV says saved; hardware keeps it in RAM only.
- Everything is saved in the patch and presets. No pattern banks, song mode or copy/paste; patterns
  are switched by sequencing SEED or patching EOS → RESEED. Seeds are the shareable unit.

## 15. MIDI

VCV: none beyond Rack MIDI-mapping of params. Hardware: per-track MIDI channel, global velocity,
destination bitmask, mono per track, MIDI clock in at 24 PPQN (24 clocks = one ×1 step), Start/
Continue/Stop.

## 16. Context menu (VCV)

Track: Evolve, Breathe, Gate length, Tie, Slop, Gate level, Pitch scale, Pitch offset, Follow, Seed
(drag to audition). Global: Scale, Root, Weight, BPM, Panel, Screensaver, Frozen clock out, Poly output
routing, Currents range, Reseed all. Expander actions: Add Shoal Expander, Add Shoal Outs. Menu sliders
turn into text boxes on click.

## 17. Hardware performance UI (ideas)

| Control | Turn | Push+turn | Tap | Hold |
|---|---|---|---|---|
| Pot L/C/R | Chance / Note / Oct | – | toggle home ⇄ dialled | – |
| Encoder L | select track | Length | Mute | Solo |
| Encoder R | Rate | cancels reseed | Reseed track | Reseed all |

Push-to-home stashes the dialled value per track and pot. Pots ignored ~200 ms around a press.
Soft takeover within 3%.

## 18. Recipes

- Following bass: Follow T1, OCTA −2, RATE /4, CHANCE ~50%, Gate ~80%.
- Parallel harmony: Follow T1, same RATE/LENG, TRNS +2 or +4.
- Canon: Follow T1, same RATE/LENG, SHFT −4, OCTA −1.
- Evolving ambient: lengths 16/11/7, rates /2 and /4, CHANCE 30–50%, Breathe 20%, Evolve 5–10%,
  Tie 30%, Slop 10%, Weight 60%, In-Sen or Hirajoshi.
- Hi-hats: CHANCE 65%, Gate 10%, Slop 15%, RATE ×8.
- New section every phrase: EOS 8 → RESEED with track 8 at LENG 64.
- Hands-free dynamics: LFO into CHANCE CV with the knob at 0.

## 19. Gaps and inconsistencies

- No panel coordinates or TRACK button count in the docs.
- VCV/hardware differences: gate default 10 V vs 5 V; gate max 100% vs 95%; CLK OUT/EOS 10 V vs 5 V;
  root pitch class vs MIDI note.
- No VCV MIDI, RUN input, swing, per-step editing, velocity, ratchet, slide, banks or song mode.
- README says "nine" and "thirteen" directions; there are 15. Hardware `dirShort[9]` has 9 entries
  for 15 modes (likely an out-of-bounds display bug).
- Rate/Length/Direction/Tie/Follow changes apply immediately in the code; shortening Length clamps.
- The divided-rate downbeat fix and the first-interval-after-reset behaviour need checking in code.
