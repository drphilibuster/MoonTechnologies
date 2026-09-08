# Repossession

**FORM 1099-A — acquisition or abandonment of secured property.** 34 HP.

A YouTube sampler. Paste a link into the box on the panel, and the module seizes
the media: it downloads it, decodes the audio to disk and the video into a small
frame store, shows you the picture, and lets you carve the clip into up to eight
regions by dragging on the timeline strip. A clock walks the regions — or the
internal one does, or your finger does; audio and picture move together, because
the picture is drawn from the same playhead the sampler reads with.

Only the eight regions are held in memory, against a budget you set, and the
eight steps share that budget between them.

The "video output" is the panel: the screen, plus a **POS** playhead CV and a
**GATE** while a region plays.

---

## Before anything works: yt-dlp and ffmpeg

Repossession links no media libraries. It shells out to two programs you install
yourself:

| tool | what it does here | get it |
|---|---|---|
| [`yt-dlp`](https://github.com/yt-dlp/yt-dlp) | fetches the media behind a URL | `brew install yt-dlp`, `pipx install yt-dlp`, or a release binary |
| [`ffmpeg`](https://ffmpeg.org/) | decodes it to PCM and to raw frames | `brew install ffmpeg`, your package manager, or a release build |

A local file needs only `ffmpeg`.

The module does not care where the tools live, and it does not assume anyone
has the same machine as the author. It looks in this order, stopping at the
first hit:

1. the folder set under **Tools folder…** in the context menu, if any — a
   plugin-wide setting, remembered in `MoonTechnologies/settings.json` in
   Rack's user directory, so one module's answer serves every patch;
2. a drop folder inside Rack's user directory, `MoonTechnologies/tools/`
   (**Open drop folder for tools** in the menu creates and opens it): copy or
   symlink `yt-dlp` and `ffmpeg` there and nothing else on the system needs
   to change;
3. the places installers put things — on macOS `/opt/homebrew/bin`,
   `/usr/local/bin`, `/opt/local/bin`, `~/.local/bin`, `~/bin`; on Linux
   `/usr/local/bin`, `/usr/bin`, `/snap/bin`, Flatpak's exports, `~/.local/bin`,
   `~/bin`; on Windows WinGet's links folder, scoop's shims, chocolatey's bin,
   `C:\ffmpeg\bin`, `C:\Program Files\ffmpeg\bin` and the per-user Programs
   folders;
4. `PATH`.

That last one is listed last on purpose: Rack launched from the Dock, the
Start menu or a desktop launcher inherits the system `PATH`, not your shell's,
so a tool that works in your terminal may still be invisible here. When `yt-dlp`
has to merge separate video and audio streams it runs `ffmpeg` itself; the
module passes it the `ffmpeg` it found, so that step cannot fail on `PATH`
either. Every lookup, found or not, is written to Rack's `log.txt`.

If a tool is missing the panel says so in plain words and the module sits inert
and silent. Nothing crashes and no patch is harmed.

A tool that is *found* but cannot be *started* is reported differently, because
it is a different problem with a different fix. The usual cause is a wrapper
script whose interpreter has moved — a `pip`-installed `yt-dlp` whose `#!` line
names a Python that a later upgrade removed will sit on `PATH`, executable,
looking perfectly healthy. The panel names the path it tried and says the
interpreter is the likely suspect, rather than reporting an exit code the tool
never got far enough to choose. Running the tool once in a terminal will show
you the same thing in more detail.

**What you import is your responsibility.** This module is a pipe between two
programs already on your machine. Downloading a video, and what you then do with
it, is between you, the site's terms of service and your local copyright law.
Use it on material you have the right to use.

---

## Getting media in

Three ways:

* **Type or paste into the link box** and press Enter, or click **SEIZE**. Any
  URL `yt-dlp` supports works, not just YouTube. A string with no `://` in it is
  treated as a file path.
* **Context menu → Load media file…** opens a file chooser.
* **Reopening a saved patch** restores the media from the cache without touching
  the network, as long as the cache files are still there.

While it works, the light beside **SEIZED ASSETS** pulses and the report column
beside the screen says which stage it is at. If something fails, the light goes
solid and the last line of the tool's own complaint is printed in the report
column.

### What actually runs

```
yt-dlp -f "bv*[height<=360]+ba/b[height<=360]" --no-playlist \
       -o <cache>/%(id)s.%(ext)s --print after_move:filepath --print after_move:title <url>

ffmpeg -v error -nostdin -y -i <file> -t <max> -vn -ac 2 -ar <engine rate> \
       -f f32le <cache>/<id>-<rate>.pcm

ffmpeg -v error -nostdin -y -i <file> -t <max> -an \
       -vf "fps=<r>,scale=<w>:<h>:force_original_aspect_ratio=decrease:force_divisible_by=2,\
            pad=<w>:<h>:(ow-iw)/2:(oh-ih)/2" \
       -c:v mjpeg -q:v 3 -pix_fmt yuvj420p -f mjpeg <cache>/<id>-<w>x<h>@<r>.mjpg
```

A source with no video stream still works — the screen just says `NO PICTURE`.
Sources that are not 16:9 are letterboxed rather than stretched.

### Video quality

**Video quality** in the menu decides what the frames are decoded at:

| Setting | Cache for a 3-minute clip | Download cap |
|---|---|---|
| 160×90, 12 fps (panel only) | ~120 MB | 360p |
| 640×360, 24 fps | ~160 MB | 360p |
| 1280×720, 24 fps | ~340 MB | 720p |

The default is the smallest, because the panel screen is 56 mm wide and nothing
larger shows on it. The other two exist because the frames now leave the module
through **Transmittal** and end up on a projector, where 160 × 90 is not a
picture.

Frames are cached as **concatenated JPEGs** with an index of frame offsets
beside them, not as raw pixels. That is what makes the larger sizes possible at
all: raw 720p at 24 fps is 88 MB *per second*, about 15 GB for a three-minute
clip, where the JPEG stream is 340 MB — 45 times smaller, and still seekable to
any single frame, which is the property that matters when regions jump around on
clock edges. Decoding one 720p frame takes about 4 ms.

Changing the setting re-decodes immediately, and each size is its own cache
file, so switching back to one you have already decoded is instant.

One caveat worth knowing: the **download** is capped when the link is first
imported, and the container is then reused rather than re-fetched. Raising the
quality on something already imported rescales the container you have. To
genuinely get more pixels out of a 360p download, delete that file from the
cache folder and import the link again.

### The cache

Everything lands in `MoonTechnologies/Repossession/` inside your Rack user
directory (**Open cache folder** in the menu goes straight there). Files are
keyed by the video id, so importing the same link twice is instant, and a saved
patch reopens without a download.

The `.pcm` carries the engine sample rate in its name, so changing Rack's sample
rate re-decodes rather than playing back at the wrong speed; the `.mjpg` carries
its size and rate for the same reason. A `.rgba` from before the JPEG format is
still read, so an older patch does not go blank — nothing writes that format any
more. Nothing is ever
deleted automatically: the folder is yours to prune.

**Maximum import length** in the menu caps the decode at 1, 3, 6 or 10 minutes
(default 3). It bounds the disk, not the memory — see below.

---

## Memory: the windows are what is in RAM

The module used to hold the entire decoded clip in memory. Ten minutes of stereo
float at 48 kHz is about 230 MB, and almost none of it was ever played: the
module plays eight *windows* cut out of the clip, and everything between them
was memory spent on audio nobody asked for.

The decode still lands on disk as a `.pcm`, exactly as before. Only the windows
are read into RAM. What the module keeps for the whole clip is its length, its
rate, and a thousand-bucket waveform to draw — about four kilobytes, whatever
the source. Reading a window is a seek and a read of a few megabytes, so
dragging a region's edge re-reads it in milliseconds and editing stays
immediate.

### The budget

**Memory for windows** in the menu sets the ceiling: 16, 32, 64 (default), 128,
256 or 512 MB. The meter in the report column shows what is actually resident —
measured off the buffers, not inferred from what was promised — with one segment
per step in that step's own colour, so the bar says *who* is holding what and
not merely how much is gone. It turns clay when the budget is nearly spent.

The budget buys **seconds**, and how many a megabyte buys depends on the rate:
64 MB at 48 kHz is about 174 seconds of stereo. Those seconds are split eight
ways to start with, so each step gets a window of about 21 s.

### Steps share the pool, and taking room back is not free

This is the part worth knowing before you rely on it:

* Every step starts with an equal share.
* **Trimming** a step returns the difference to a common pool.
* **Growing** a step takes from that pool. When the pool is empty a step simply
  stops growing — drag as far as you like, the window stops where the budget
  says. The start you placed stays put; only the length gives.
* **Disabling** a step (ctrl-click) hands its whole share back to the pool, and
  the other steps can grow into it.
* **Re-arming** a step takes back up to an even share *out of whatever is left*.
  If the others have eaten it, there is nothing to take: the step stays disabled
  and **blinks** to say so. Trim another step and try again.

That refusal is the instrument, not an error message. Eight steps sharing one
budget means a long window somewhere costs a short one elsewhere, and the panel
tells you where the cost landed.

### Filling the slots

**Eight equal spans** in the menu no longer cuts the clip into eight touching
eighths. It gives every slot the largest window the budget will pay for and
spreads them evenly from the start of the clip to the end. The old behaviour
made the windows a function of the clip's length — a ten-minute video produced
eight seventy-five-second regions whether or not there was memory for them. The
budget decides the length now and the clip only decides where they sit, so one
setting behaves the same way on any source. A clip shorter than a single window
is a windfall: every slot gets the whole thing.

---

## The timeline

The strip under the screen is the schedule of what has been seized. The waveform
is drawn from decimated peaks; the white hairline is the playhead; each region
is a coloured span, mint while it is the one playing.

| gesture | what it does |
|---|---|
| drag in empty space | takes a new region into the first free slot, sized as you drag |
| drag a region's left or right edge | trims it |
| drag a region's middle | slides it, keeping its length |
| click the notch at the top of a region | releases it — the slot goes empty |
| click anywhere on a region | selects it, so the LIENS knobs edit it |
| scroll | zooms about the pointer |
| shift-scroll | pans |

A region dragged down to nothing is released rather than left as a sliver — and
"nothing" follows the zoom, so a span you are trimming at 100× is not deleted
for being small on screen.

**Zoom is the strip's own, not Rack's.** Rack's zoom enlarges the whole panel,
which runs out well before a boundary can be placed accurately on a three-minute
clip. Scrolling on the strip magnifies the strip alone; a lime bar along the
bottom edge shows where the view sits in the whole clip, and appears only when
the view is not the whole clip. **Fit the whole clip** in the context menu goes
back.

Each region is drawn in its own colour — a ramp from lime to mint across the
eight slots — and it is the same colour its step button wears, because both come
from one function. A disabled region is drawn in clay and hatched, so it reads
as skipped without relying on colour alone.

**There is no SKIP control because there is nothing to skip:** an empty slot is
a skipped slot. A slot that holds a region you want back later can be *disabled*
instead — ctrl-click (cmd on a Mac) its step button. It keeps its span, its
speed and its gain, and the sequencer passes over it exactly as if it were
empty. Ctrl-click again to arm it.

---

## Controls

### SEIZED ASSETS

Eight lit buttons, one per slot, each wearing its slot's own colour so a button,
its span on the timeline and its line in the report cannot be told apart.

| gesture | what it does |
|---|---|
| tap | selects the region — and, when the transport is **stopped**, fires it |
| tap an empty slot | fills it with the corresponding eighth of the clip |
| hold | loops that step for as long as you hold it, whatever its LOOP says |
| ctrl-click (cmd on a Mac) | disables the step, or arms it again |

Holding is the performance gesture: it overrides the region's own LOOP and hands
it back the moment you let go, so a step can be stuttered under a finger without
changing anything that is saved.

A slot's light is bright while it is playing, half-lit when it is the selected
one, dim when it merely holds a region, and dark when the slot is empty. A
disabled slot burns clay instead of its own colour.

The light beside the caption is the fetch/decode indicator.

### LIENS — the terms charged against the selected region

These seven controls edit **whichever region is selected**. Turning the REGION
knob (or pressing a slot button, or sending REGION CV) loads that region's
values into the knobs; from then on the knobs write back into it.

| control | range | what it does |
|---|---|---|
| **REGION** | 1–8 | which slot the knobs edit. Snaps. |
| **SPEED** | 0.25× – 4× | playback rate of the selected region, in octaves either side of 1× |
| **GAIN** | 0 – 200 % | level of the selected region |
| **LOOP** | on / off | on, the region repeats until the next clock; off, it plays once and holds |
| **REV** | fwd / rev | plays the region backwards |
| **MODE** | 4 positions | how the clock walks the slots: FORWARD, RANDOM, PING-PONG, CV ONLY |
| **TEMPO** | 30 – 300 BPM | the internal clock. Silent while CLOCK is patched |
| **RUN** | 3 positions | the transport: **RUN** up, **STOP** middle, **LATCH** down |

**MODE** in detail:

* **Forward** — each clock advances to the next slot that holds a region, wrapping.
* **Random** — each clock picks a random slot that holds a region.
* **Ping-pong** — walks up through the filled slots and back down.
* **CV only** — the clock never advances the slot; it only re-fires the current
  one. Use it with REGION CV.

**RUN** in detail — three states, because the transport has three things to say:

* **RUN** (up) — the clock walks the slots per MODE and plays them.
* **STOP** (middle) — nothing plays on its own. The step buttons still do:
  tapping one fires it, holding one loops it. This is the module as an
  instrument rather than as a sequencer.
* **LATCH** (down) — whichever step is *selected* loops continuously, and the
  clock never advances. Selecting another step moves the loop to it.

Latching used to be reachable only by accident: patch a clock, stop it, and
whichever step you landed on looped forever. It is a position on a switch now.

### The clock

**TEMPO** runs an internal clock, 30 to 300 BPM, one pulse per beat. Patching
**CLOCK** silences it — an external clock is always the authority, and pulling
the cable hands the tempo knob back. The light beside CLOCK follows whichever
clock is actually in charge, so it is never ambiguous which one you are hearing.

The internal clock keeps running while latched even though nothing advances, so
unlatching lands in time rather than wherever the finger left it.

### COLLECTIONS — the inputs

| jack | expects | what it does |
|---|---|---|
| **CLOCK** | gate/trigger, 0/10 V | advances to the next slot per MODE and fires it. Threshold 1 V with hysteresis |
| **RESET** | trigger | jumps to the first filled slot and fires it |
| **REGION** | 0–10 V | picks the slot outright, 1.25 V per slot. While this is patched the clock re-fires rather than advancing, in every mode |
| **SCAN** | 0–10 V | takes the playhead over: 0 V is the region's start, 10 V its end. The region becomes a window you scrub rather than something played through |
| **FIRE** | trigger | re-fires the current region from its start without advancing |
| **SPEED** | ±5 V, poly | added to the SPEED knob at 0.4 octaves per volt, so ±5 V is ±2 octaves. The sum is clamped to ±3 octaves |
| **GAIN** | ±5 V, poly | scales the region's gain: +5 V doubles it, −5 V silences it |
| **START** | ±5 V, poly | shifts the window's start, ±5 V being a tenth of the clip either way, keeping its length |
| **LENGTH** | ±5 V, poly | scales the window's length from its start: +5 V doubles, −5 V halves |

### The last four are per step

SPEED, GAIN, START and LENGTH are polyphonic, and **channel N addresses slot N**
— one cable carries all eight. A *monophonic* cable in any of them applies to
every step at once, which is what a single LFO into SPEED should obviously do
and what eight separate jacks would have cost a third of the panel to say. A
cable with fewer channels than there are slots leaves the slots above it alone
rather than wrapping.

START and LENGTH never write to the stored region. A modulated window springs
back the moment the cable is pulled, and what the patch saves is what you drew.

If you would rather patch each step by hand, the **SCHEDULE A** expander breaks
all four out into discrete per-step jacks.

### Outputs (the footer)

| jack | signal |
|---|---|
| **OUT L / OUT R** | audio, ±5 V |
| **POS** | the playhead over the whole clip, 0–10 V. Not over the region — over the clip, so it lines up with the timeline |
| **GATE** | 10 V while a region is playing, 0 V when stopped or when a non-looping region has run out |
| **EOR** | a 1 ms trigger each time a region reaches its end, whether it loops or stops |
| **REGION** | the slot currently playing, 1 V per slot: 0 V for slot 1 up to 7 V for slot 8 |
| **STEPS** | per-step audio, 8 channels: whichever step is sounding appears on its own channel, summed to mono, and the rest are silent. One slot plays at a time, so this is the stereo pair routed by which step made it — which is what lets a step have a chain of its own |

---

## What is going on inside

* **Reading** is a four-point cubic (Catmull-Rom) interpolation on a fractional
  read pointer held as a `double`, so it stays exact minutes into a clip.
* **Seams** — loop wraps, region changes, FIRE — are crossfaded. The length is
  in the menu: off, 2 ms, 4 ms (default) or 10 ms. The crossfade is a real
  two-tap one: the old read pointer keeps running under the new one for the
  duration of the fade, so a loop point does not click.
* **Sample rate** is handled by ratio, not by re-decoding: the file's own rate
  divided by the engine's rate is folded into the read step, so the pitch is
  right the instant Rack's rate changes and stays right after the background
  re-decode lands.
* **The module is monophonic.** A sampler with minutes of state in it is not
  something to instantiate sixteen times per cable. The four per-step CV inputs
  and the STEPS output are polyphonic, but that is one cable carrying eight
  *steps*, not eight voices.
* **A window is read on its own thread** and crosses to the audio thread as an
  atomic pointer, the same handover the media uses. A step whose window has not
  arrived is silent rather than stalling — the sequence keeps time and the audio
  appears when it is there. The buffer a crossfade is still reading out of is
  held well past the longest fade before it is freed.
* **Nothing in `process()` allocates, locks or touches a file.** The decoded
  media crosses to the audio thread as a plain atomic pointer, and the previous
  one is only freed once the audio thread has demonstrably moved past the swap.
* **The download can always be cancelled.** Deleting the module raises a flag
  the worker checks between pipe reads, and it kills the child process, so the
  join in the destructor returns in milliseconds rather than waiting out a
  transfer.

---

## Context menu

| item | |
|---|---|
| **Load media file…** | open a local file instead of a URL |
| **Re-import** | run the pipeline again for the current source |
| **Maximum import length** | 1 / 3 / 6 / 10 minutes (default 3) — bounds the decode on disk |
| **Memory for windows** | 16 / 32 / 64 / 128 / 256 / 512 MB (default 64) — bounds RAM |
| **Edge crossfade** | off / 2 ms / 4 ms / 10 ms (default 4 ms) |
| **Eight equal spans** | fill all eight slots with equal eighths of the clip |
| **Release all** | empty every slot |
| **Arm every step** | re-arms anything disabled |
| **Disable the selected step** | the same thing ctrl-click on a step button does |
| **Fit the whole clip** | undoes the timeline zoom |
| **Tools folder…** | a directory searched first for `yt-dlp` and `ffmpeg`; remembered plugin-wide |
| **Clear tools folder** | go back to the automatic search |
| **Open drop folder for tools** | opens `MoonTechnologies/tools/` in Rack's user directory, where the two programs can simply be dropped |
| **Open cache folder** | show the downloads and decoded files in your file manager |

## Saved in the patch

The link text, the media id and source, the title, every region (as fractions of
the clip, so they survive a re-import at another rate, and including whether it
is armed or disabled), the import length, the crossfade length, the memory
budget and the tools folder. A patch saved before steps could be disabled reopens with all of them
armed, which is what it meant. The audio itself is not saved into the
patch — it is reopened from the cache, or re-fetched if the cache has been
cleared.

## Credit

The interaction — a screen, a waveform strip you carve regions out of by hand,
and slots you sequence them from — is the shape of the classic hardware phrase
samplers, above all the **Roland SP-303/SP-404** family's pad-per-chop workflow
and the region editing of a **Serato/Ableton**-style clip strip. Nothing is
copied from any of them; the debt is to the idiom.

The fetching and decoding are entirely the work of
[yt-dlp](https://github.com/yt-dlp/yt-dlp) and [FFmpeg](https://ffmpeg.org/),
which are not bundled and not modified — Repossession only runs them.
