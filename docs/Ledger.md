# Ledger — generative sequencer · SCHEDULE L

**An eight-track sequencer that grows its melodies, and writes them down when
you want to keep them.** By default each track's melody is grown from a seed
(0–999). Every step is worked out the moment it plays from the seed, the step
and the track's settings. Two things follow from that. Nothing a knob does is
destructive, because turning it back brings the exact line back. And a seed
number is a melody: give someone the seed and the settings, and they hear what
you heard. A track can also play a **written pattern**: notes you draw or
capture, chords and all. Each track holds sixteen slots of either kind, and
launches between them. 35 HP.

Ledger's generator is **Shoal**, by Ormer Modular, whose engine is open source
under the MIT licence (<https://github.com/ormermodular/shoal>). Ledger runs a
line-for-line port of it, described under "Compatibility" below. The written
patterns, slots, voices, the per-track effects and the MIDI follow Squarp's
Hermod+. This manual describes what is in the module today.

The research notes the design comes from are in `docs/research/Shoal.md` and
`docs/research/HermodPlus.md`.

## The idea in one paragraph

Each track holds a seed. The seed gives a **base melody**: two octaves of the
current scale, centred on the root, rolled per step. Everything else filters
that base melody. CHANCE decides which steps sound, NOTE and OCTAVE bend some
of them away, the walk decides which step comes next, and so on. A track can
**follow** another track: it plays the other track's material through its own
settings. A follower on the same rate and length with TRNS +2 plays parallel
thirds; give it SHFT −4 and it plays a canon.

## The display

The display has five pages: the eight tracks at a glance, the selected track's
piano roll, its effects, the slots, and the song.

Across the top of the glass, on every page, is a **strip of tabs**: Ledger's
buttons, on the screen. Its first row is the pages (**TANK**, **ROLL**, **FX**,
**SEQ**, **SONG**) and the eight tracks (**T1–T8**); its second the transport
(**RUN**, **RESET**, **FREEZE**, **REC**) and what is done to the selected track
(**MUTE**, **SOLO**, **RESEED**, **CAPTURE**). Click a tab to press it. A tab lights
the way its button's LED did: the page on show, the selected track (and the
others glowing with their gates), RUN while running, and so on. They are Rack
parameters, so right-click one for its menu, and MIDI-Map can learn it. What
each does is under "Buttons" below.

### TANK

The left of the display holds eight lanes, one per track. Each lane shows the
whole pattern, however long. A bar's height is the note's pitch, a flat tick is
a rest, and the bright cell is the playhead. Dots past the end mark unused
cells in a short loop. The lanes are the real future pattern, so turning CHANCE
or NOTE visibly reshapes them before you hear it. A dim lane is **breathing**
(resting this pass). After each lane come its rate, its active slot (`G3` is
the generator in slot 3, `P5` the pattern in slot 5), `<n` if it follows track n,
and S or M for solo or mute, or `>` while a launch is queued. A pattern track's
lane shows the height of the highest note in each step, with a tick above it for
each extra note of a chord.

The right of the display shows the selected track's books: its number, the seed
it is playing, chance (CH), note (NT), octave (OC), rate, length, direction
(DIR), evolve (EV), breathe (BR), fixed octave (OCT) and transpose (TR), then
the key (root and scale). The line under the key blinks **RESEED ARM** while a
reseed waits for the loop to come round, and shows REC, FRZ while frozen, STOP
while stopped, or `> n` while a launch is queued.

**Every value in the books is a control.** The pointer turns into up/down
arrows over a number: hold it and drag up or down (Ctrl drags finely, Shift
coarsely; double-click resets). RATE, DIR, OCT, the root and the scale are
lists: click one to pick from them, or hold and drag to step through them.
Right-click any of them for Rack's parameter menu, where a value can be typed
in. They set the same things the knobs of the same names did, for the selected
track, so a patch made before they moved loads unchanged, and undo and MIDI-Map
work on them as on a knob.

Click a lane to select its track.

### ROLL

The selected track's active slot as a piano roll: time across, the loop's
length; pitch up, 25 semitones at a time (scroll to move). The highlighted
column is the step playing now. The header shows the slot, its length and rate,
the snap, the note count and the lane on show.

| mouse | |
|---|---|
| click empty space | add a note there, on the snap grid, as long and as loud as the last one touched |
| drag a note | move it, in time (on the snap grid) and in pitch |
| drag a note's right end | change its length |
| Alt-drag a note | change its velocity (up is louder); brighter notes are louder |
| right-click a note | delete it |
| right-click empty space | the pattern menu |
| scroll | move up and down in pitch |

| key (pointer over the display) | |
|---|---|
| Delete / Backspace | delete the selected note |
| ← → | move it by the snap; with Shift, shorten or lengthen it |
| ↑ ↓ | move it a semitone; with Shift, an octave |
| R | randomize the pattern |

Below the notes is the **lane strip**: one point per step for one of six lanes
(MOD 1–4, BEND, TOUCH), with a curve between points: OFF (hold), LIN, S, LOG or
EXP. Click or drag in the strip to set points; right-click a point to remove
it. MOD 1 comes out of the MOD jack. All six go out over MIDI when the track has a
MIDI output (MOD 1–4 as CCs, BEND as pitch bend, TOUCH as channel pressure), and
MOD 1–4 are mod-matrix sources.

The pattern menu (right-click) has:
- Randomize, which draws a new pattern in the current scale and key. Its
  density, pitch range, lengths and velocities are under *Randomize settings*,
  and notes land on the snap grid.
- Re-roll pitches, lengths or velocities alone.
- Snap: a step down to 1/8, or off (1/24 of a step).
- Which lane is shown, and its curve.
- Clear the lane, or clear the notes.

On a generator slot the roll says so, and offers CAPTURE.

### FX

The selected track's effects chain. On the left are its eight slots, in the
order notes pass through them. On the right are the chosen effect's parameters,
as bars.

- **Click a slot** to show its parameters.
- **Right-click a slot** to choose its effect, move it up or down, copy, paste
  or clear it.
- **M** mutes the effect on every slot; **S** mutes it only while the playing
  slot plays (Hermod's pattern mute). Muting an effect that holds notes ends
  them.
- **Drag a bar** sideways to change a value. **Double-click** resets it.
- **Right-click a bar** to:
  - type a value;
  - give the playing slot **its own value** for that parameter (Hermod's
    pattern values; the bar turns lime and says *slot*), or go back to the
    track's value;
  - route CV A–D or a MOD lane to it through the mod matrix.
- A value CV is moving shows where it is playing, beside where it is set.

### SEQ

Every track's sixteen slots: a column per track, a row per slot. `GEN 347` is a
generator with seed 347; `PAT 12` a pattern of 12 notes; `·` an empty slot. The
playing slot is lit, and a queued one blinks.

- **Click a slot** to launch it on its track (and select the track).
- **Click a row number** to launch the whole row: a sequence. **Right-click it**
  to launch it, add it to the end of the song, or copy, paste or clear the whole
  row (every track's slot in it).
- **Right-click a slot** for its menu:
  - Launch it, or its whole row.
  - New generator: the track's present settings with a random seed.
  - New pattern: empty.
  - Copy the playing slot here.
  - Clear it.

A launch waits for the moment set under *Launch slots* in the module menu.
Three settings are per track:
- immediately;
- on the track's next step;
- at the end of the track's loop (the default).

The rest launch a queued row together:
- on a grid of 4–64 clocks;
- **when every loop ends together** (Hermod's MODULO: the least common multiple
  of the playing tracks' loops);
- at the end of the **shortest** loop;
- at the end of the **longest** loop;
- at the end of **one track's** loop (chosen in the menu).

A loop's length is counted in master ticks. Eight steps at ×2 is four ticks;
five steps at ×1.5 lines up with the clock only after ten. A modulo longer than
about a million ticks is held there. While stopped, a launch happens at once. An
empty slot is silence.

A launched slot **starts at its first step** (it can be turned off in the menu,
when it carries on from where the track was). A slot launched just as the
outgoing one finishes its loop, and as long as it, simply goes on round, walk and
all, so nothing about a generator's next loop changes. A generator's note still
sounding when a pattern takes over is finished by the pattern track's voices,
and ends when Shoal would have ended it. A tie stays legato into the pattern's
first note, which is what lets CAPTURE's pattern replay the generator sample for
sample.

### SONG

The song is a list of rows to play in order, each a number of times through,
up to 32 entries. It plays from the first entry to the last and goes round
again.

- **Click the header** to start or stop it (or *Play the song* in the menu).
  Started while the clock runs, it waits for the playing row's period to come
  round; started while stopped, its first row is there at once. A reset takes it
  back to its first entry.
- **Click +** to add an entry. Its row is the next after the last entry's (the
  first adds the selected track's playing row). A row number's menu on SEQ can
  add any row.
- **Scroll** over an entry to change its row; with Shift, the times through.
- **Right-click** an entry to set its row or times, duplicate it, move it, or
  delete it, or to clear the song.

A pass of a row is the period *Launch slots* names: a grid, or the modulo,
shortest, longest or one track's loop. The three per-track settings count as
modulo. The playing entry is lit, and shows which pass it is on. Stopping the
song leaves the row it was on playing.

### Undo

Rack's **Undo** and **Redo** (Edit menu, or Ctrl/Cmd-Z) take back Ledger's own
edits as well as its knobs:
- the piano roll and lane strip, one step per gesture;
- a slot made, copied or cleared, and a row pasted or cleared (one step for the
  row);
- an effect chosen, moved, pasted, cleared, muted, or a value set (including a
  slot's own values);
- every change to the song.

Launches, recording, CAPTURE, the matrix and the menus' settings are not undo
steps.

## Slots and patterns

Each slot holds its own settings: length, rate, direction, chance, breathe,
note, octave leaps, evolve, gate, tie, slop, octave, transpose, seed and shift.
Launching a slot stores the outgoing slot's settings back into it and loads the
incoming one's, so the knobs move with it. Follow, mute, the voices and the
output settings belong to the track and stay put.

**A pattern is played by the same walker as a generator.** On a pattern track:
- LENG is the pattern's length, up to 64 steps.
- RATE is its speed (the 29 rates include Hermod's zooms ÷4, ÷2, ÷1.5, ×1.5,
  ×2 and ×4).
- DIRN is any of the fifteen walks, over the pattern's steps.
- SHFT rotates the pattern.
- SLOP leans each step late, from the seed.
- BREATHE rests whole passes.
- CHANCE is each note's chance of playing, rolled afresh each pass but the same
  pass after pass from a reset.
- TRNS transposes in **semitones** (on a generator it is scale degrees) and
  OCTA in octaves.
- NOTE ±, OCTAVE ±, EVOLVE, TIE, GATE and WEIGHT do nothing; the display shows
  them as `-`.

A step is 24 ticks, and a note can start on any tick and last any number of
them. Chords, overlaps and notes between steps are all fine.

**CAPTURE** writes the selected generator's loop into its next empty slot and
launches it. The captured pattern keeps the generator's length, rate, direction,
seed (so SLOP leans the same) and breathe. Chance, note and octave leaps,
evolve, tie and shift go back to neutral, because their effect is already in
the notes. On a Forwards walk, the captured pattern replays the generator
sample for sample (the module's test checks this), and then it is yours to edit.

### Voices

A pattern track has 1–8 voices (*Voices* in the menu). Its PITCH and GATE jacks
carry one channel per voice, so a three-voice track's chords come out as a
three-channel cable. Which voice a note takes is Hermod+'s allocator choice:

| allocation | |
|---|---|
| Longest free (default) | the voice that has been silent longest; if all are busy, the oldest note is cut |
| Same note keeps its voice | a repeated pitch goes back to the voice it played on |
| First free, else drop | voices in order; a note that does not fit is not played |
| Round robin | each note takes the next voice |
| Random | a random free voice |

A note that lands on a voice still sounding is **legato** (the gate stays
high); *Retrigger overlapping notes* in the menu dips the gate for 1 ms
instead, so envelopes restart. A generator track is always one voice, exactly as
Shoal plays it.
## Knobs and the books

Fourteen settings address the **selected track**. Selecting a different
track moves them to that track's values. CHANCE, NOTE ±, OCTAVE ±, SHFT, GATE,
TIE and SLOP are knobs on the face; the others are set on the display's books
(TANK page), and the performance trio, CHANCE, NOTE ± and OCTAVE ±, is on both.

| setting | range | what it does |
|---|---|---|
| CHANCE | 0–100 % | how likely each step is to sound |
| NOTE ± | ±100 % | random scale-step variation. Distance from the centre sets how often a note varies and how far it can go (up to 12 scale steps); the sign sets the direction. At 0, the pure base melody |
| OCTAVE ± | ±100 % | random octave leaps, on the same principle (up to 5 octaves) |
| LENG | 1–64 | loop length in steps |
| RATE | /64 … ×64 | speed against the master clock; 29 exact ratios |
| DIRN | 15 walks | the order steps are visited in (see below) |
| TRNS | −7…+7 | fixed transpose in **scale degrees**, so it always stays in key |
| SHFT | −63…+63 | rotates the whole pattern without changing it |
| OCTA | −3…+3 | fixed octave transpose (OCTAVE ± is the random one) |
| EVOLVE | 0–100 % | each pass, this share of steps quietly re-roll. The drift is seeded, so it repeats |
| BREATHE | 0–100 % | chance that a whole pass rests |
| GATE | 5–100 % | gate length, as a share of the step |
| TIE | 0–100 % | chance a note holds into the next step without retriggering |
| SLOP | 0–100 % | a seeded per-note lateness of up to half a step. The same notes lean by the same amount every loop |

Four settings are **the books**, shared by every track. WEIGHT and BPM are
knobs; SCALE and ROOT are the key line of the display's books.

| setting | what it does |
|---|---|
| SCALE | Chromatic, Major, Natural minor, Harmonic minor, Dorian, Phrygian, Lydian, Mixolydian, Major pentatonic, Minor pentatonic, Blues, Hirajoshi, In-Sen |
| ROOT | C … B. A track adopts a new key on its next note |
| WEIGHT | consonance: this share of notes is pulled to the nearest root, third or fifth |
| BPM | the internal tempo, 20–300. ×1 is one step per beat. Ignored while CLOCK is patched |

Rack's parameter menu, MIDI-mapping and undo work on the knobs and the books'
fields as usual. A mapped setting controls whichever track is selected.

## CV

**Every setting but BPM has a CV jack.** Under each knob is its own; the row
under those holds the books' settings, each jack named (LENG, RATE, DIRN, TRNS,
OCTA, EVOLVE, BREATHE, SCALE, ROOT). It adds to the setting and the result is
clamped, as Shoal's expander does it. One
volt is a tenth of the setting's range, so 0–10 V sweeps a one-sided setting
(CHANCE, EVOLVE, GATE …) from bottom to top and ±5 V sweeps a two-sided one
(NOTE ±, TRNS, SHFT, OCTA …) from end to end.

- **The track jacks are polyphonic.** Channel n moves track n. A mono cable
  moves every track at once.
- **SCALE and WEIGHT** add to the books.
- **ROOT replaces its knob while patched.** It reads 1 V/oct with 0 V = C, so a
  keyboard or quantised sequence changes key; it reads only the pitch class.

**CV A–D** are four free inputs for the **mod matrix**. Each track has four
matrix slots, in the context menu under *Mod matrix*. A slot routes one source
(CV A–D, the track's own MOD 1–4 lanes, or a MIDI CC the track receives) to one
of the track's fourteen settings or any parameter of its effects, with:

| | |
|---|---|
| Amount | −100…100 %, an attenuverter. At 100 %, the source's full swing covers the setting's full range |
| Polarity | *Bipolar* reads ±5 V as −1…1. *Increase only* reads 0–10 V as 0…1 and never pulls a setting down. A lane or a CC (0–127) is read the same way: bipolar centres on 64, the way pitch bend does, so 64 changes nothing and 0 and 127 are the two ends; increase only reads 0–127 as 0…1 |
| CC | for a MIDI CC source: which CC (0–119). *Learn* takes the next CC the track receives |
| Offset | −100…100 % of the setting's range, added while the slot has a source |

The matrix also adds to the setting, so its knob or field still works underneath. A CV
jack and matrix slots aimed at the same setting add together.

**Knobs show what is set; the display shows what is playing.** A value under
CV is drawn in lime at its modulated value, and so is the lane number of any
track something is modulating. That includes the books' own fields: one under
CV shows, in lime, where it is playing, and dragging it moves where it is set.
Modulation is never saved and never moves a knob: a patch saved mid-sweep keeps
the knob's value.

## Effects

Each track has eight effect slots, in series. Notes, from a pattern or a
generator, go through them in order and on to the track's voices. Effects keep
time with the master clock: one clock is one beat (a quarter note), so 1/16 is
a quarter of a clock, and anything synced lands on the clock's grid.

A **generator** with no live effect skips the chain entirely and plays exactly
as Shoal made it. Give it an effect and its gate and pitch are read back as
notes (a tie is legato), which then go through the chain like a pattern's.

Squarp's firmware is closed, so these are written from the Hermod+ manual and
the table below is Ledger's own definition of them.

| effect | what it does | parameters |
|---|---|---|
| **Arpeggiator** | Holds the notes played and walks them on a grid. Letting go of every key stops it | Style (up, down, up-down, down-up, as played, random, converge, diverge); Rate (1/1 to 1/64, with triplets); Octaves (−5 to 5); Gate (1–200%; past 100% notes overlap, legato); Humanize; Restart on new note; Cycles (0 = endless) |
| **Bernoulli** | Sends each note down path A (on down the chain) or path B by chance. Path B drops the note, or sends it to a MIDI port instead; its note-off follows it there | Chance of B; Path B (drop / MIDI out A / MIDI out B); B channel |
| **Chance** | Each note's chance to play | Chance; One roll per (note, 1/16, 1/8, beat, bar); Chance × velocity; Sync grid and Chance on the grid (notes landing exactly on the grid get their own chance) |
| **Echo** | Repeats each note | Time; Repeats (0–16); Fade velocity and Fade gate (off, linear, exponential, logarithmic); Pitch up / Pitch down (the last repeat reaches this many semitones; both on alternate) |
| **Envelope** | An AHDSR that writes a lane, triggered by notes (which pass) | Attack, Hold, Decay, Sustain, Release (1 ms to 10 s); Depth; Offset; Destination (MOD 1–4, BEND, TOUCH); Mode (gate, trigger, wait for AHD); Sidechain note (only that note triggers it) |
| **Euclid** | Notes on a Euclidean rhythm. With a fixed note it adds its own and passes the rest; with −1 it plays the held notes on its hits | Note; Steps (1–32); Fills; Rate; Gate; Rotate; Velocity |
| **Filter** | Lets a range of notes and mod values through (or, with low above high, keeps it out) | Lowest / Highest note; Lowest / Highest mod |
| **Glide** | Notes glide to their pitch from the voice's last one | Type (none, linear, exponential, smooth); Time (10 ms to 10 s) |
| **Harmonizer** | Adds up to four notes to each one | Keep the played note; Harmony 1–4 (±24 semitones, 0 = off) |
| **Hold** | Holds notes after their keys are up | Mode (hold: pressing a held note lets it go; relatch: a new chord replaces the old once every key was up); Most notes |
| **LFO** | Writes a lane; notes pass | Wave (sine, triangle, ramp, square, random); Destination; Sync; Period (1/16 to 32 bars, synced) or Rate (0.1 Hz to 1 kHz, free); Range; Offset; Phase |
| **Mod to note** | Turns a lane into pitch: each note's pitch (sample and hold), or notes of its own on a grid | Sample (on each note, or a division); Lowest / Highest note; Pass mod on; Source lane |
| **MIDI output** | A tap: sends what reaches it to a MIDI port and channel, and passes everything on down the chain. Put it between two effects to send the middle of a chain. Lanes go out the way the track's MIDI settings map them | Port (Out A / Out B); Channel; Send notes; Send mod; Velocity scale |
| **Note to mod** | Writes a lane from notes | Destination; Value (velocity, gate, pitch); At note-off (−1 = hold); Pass notes on |
| **Randomizer** | Varies each note within limits | Note down / up (semitones); Octave down / up; Velocity down / up; Length (holds note-offs up to a bar); Chance |
| **Ratchet** | Repeats a held note on a grid | Rate; Gate; Long-short (alternately lengthens and shortens) |
| **Register** | A Turing-machine shift register making its own notes; others pass | Seed; Rate; Bits (2–16); Chaos (0 and 100% are locked loops); Centre; Span; Gate; Every step |
| **Scale** | Puts notes in key | Scale and Key (or the books'); Out-of-key notes (nearest, down, up, drop); Transpose first |
| **Swing** | Delays the off-beats, accents and humanizes | Groove (50% straight); Grid (1/8, 1/16, 1/32); Accent; Human |

Whatever an effect does to a note, its note-off follows. A harmony, an echo or
an arpeggio note ends when the note that made it ends, and muting, removing or
changing an effect ends what it was holding. The module's tests check this by
fuzzing hundreds of random chains under random edits.

**Lanes.** Six lanes travel the chain alongside notes: MOD 1–4, BEND and TOUCH.
They come from a pattern's lane strip and from the Envelope, LFO and Note to mod
effects. The MOD jack carries each track's MOD 1. The matrix can use MOD 1–4 as
sources, so a lane, or an LFO writing to one, can move any of the track's
settings or effect parameters.

## Buttons

Ledger's buttons are the tabs across the top of the display (see "The display").

| tab | what it does |
|---|---|
| T1–T8 | select a track. The tab is full on the selected track and flickers with the others' gates |
| TANK, ROLL, FX, SEQ, SONG | the display's page |
| CAPTURE | write the selected generator's loop into its next empty slot, and launch it (see Slots and patterns) |
| REC | record MIDI in into the playing patterns (see MIDI). Lit while recording; flashing while it waits for a first note (punch in) or for the clock |
| MUTE | silence the selected track's gate. Its pattern keeps running and its pitch holds |
| SOLO | mute every other track. Soloing another track moves the solo; pressing again clears it |
| RESEED | give the selected track a random new seed. It lands at the track's loop origin, and the light stays on until it does |
| RUN | play / pause |
| FREEZE | nothing advances. Every track that has started holds its gate high, so the last notes hang as a chord |
| RESET | every track restarts at step 1 on the next tick |

## The walks

| walk | order |
|---|---|
| Forwards | 1, 2, 3 … |
| Reverse | … 3, 2, 1 |
| Pendulum | there and back; the end steps play twice |
| Random | any step (not reproducible) |
| Drunk | half the time forward, a quarter repeat, a quarter back |
| Pong | there and back; the end steps play once |
| Tide | forwards, but the pitches drift one step through the rhythm each pass |
| Shuffle | every step once per pass, in a new seeded order each pass |
| Pools | loops a pocket of 3–4 steps two to four times, then hops elsewhere |
| Stride | 1, 3, 2, 4, 3, 5 … |
| Gravity | forwards, with a seeded chance each step of snapping back to step 1 |
| Converge | 1, last, 2, last−1 … meeting in the middle |
| Diverge | from the middle outwards |
| Skitter | random, never the same step twice running |
| Anchor | 1, 2, 1, 3, 1, 4 … a pedal note |

A reseed waits for the walk's own origin, so it lands on a phrase boundary.
Random, Drunk, Pools and Skitter have no origin, so a reseed lands on the next
step.

## Jacks

| jack | |
|---|---|
| CLOCK in | one pulse is one ×1 step. Patching it replaces the internal tempo at once |
| RESET in | the same as the RESET button |
| RUN in | while patched, the transport: high plays, low stops |
| RESEED in | each trigger reseeds every track (each lands at its origin) |
| FREEZE in | a gate, combined with the button |
| SEED in | 0–10 V picks the selected track's seed (about 10 mV per seed), armed. Quantised CV gives repeatable seeds |
| CV A–D | sources for the mod matrix |
| (under the knobs, and the row below) | see CV above |
| PITCH 1–8 | 1 V/oct, 0 V = C3, or the track's pitch standard. Holds through rests. A channel per voice on a pattern track |
| GATE 1–8 | 10 V gates (the menu's Gate level). A channel per voice |
| VEL | 8 channels: each track's last velocity, 0–10 V. A generator's is the MIDI velocity setting, 100 |
| MOD | 8 channels: each track's MOD 1 lane, 0–10 V: a pattern's lane strip as its playhead moves, or what an effect writes |
| CURRENT | 8 channels: each track's **Current**, a slow seeded drift that moves to a new value every step and eases there over the step. 0–10 V (±5 V in the menu) |
| EOS | 8 channels: a pulse every LENG steps of each track, whatever its walk |
| CLOCK out | the master clock, ×1 |

## Menu

- **Track n** (the selected one):
  - Seed (type a number).
  - Follow (share the seed of another track).
  - Voices, Voice allocation and Retrigger overlapping notes (see Voices).
  - Gate level (1–10 V).
  - Pitch scale (5–200 %) and Pitch offset (in 0.1 V).
  - **Pitch standard**: 1 V/oct; 1.2 V/oct for Buchla gear; or Hz/V for Korg
    MS and Yamaha CS synths, where C3 is 1 V and each octave doubles it (held
    at 10 V). It applies after the scale and offset.
  - **Mod matrix** (see CV).
  - **MIDI** (see MIDI): input channel, the CC that plays MOD 1, whether it
    follows the transpose leader; output port and channel, and the CC each MOD
    lane sends.
- **Books**:
  - Reseed all tracks.
  - Launch slots: when a queued slot takes over, and which track's loop the
    one-track setting waits for (see SEQ).
  - A launched slot starts at its first step (see SEQ).
  - Play the song (see SONG).
  - Frozen clock out (whether CLOCK out keeps running while frozen).
  - **Poly output routing**, as Shoal does it. *Pairs*: PITCH/GATE 2, 4, 6
    and 8 each carry their pair of tracks as two channels. *Split 4+4*: jacks
    4 and 8 carry tracks 1–4 and 5–8. *All 8*: jack 8 carries every track. A
    hub jack's first channel is its group's first track (its first voice, on a
    pattern track); every other jack carries its own track.
  - Currents ±5 V.
  - **MIDI** (see MIDI): the input device and who it plays; clock from MIDI;
    program change; the transpose leader; Out A and Out B, each a device and a
    clock mode; all notes off.
  - **Recording**: record, the mode (overdub, replace, looper) and punch in.

## MIDI

Ledger has one MIDI input and two outputs, **Out A** and **Out B**, chosen in
the menu under *Books > MIDI*. Every track can send on either output, each on
its own channel, so one device carries all eight.

### In

*Input plays* is either **the selected track** (the default: select a track
and play it, on any channel), or **tracks by their input channel**, where each
track hears the channel set in its own MIDI menu: Off, 1–16 or Any. A new
Ledger has track n on channel n.

What a track hears goes into its chain like its own notes, so its effects
process what is played as well as what is sequenced, and its voices play it.
A track hearing MIDI always runs its chain. A generator played into is still
Shoal, note for note (the module test checks this); played notes and the
generator's share its voices.

- Notes, with velocity. The sustain pedal (CC 64) holds released notes until it
  lifts.
- One CC (CC 1 by default, set per track) plays the **MOD 1** lane; pitch bend
  plays **BEND**; channel pressure plays **TOUCH**.
- Every other CC is kept as a matrix source (see CV). Choose *MIDI CC* as a
  slot's source and *Learn* to take the next one moved, or type its number. On
  the FX page, *Modulate with > MIDI CC* on a parameter does both in one step.
- **Program change** n queues row n + 1 of the SEQ grid on every track, at the
  launch quantisation (it can be switched off).
- **Clock from MIDI in**: while CLOCK in is unpatched, MIDI clock drives the
  master clock (24 clocks = one tick). Start resets every track to step 1, Stop
  pauses, Continue resumes in place.
- **The transpose leader**: give it a channel and notes on that channel stop
  playing tracks and instead move them. In the default mode the note is a
  transposition, C4 (note 60) being none. It moves every track set to follow
  it: a pattern's notes by semitones, a generator's pitch output by the same.
  Turn *Follows the transpose leader* off for a drum track. In the other mode
  the note's pitch class becomes the books' root, as if ROOT were turned, so
  generators stay in key. The transposition shows beside the key on TANK.

### Recording

REC (or *Books > Recording > Record*) writes what the input plays into each
track's playing slot while the clock runs. Notes land where the track's
playhead is, at the roll's resolution of 24 ticks a step. Lanes get a point at
the step they arrive in.

- An **empty** slot becomes a pattern with the track's current settings. A
  **generator** slot is never written into: CAPTURE it first.
- **Overdub** adds notes to what is there.
- **Replace** clears each step as the playhead reaches it, so a pass with REC
  on rewrites the loop.
- **Looper** starts a new loop at the first note played: the slot is cleared,
  and that note's step becomes the loop's first. When REC is turned off, the
  loop's length is set to the steps played, to the nearest step (at most 64).
  Shift is set so the loop goes round from where it began. The looper assumes
  the track walks forwards.
- **Punch in** waits for the first note before recording starts. REC flashes
  until then.

### Out

A track with an output sends its notes as they leave its chain, after every
effect, with what its lanes do. A generator with no live effect is read back
from its gate and pitch; its jacks are untouched. Ties are sent legato, the
next note before the last one's note-off.

- Notes sounding on a port are counted. Two sources sounding the same note on
  the same channel each send a note-on, and only the last to let go sends the
  note-off.
- A track that changes its port or channel ends what it was sounding there
  first. So does an effect that is muted, removed or changed, and a port whose
  device changes. *All notes off* ends everything both ports have sounding.
- A muted track sends no new notes.
- MIDI goes out once per sample, but Rack hands it to the driver as each audio
  block is processed. Its timing is therefore as fine as the audio block size,
  as with any Rack MIDI module.
- **Clock out**, per port: off, clock and transport, clock only, or transport
  only. 24 clocks a tick, the first on the tick and the rest spread across it.
  If the master clock speeds up, a beat that ends early sends the clocks it
  still had at once, so the far end counts exactly 24 every beat. Running from
  the top (after a reset, or at first) sends Start; resuming where it paused
  sends Continue, since that is what Ledger does. A reset while running sends
  Start, with the downbeat's clock on the same sample. Stopping sends Stop.

## Recipes (Shoal's own)

- **Following bass line**: track 2 follows track 1, OCTA −2, RATE /4, CHANCE about 50 %, GATE about 80 %.
- **Parallel harmony**: follow track 1 with the same RATE and LENG, and TRNS +2 for thirds.
- **Canon**: follow track 1 with the same RATE and LENG, SHFT −4 and OCTA −1. Shift the follower, never the leader.
- **Evolving ambient**: lengths 16, 11 and 7, rates /2 and /4, CHANCE 30–50 %, BREATHE 20 %, EVOLVE 5–10 %, TIE 30 %, SLOP 10 %, WEIGHT 60 %, In-Sen or Hirajoshi.
- **A new section every phrase**: patch EOS channel 8 to RESEED, with track 8 at LENG 64.

## Compatibility with Shoal

`src/Ledger/Shoal.hpp` is Shoal's sequencing code (v1.2.1) with the disting NT
host calls swapped out. `tests/Ledger/test_golden` builds the original
`shoal.cpp` (vendored in `vendor/shoal/`) next to it. It runs both through
150 fuzzed scenarios of 100,000 frames each, covering every walk, rate, scale,
follow chain, freeze, solo, reseed, reset and clock source, and requires every
gate, pitch, Current, EOS pulse and MIDI byte to match on every frame. The same
seeds and settings therefore give the same melodies as Shoal on the disting NT.

Differences, all deliberate:

- **Values that differ by version.** Gates default to 10 V; GATE reaches 100 %;
  CLOCK out and EOS are 10 V. These are Shoal-for-VCV's values; the NT's are
  5 V and 95 %.
- **Root is a pitch class.** C3 = 0 V, as in Shoal-for-VCV.
- **Effects on a generator.** With an effect on it, a generator plays through
  Ledger's voices, not straight to the jack. The module's test checks that
  this round trip, through a transparent effect, is still frame-identical to
  Shoal.
- **Per-sample timing.** Ledger runs the engine one sample at a time where the
  NT runs it in blocks of four, so a reseed trigger can arm up to three samples
  sooner. Seeds still land at loop origins either way.
- **Evolution is not saved.** A reloaded patch starts each track's lineage fresh
  from its seed, as Shoal does.
