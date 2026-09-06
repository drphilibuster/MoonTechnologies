# Repossession

**FORM 1099-A — acquisition or abandonment of secured property.** 34 HP.

A YouTube sampler. Paste a link into the box on the panel, and the module seizes
the media: it downloads it, decodes the audio into RAM and the video into a
small frame store, shows you the picture, and lets you carve the clip into up to
eight regions by dragging on the timeline strip. A clock walks the regions;
audio and picture move together, because the picture is drawn from the same
playhead the sampler reads with.

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
       -vf "fps=12,scale=160:90" -pix_fmt rgba -f rawvideo <cache>/<id>.rgba
```

360p is deliberate: the screen is 160 × 90, and the download is the slow part.
A source with no video stream still works — the screen just says `NO PICTURE`.

### The cache

Everything lands in `MoonTechnologies/Repossession/` inside your Rack user
directory (**Open cache folder** in the menu goes straight there). Files are
keyed by the video id, so importing the same link twice is instant, and a saved
patch reopens without a download.

The `.pcm` carries the engine sample rate in its name, so changing Rack's sample
rate re-decodes rather than playing back at the wrong speed. Nothing is ever
deleted automatically: the folder is yours to prune.

**Maximum import length** in the menu caps the decode at 1, 3, 6 or 10 minutes
(default 3). It is a memory control as much as a time one — the audio lives in
RAM, and ten minutes of stereo float at 48 kHz is about 230 MB.

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

A region dragged down to nothing is released rather than left as a sliver.

**There is no SKIP control because there is nothing to skip:** an empty slot is
a skipped slot. The sequencer only visits slots that hold a region.

---

## Controls

### SEIZED ASSETS

Eight lit buttons, one per slot. Pressing one selects that region. Pressing an
empty one fills it with the corresponding eighth of the clip, so there is
something there to shape. A slot's light is bright while it is playing, half-lit
when it is the selected one, dim when it merely holds a region, and dark when
the slot is empty.

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
| **RUN** | latch | the primary action. Stops and starts playback; lit while running |

**MODE** in detail:

* **Forward** — each clock advances to the next slot that holds a region, wrapping.
* **Random** — each clock picks a random slot that holds a region.
* **Ping-pong** — walks up through the filled slots and back down.
* **CV only** — the clock never advances the slot; it only re-fires the current
  one. Use it with REGION CV.

### COLLECTIONS — the inputs

| jack | expects | what it does |
|---|---|---|
| **CLOCK** | gate/trigger, 0/10 V | advances to the next slot per MODE and fires it. Threshold 1 V with hysteresis |
| **RESET** | trigger | jumps to the first filled slot and fires it |
| **REGION** | 0–10 V | picks the slot outright, 1.25 V per slot. While this is patched the clock re-fires rather than advancing, in every mode |
| **SCAN** | 0–10 V | takes the playhead over: 0 V is the region's start, 10 V its end. The region becomes a window you scrub rather than something played through |
| **SPEED** | ±5 V | added to the SPEED knob at 0.4 octaves per volt, so ±5 V is ±2 octaves. The sum is clamped to ±3 octaves |
| **FIRE** | trigger | re-fires the current region from its start without advancing |

### Outputs (the footer)

| jack | signal |
|---|---|
| **OUT L / OUT R** | audio, ±5 V |
| **POS** | the playhead over the whole clip, 0–10 V. Not over the region — over the clip, so it lines up with the timeline |
| **GATE** | 10 V while a region is playing, 0 V when stopped or when a non-looping region has run out |
| **EOR** | a 1 ms trigger each time a region reaches its end, whether it loops or stops |
| **REGION** | the slot currently playing, 1 V per slot: 0 V for slot 1 up to 7 V for slot 8 |

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
  something to instantiate sixteen times per cable.
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
| **Maximum import length** | 1 / 3 / 6 / 10 minutes (default 3) |
| **Edge crossfade** | off / 2 ms / 4 ms / 10 ms (default 4 ms) |
| **Eight equal spans** | fill all eight slots with equal eighths of the clip |
| **Release all** | empty every slot |
| **Tools folder…** | a directory searched first for `yt-dlp` and `ffmpeg`; remembered plugin-wide |
| **Clear tools folder** | go back to the automatic search |
| **Open drop folder for tools** | opens `MoonTechnologies/tools/` in Rack's user directory, where the two programs can simply be dropped |
| **Open cache folder** | show the downloads and decoded files in your file manager |

## Saved in the patch

The link text, the media id and source, the title, every region (as fractions of
the clip, so they survive a re-import at another rate), the import length, the
crossfade length and the tools folder. The audio itself is not saved into the
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
