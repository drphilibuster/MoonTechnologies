# Squarp Hermod+: research notes for Ledger

Source, read in full (101 pages) on 2026-10-02:
<https://squarp.net/static/HERMODPLUS_manual-e8f40e77eb1796be6fa6f764f49de9b7.pdf>
(manual updated 2026-07-17, firmware hermodplusOS 3.0). [NEW] marks features the manual flags as new.
Some content exists only in images (architecture p9, USB p15, LED legend p13, analyser p20, layouts
p63–64, generator settings screen); those gaps are noted.

Hermod+ is closed source. Ledger reimplements behaviour from this manual; it cannot be bit-exact.

## 1. Concept (p7–9)

- 16 tracks × 16 patterns, 8 effects per track, polyphonic piano roll and automation editor,
  ModMatrix with attenuverters, "polymorphic layouts", 4 assignable CV inputs, CV/Gate and MIDI,
  project swap without stopping.
- 8 CV/Gate voices. One track can drive several voices (poly, velocity CV, aftertouch CV) or be a
  modulation track.
- Modes: **Step** (edit patterns, length 1 step–16 bars, zoom ÷4–×8, random generation),
  **Effects** (add/edit/mute effects, modMatrix), **Track** (mutes, layouts, settings),
  **Seq** (switch sequences live, chain, save/load). Holding a mode button opens it temporarily.

## 2. Hardware

- Screen; one encoder (turn / press / hold / press-turn for fine or fast).
- Buttons: step, effects, track, seq; play, rec; X (next), Y (previous); 8 backlit track switches.
- 16-pad matrix: steps, effect slots, mutes, sequences, or keyboard/XY in On Air mode.
- Outputs: 8 CV/Gate pairs (−5…+5 V; C0–C10, C5 = 0 V), Reset and Clock gate outs.
- Inputs: CV A–D (as CV/Gate pairs AB/CD, mod, gate/clock/reset/trigger; 16-bit for mod).
- MIDI TRS in/out, USB device, USB host (all class compliant, in and out).
- SD card (projects, settings, calibration, custom scales). xp32 expander adds 24 CV/Gate outs.
- Calibration: hold X at boot (outputs, with voltmeter); hold Y at boot (inputs, against voice 1).

## 3. Tracks (p11, 59–71)

- TR1–TR8: CV/Gate tracks (can also send MIDI, support layouts). TR1–8 MIDI: MIDI-only.
- Track edit menu: COPY, PASTE, CLEAR, [NEW] RENAME (12 chars).
- Mutes: PATTERN MUTE (pad; this sequence only) and GLOBAL MUTE (hold track + pad).
  MISC > MUTE BY DEFAULT chooses which a plain press does.

**Layouts (CV tracks).** Press X → number of voices → preset. Only layouts that fit the remaining
voices are offered. Examples: 1 VOICE MONO (default), 1 VOICE MODULATION (CV = mod value, gate = extra
gate; can turn MIDI CC into CV), 2 VOICES MONO + MODULATION, 3 VOICES MONO + VEL + AFTERTOUCH,
3 VOICES MONO + AFTERTOUCH + MODULATION, 3 VOICES POLY, 4 VOICES POLY. Mod/vel/AT voices' gates still
work as triggers.

**Poly allocators.** POLYLRU (default; longest silent), POLY (same note may reuse its voice), FIRST
(in order; extras dropped), CYCLIC, RANDOM.

**Transpose.** TR8 MIDI with TRANSPOSE ON is the transpose leader; tracks with TRANSPOSE ON follow
its pitch in real time.

**Track configuration (hold a track switch).**
- PLAYER: QUANTIZE OFF/1/32…1/4 (non-destructive); APPLY SUSTAIN; TRANSPOSE; MUTE AFFECTS INPUT
  (YES mutes after effects; NO mutes only sequenced data).
- INPUTS: INPUT PORT (--/MIDI/USB DEVICE/USB HOST), INPUT CHANNEL 1–16, INPUT MIDI MOD CC0–119
  (default CC1, recorded as MOD), INPUT NOTE (--/CV/GATE AB/CD), INPUT MOD (--/CV A–D),
  TRIG GENERATOR GATE (each gate generates a new pattern), TRIG GENERATOR MOD,
  TRIG GENERATOR [PITCH, LENGTH, VELOCITY, MOD] (re-randomise one attribute).
- OUTPUTS: OUTPUT PORT (--/MIDI/USB DEVICE/USB HOST/ALL PORTS), OUTPUT CHANNEL,
  DESTINATION MOD/MOD2/MOD3/MOD4 (--/CC0–119/PC), DESTINATION PITCH (PITCHBEND/CC),
  DESTINATION AFTR (AFTERTOUCH/CC).
- CV OUT: GATE RETRIG; ALLOCATOR; OUTPUT STANDARD V/OCTAVE (C0 −5 V … C10 +5 V), 1.2 V/OCTAVE,
  HZ/V (C0 0.0312 V, C5 1 V, C7 4 V, top Eb7 4.75 V); FINETUNE ±100; BEND RANGE 1 st–5 oct;
  CV RANGE MIN/MAX −5…5 V.

## 4. Patterns and step mode (p16–28)

- Project → 16 tracks → 16 patterns. Sequence N = pattern N of every track.
- Length: hold Y + turn, 16 (1 bar) to 256 steps (16 bars); with encoder press, 1/16 increments
  (polymeter). Hold X+Y + turn doubles/halves.
- Zoom (hold X + turn): /4, /2, 2/3, ×1 (16 steps/page), 3/2, ×2, ×4, ×8. Time signatures come from
  length × zoom (12 steps ×1 = 3/4; 12 at 2/3 = 6/8).
- Pages via X/Y; view follows the playhead until you change page.
- **Notes** are polyphonic and grid-free (chords, overlaps, off-grid). Each has pitch (C0–C10),
  length, velocity. Press a pad to add/delete; hold a step and press the encoder to cycle PITCH/LENGTH/
  VELOCITY. Holding several steps edits a range. Holding a step loads its values as defaults.
  Copy: hold a step, press another. Quick length: hold step + track switch (manual gives both 1–8
  steps and ½–7 steps).
- **Row edit:** hold step + turn right; pads show one pitch (drums, chords). step cycles pitches;
  step + X clears the row.
- **Note learn:** played notes/chords become the current pitch/chord and velocity.
- **Analyser lanes:** hold step + track 1–8 → NOTES, PITCHBEND, AFTERTOUCH, SUSTAIN, MOD, MOD2, MOD3,
  MOD4. Each lane supports select, edit, random, copy/paste, erase, rotate, page, zoom, length.
  Pitchbend adds to CV on a MONO track. MOD2–4 have no CV out (except xp32); they serve as modMatrix
  sources and MIDI CC.
- **Mod tracks:** tabs MOD and GATE. MOD sub-parameters VALUE and INTERPOLATION.
  [NEW] Interpolation per lane: OFF, LIN, S-curve, LOG, EXP 0–255 (also PB and AT lanes).
- **Multi-voice tracks:** voice buttons show the piano roll (note voices) or automation lane.
- **Recording:** notes, PB, AT, sustain, mod from MIDI/USB/CV. Overdub (default; starts after the
  first loop); Hard rec (X+rec); Looper (Y+rec; length set when recording stops); Multitrack
  (X+Y+rec, up to 16 tracks). Shortcuts reassignable. CV notes quantised to C0–C10; CV mod 16-bit
  unquantised; one input can't be note and mod.
- **Pattern edit menu:** COPY/PASTE TRACK; per event type copy/paste (also per page); delete per
  type; step+X deletes shown type; step+Y randomises it; hold encoder + turn rotates by the zoom step.
- **Generator:** settings quantize grid, note density, pitch range, length range, velocity range
  (exact ranges only in screenshots). RANDOMIZE makes a new pattern or re-randomises only pitch,
  length or velocity. Mod tracks randomise values and gates separately. Row edit restricts it to the
  row. Gate-triggerable from CV.

## 5. Effects (p29–58)

Up to 8 per track, in series (order matters), duplicates allowed, non-destructive, real time,
polyphonic, processing live input and the pattern player.

- Add: select an empty slot or hold an empty pad. Context menu EDIT/COPY/DELETE/PASTE (across tracks).
- Mute: pad = pattern mute; hold effects + pad = global mute. ENV/LFO send their default when muted.
- **Pattern values:** hold the encoder on a parameter to toggle between the global value and a
  per-pattern value.

**ModMatrix (p35–37).** Y in effects mode; 4 slots per track.
- Sources: CV A–D, MIDI CC1–119, recorded MOD/MOD2/MOD3/MOD4.
- Destination: any parameter of any effect on the track.
- Additive offset (the knob still works). Per slot: ATTENUVERTER −100…100%, POLARITY bipolar or
  increase-only, OFFSET. Screen shows full range, working range and a live dot. CV defaults ±5 V.

### Effect reference

**ARPEGGIATOR.** STYLE (ORDER, UP, …, RANDOM, …, SILENCE 1 OUT OF 3; full list only in a screenshot);
RATE 1/1–1/64; OCTAVE −5…5 (negative = downward); GATE 0–200% (max depends on rate); HUMANIZE 0–100%
(velocity and gate); RE-TRIG --/NOTE/8BARS…1BAR/1/2…1/16; REPEAT --/x1…x16 (cycles before stopping).

**BERNOULLI** [NEW]. Routes each event to A or B by PROBABILITY 0–100% (0 = always A, 100 = always B);
OUT A PORT/CH, OUT B PORT/CH; port -- disables a path.

**CHANCE.** CHANCE 0–100%; LOT --/BAR/BEAT/1/8/1/16/1/24/1/32 (one roll per window);
CHANCE = VELOCITY; SYNC --/BAR/BEAT/1/8/1/12/1/16/1/24/1/32 (notes exactly on this grid use)
SYNC CHANCE 0–100%.

**ECHO.** TIME 1/1–1/64; REPEAT 0–16; FADE VELOCITY --/LIN/EXP/LOG; FADE GATE --/LIN/EXP/LOG;
FADE PITCH UP --/1…60; FADE PITCH DOWN --/−1…−60 (both on → repeats alternate).

**ENVELOPE.** AHDSR: ATTACK, HOLD, DECAY, SUSTAIN LEVEL, RELEASE 0–100%; DEPTH 0–100%; OFFSET ±100%;
CURVE A/D/R LOG −100…EXP 100; SIDECHAIN ALL/C0…B9 (only that note retriggers; with dest VELOCITY the
sidechain note itself is unaffected → ducking); MODE GATE ON / TRIG / WAIT AHD; DESTINATION MOD,
MOD2–4, PITCHBEND, AFTERTOUCH, CC0–119; RETRIG ATTACK FROM ZERO / EQUAL TIME / EQUAL SLOPE. Default
value (on mute/delete/dest change) = OFFSET. Not audio rate.

**EUCLID** [NEW]. NOTE INPUT/C0…C10 (INPUT uses held notes; a fixed note generates its own notes and
passes input through → stackable polyrhythms); STEPS 1–32; FILLS 1–32; RATE 1/1–1/64; GATE LENGTH
0–100%; ROTATE 0–31; RESET --/NOTE/BAR/1/2…1/16/8 BARS…1 BAR; MOD AMOUNT 0–100% (alternately
lengthens/shortens); MOD SPEED 1/64…128/1.

**FILTER.** NOTE VAL MIN/MAX C0–C10; MOD VAL MIN/MAX 0–127. MIN < MAX passes the range; MIN > MAX blocks it.

**GLIDE** [NEW]. TYPE NONE/LINEAR/EXPONENTIAL/SMOOTH; TIME 0–2500 ms in 10 ms steps, then 3, 5, 10,
30, 60 s.

**HARMONIZER.** ORIGIN --/ON; HARMO 1–4 −24…--…+24 semitones.

**HOLD** [NEW]. MODE HOLD (toggle; new note added, held note removed) / RELATCH (stack while any key
is down; after full release the next note starts a new chord); MAX NOTES NO LIMIT/1–12 (drops oldest);
RESET action.

**LFO.** WAVEFORM SINE/TRIANGLE/RAMP/SQUARE/RAND; DESTINATION MOD, MOD2–4, PB, AT, CC0–119; SYNC;
SYNC RATE 1/64…128/1; UNSYNC RATE 0–100% (≈0.1 Hz–1 kHz, exponential); RANGE ±100% (negative
inverts); OFFSET ±100%; PHASE ±180°; MUTE W/ TRACK (default ON). Incoming messages on the destination
add to the offset. Default = current centre.

**MOD TO NOTE.** SAMPLE RATE SAMPLE/HOLD (on each note-on; gate follows that note) or 1/64…1/1
(clocked); NOTE MIN/MAX C0–C10; PASS MOD. Monophonic; only while playing.

**MIDI OUTPUT** [NEW]. Sends from any point in the chain: MIDI CHANNEL, MIDI PORT, SEND NOTES/CC/
PITCHBEND/AFTERTOUCH, VELOCITY SCALE 0–200%. Typical: before an arp so a synth gets dry notes while
CV gets the processed ones.

**NOTE TO CC.** CC DESTINATION 0–119; CC VALUE NOTE VELOCITY / NOTE ON/OFF (127 on, reset on off);
CC DEFAULT VALUE --/0–127; PASS NOTES.

**RANDOMIZER.** NOTE− / NOTE+ 0–12 st; OCTAVE− / OCTAVE+ 0–5; VELO− / VELO+ 0–100%; LENGTH 0–100%
(delays note-offs up to a whole note); CHANCE 0–100% (per note, of applying any of it).

**RATCHET.** RATE 1/64–1/1; GATE LENGTH 0–100%; MOD AMOUNT 0–100%; MOD SPEED 1/64…128/1.

**REGISTER** [NEW] (Turing machine). Each clock the register shifts and feeds its top bit back, with
a chance of flipping; register value → pitch; a note plays when the top bit is set, or every step
with STEADY. Makes its own notes; input passes through. SEED 0–255 (re-seeded on unmute); RATE
1/32…8/1; BITS 2–16 (default 8); CHAOS LOCKED(0%)/1–99%/LOCKED(100%; 50% = most random);
CENTER C-2…G8; SPAN ±0…±60 st; GATE 1–100%; STEADY.

**SCALE.** COLOR (family); SCALE (71 built-in + 16 custom); KEY C…B; STICK UP/DOWN/FILTER/ALGO1/ALGO2
(text describes Down, Up, Filter (drop), Odd up, Odd down); TRSP ±36 (before quantising).

**SWING.** GROOVE 0…50…100% (50 = none; >50 delays off-grid notes; <50 delays on-grid ones);
SYNC 1/1…1/24 (1/16 classic); ACCENT 0–100% (<50 off-beats, >50 on-beats); HUMAN 0–100%.

## 6. MIDI

- Ports: TRS, USB device, USB host; all in and out.
- Per track: input port/channel; output port (or ALL)/channel; MOD1–4 → CC0–119 or Program Change;
  pitch → PB or CC; aftertouch → AT or CC.
- MIDI→CV: notes on CV tracks drive CV/Gate; CC drives a MOD voice; velocity/AT via layouts;
  pitchbend adds to CV with bend range up to ±5 oct.
- Global MIDI INPUT: ACTIVE TRACK PORT/CHANNEL (route to the selected track); accept/ignore NOTES,
  CC, PITCHBEND, AFTERTOUCH; PROG CHANGE SEQ channel (Program Change selects SE1–16).
- MIDI THRU matrix MIDI/HOST/DEVICE → MIDI/HOST/DEVICE.
- Clock: in from MIDI/DEVICE/HOST at 24 PPQN; out per port --/CLOCK+TRANSPORT/ONLY CLOCK/
  ONLY TRANSPORT; CLOCK ON STOP.
- Not documented: MPE, MIDI learn, Song Position Pointer, NRPN, poly aftertouch.

## 7. CV inputs

- Uses: note pairs AB/CD, mod CV (16-bit), modMatrix sources, clock/reset/play-enable/rec-enable/
  BPM, generator triggers, tuner.
- Settings: CV IN DETECTION (record only on change); ACTIVE TRACK NOTE/MOD; per-input RANGE −5…+5 /
  0…+5 / −2.5…+2.5 / −1…+1 V; GATE CAPTURE 0–20 ms; [NEW] CV PITCH OFFSET ±60 st.
- Monitors: CV IN (volts, nearest note, fine offset), MIDI IN, jitter (BPM over 48 clocks, jitter
  %, histogram, min/max interval). Tuner on CV A–D, E0–E6, cents and confidence.

## 8. Clock and sync

- BPM menu (hold play or rec; the manual says both), tap tempo with rec.
- SYNC INPUT: CLOCK SOURCE INTERNAL/MIDI/DEVICE/HOST/CV A–D; CV RESET --/CV A–D/AUTO (gate stops
  and resets effects; play resumes on next clock; clock+reset together restart at once; AUTO starts
  on clock and resets after 2 s without one); CV PLAY ENABLE; CV REC ENABLE; CV BPM; CV CLOCK DIV
  1/96…1/1.
- SYNC OUTPUT: per-port MIDI clock modes; CLOCK ON STOP; RESET OUT MODE RESET ON PLAY / ON STOP /
  SEQ CHANGE / RUN (high while playing) / STOP / 1/96…128 BARS (second clock); CLOCK OUT RATE
  1/96…128 BARS.
- No global swing (SWING effect only); time signature only via length × zoom.

## 9. Sequences and performance (p72–79)

- Launch: pad in seq mode, or seq + pad from anywhere.
- SYNC (hold X + turn): MODULO (LCM of all pattern lengths), 1…16 + X BARS (step precision with
  encoder press), SHORTEST TRACK, LONGEST TRACK, SAME AS TR X.
- RUN (hold Y + turn): SYNC/INSTANT, RESTART/FREE.
- Chain/song: hold X + pads to queue; short X clears; loops; [NEW] jump to any step while stopped.
- Seq edit: COPY, PASTE, CLEAR; [NEW] copy across projects. Duplicate: hold active pad + empty pad.
- On Air (hold step + rec): note tracks → chromatic keyboard on pads (X/Y octave), records;
  mod tracks → XY controller.
- Mutes for tracks and effects, each at pattern and global level.

## 10. Save/load and settings

- PROJECT SAVE LOAD: SAVE, SAVE AS, NEW, LOAD (and DELETE). Background load; swap at bar end
  (Y takes the new BPM, X keeps the current one). AUTOLOAD at boot (hold step to skip).
- MISC: LED BRIGHTNESS, HOLD TIME, SMART OVERDUB (replace notes only once new notes arrive) + TIME,
  FOLLOW PLAYHEAD, MUTE BY DEFAULT, POPUP TRACK SELECT.
- REC: MULTITRACK, LOOPER, HARD REC, PUNCH IN (start on first incoming note), SHORTCUT X/Y/X+Y.
- CUSTOM SCALES [NEW]: 16, 12-note editor, `.json` in `/HERMOD/scales/`, global.
- INFO: OS, CPU, RAM per event class.

## 11. Shortcuts

Any mode: track switch selects; seq+pad launches; step+rec On Air; seq+play restart; hold a mode
button for temporary access. Step mode: X/Y/X+Y + rec record modes; encoder hold + turn rotate;
X + turn zoom; Y + turn length; X+Y + turn double/halve; pad + track switch quick length; step + turn
row edit; step + track 1–8 lanes; step + X erase; step + Y randomise. Effects: hold empty pad add;
pad pattern mute; effects + pad global mute; Y modMatrix. Track: X layout; Y settings; hold track
switch config. Seq: X + pads chain; X + turn sync; Y + turn run mode; active + empty pad duplicate.

## 12. Ideas worth borrowing

- Polymorphic layouts (one track over N voices; every voice still gives a gate).
- Gate-triggered regeneration per attribute.
- Per-pattern effect parameter values; two-level mutes for tracks and effects.
- ModMatrix as an additive offset from CV, CC or recorded MOD lanes.
- Grid-free polyphonic piano roll with interpolated automation.
- Transpose leader track.
- MIDI OUTPUT and BERNOULLI as mid-chain taps.
- Launch quantise by LCM, shortest/longest, or a chosen track.
- Looper recording that sets the length; punch-in; smart overdub.
- Tuner, CV/MIDI monitors, jitter monitor.
- V/oct, 1.2 V/oct and Hz/V outputs with per-voice clamping.

## 13. Manual inconsistencies

BPM menu (hold play p13 vs hold rec p98); quick length (1–8 vs ½–7 steps); STICK names (ALGO1/2 vs
Odd up/down); DESTINATION AFTR printed ending "CH16" (p69); modMatrix CC sources start at CC1 while
effect destinations start at CC0.
