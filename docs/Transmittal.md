# Transmittal

The plugin's video output, for VCV Rack 2. It takes frames from another module
in this plugin and streams them out through `ffmpeg` as a live HLS playlist,
which TouchDesigner reads with a **Video Stream In TOP** — and so does VLC,
ffplay, Safari, or anything else that opens an `.m3u8`.

FORM W-3 is the transmittal that accompanies what is being filed.

Part of the [Moon Technologies](../README.md) plugin.

## Quick start

1. Put a **Repossession** in the rack and seize something, so it has frames.
2. Put a **Transmittal** next to it. Turn **SOURCE** until the read-out names
   the Repossession you want.
3. Pick a **SIZE** and a **RATE**, press **SEND**. The caption light goes mint
   once frames are actually going out.
4. Right-click → **Copy playlist path**, and paste it into a Video Stream In
   TOP.

## Controls

| Control | What it does |
|---|---|
| **SOURCE** | Which module's frames to send. `--` is nothing; every module publishing to the video bus appears after it, named for its own id. |
| **SIZE** | The stream's frame size: 160×90, 320×180, 640×360 or 1280×720. A source publishing something else is scaled to fit. |
| **RATE** | 12, 15, 24 or 30 fps. |
| **SEND** | Start and stop. |
| **SEND** (input) | A gate: high starts, low stops, so a sequencer can hold the transport rather than having to toggle it. |
| **LIVE** (output) | 10 V while frames are actually being written — which is not the same as having been asked to send, and is the difference you want when something is wrong. |

SIZE, RATE and SOURCE are each baked into ffmpeg's command line, so changing any
of them restarts the encoder and the playlist. That is a visible hiccup, and it
is the honest behaviour: raw video carries no header, so a size ffmpeg was not
told about does not fail, it shears the picture.

## What it needs

**ffmpeg on your PATH** — the same one Repossession already needs, found the
same way, so if Repossession works this does too. Nothing is linked or bundled;
the plugin still has no third-party dependencies of its own.

Transmittal asks for the platform hardware encoder first (VideoToolbox on macOS,
Media Foundation on Windows, VAAPI on Linux). An ffmpeg built without it does
not refuse to start — it exits a moment later — so the first failed write inside
the first second drops to `libx264` and tries again, and the read-out says
`streaming (software)` when it has.

## Latency, and why

**Expect two to three seconds.** HLS cuts the stream into one-second segments
and a player sits a segment or two behind the live edge by design. That is fine
for recording and capture, and wrong for performing to.

It is what ships first because it costs nothing: every ffmpeg build has the HLS
muxer, whereas SRT and RTSP need `--enable-libsrt` and a protocol many builds
(including Homebrew's current one) do not have. The fast path is **Syphon** on
macOS and **Spout** on Windows — publishing a GPU texture instead of piping
bytes, sub-frame latency, read by TouchDesigner's Syphon Spout In TOP. When that
lands it goes *alongside* this rather than replacing it: the video bus, the
source selection and this whole panel stay as they are.

## How frames get here

Rack's cables carry one float per sample, which is the wrong shape for a frame
by several orders of magnitude, and the port types are Rack's rather than a
plugin's to extend. Expander messages would work but only between neighbours,
and a video sink has no business being bolted to the side of its source.

So sources publish to a small bus inside the plugin, keyed by module id, and
Transmittal reads by id — which lets the two sit anywhere in the rack, with any
number of sinks on one source. Frames are CPU-side RGBA, because that is what
ffmpeg wants on a pipe and a texture would have to be read back off the GPU to
get it. A future Syphon backend wants exactly the opposite and will publish
textures alongside this, not through it: converting either way is the one thing
that would make both slow.

## Sources

**Repossession** publishes its decoded video: 160×90 at 12 fps. That is a panel
thumbnail rather than a video source — it is what Repossession decodes for its
own screen — so sending it at 1280×720 gives you a very large thumbnail, not a
restored picture. Making Repossession a real video source means re-decoding at a
real size, and its cache is uncompressed frames on disk: 160×90 at 12 fps is
0.69 MB/s, but 720p30 in the same format is 111 MB/s and a three-minute clip is
about 20 GB. That is a change to Repossession's media cache, not to this module.

## Where the playlist goes

`<Rack user dir>/MoonTechnologies/Transmittal/stream-<module id>.m3u8`, with a
few `.ts` segments beside it. The id is in the name so two Transmittals in one
rack write two streams rather than fighting over one file — which is also why
the path is worth copying off the panel rather than guessing. Old segments are deleted as the stream runs, so the
directory does not grow; the playlist is written without an end marker, so
players treat it as live rather than as a finished recording.

## Known limits

- No audio. The stream is video only.
- Rack Pro as a VST is not supported for video by anyone, LZX included, and this
  is no exception.
