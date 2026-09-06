# Retroactive

A windowed sample-permutation effect for VCV Rack 2.

Take a group of consecutive samples — a *window* — and emit them in a different order,
while leaving the windows themselves in their original time order. The macro-timeline
(melody, rhythm, phrasing) plays forward while every micro-chunk is rearranged. Short
windows make it a timbral effect; long windows give the familiar "reversed but still
moving forward" sound.

**And then there is OVERDRAFT, which is the best thing in the module and was not
designed in.** Push FADE past half the sub-block — short window, high SUBDIV — and the
crossfade clamp starts doing all the work; the OVERDRAFT light comes on to say so.
What comes out stops behaving like a delay. It plucks and rings: Karplus–Strong-ish
pitched tones, delays that never quite settle, comb-like resonances that move on their
own, and past a point plain chaos.

The ingredients for that are genuinely all present — the sub-block period `B` sets a
pitch, and the overlapping crossfade averages successive reads the way the filter in a
plucked-string loop does — but it is emergent rather than intended, and it is not
predictable. Both TIME and SUBDIV change `B`, and `B` is what the clamp bites on, so
small moves in either can land somewhere quite different. There is no way to read the
right settings off the panel; you find them.

Treat it as a second instrument hiding inside the first rather than as a
misconfiguration. [Overdraft](#overdraft) below has the mechanics, and — importantly —
which modes actually respond to it: in Identity and Reverse the light comes on and
nothing happens at all.

Part of the [Moon Technologies](../README.md) plugin.

A single sample is one number, so it cannot be reversed on its own. What gets reversed
is the *order of samples within a window* — and once you say it that way, the general
case is obvious:

> Every variant of this effect is a permutation `f(i)` of sample indices within a window.

So there is one engine — a window read map — and every mode is a different map:

```
f(i) = map[s] * B + (rev[s] ? (B - 1 - r) : r)      s = i / B,  r = i % B,  B = N / S
```

| Mode | `map[s]` | `rev[s]` | Bijective |
|---|---|---|---|
| Identity | `s` | no | yes |
| Reverse | `S-1-s` | yes | yes |
| Block reverse | `S-1-s` | no | yes |
| Block internal | `s` | yes | yes |
| Block shuffle | Fisher–Yates of `[0,S)` | no | yes |
| Pairwise swap | `s^1` | no | yes |
| Stutter | `s0 + (s % R)` | no | **no** |
| Scatter | uniform `[0,S)` | coin flip | **no** |

## SUBDIV does nothing in Identity and Reverse

This surprises people, so the panel says so: the display shows `SUB --` in those two
modes. Substituting Reverse's `map[s] = S-1-s` and `rev[s] = true` into `f(i)` gives
exactly `f(i) = N-1-i` **for any S** — whole-window reverse *is* block-reverse composed
with internal-reverse, so subdividing cannot change it. Identity is `f(i) = i` for the
same reason. Every other mode breaks that composition apart and responds to SUBDIV.

## Latency

The output is **inherently `N + L` samples late** (window length plus declick fade), in
every mode including Identity — special-casing Identity to zero would re-time the output
whenever you switched modes. The panel display shows the figure.

**Rack 2 has no latency-reporting API**, so the panel and this file are the only
disclosure. If you parallel a dry path around Retroactive, it will arrive `N + L` early.
The context menu has *Dry path compensation*, which delays the module's own dry signal
to match; it does not help an external dry path.

## Controls

| Control | Range | Default |
|---|---|---|
| TIME | 0.01 – 4.0 s, logarithmic | 0.25 s |
| CLK DIV | ×1/16 … ×16 | ×1 |
| MODE | the eight above | Reverse |
| SUBDIV | 1, 2, 4, 8, 16, 32, 64 | 1 |
| FADE | 0 – 20 ms declick | 3 ms |
| MIX | 0 – 1 (insert effect) | 1.0 |
| CHAR | Crossfade / Overlap | Crossfade |
| FREEZE | button + gate input | off |

TIME, MODE, SUBDIV and MIX have attenuverted CV. FADE and CLK DIV do not.

- **IN R normals to IN L**, so a mono input gives dual mono. Polyphonic cables are
  summed rather than silently truncated to channel 1. The module is deliberately
  **not polyphonic**: it is two lanes sharing one schedule, so stereo is phase-locked
  by construction.
- **CLOCK** sets the window length to `period × CLK DIV`. One edge cannot give a period,
  so the module free-runs from TIME until the second edge. If the cable is *pulled* it
  falls back to TIME immediately; if the cable stays but the edges *stop*, it keeps
  free-running at the last period and the clock light dims — snapping back to the knob
  mid-performance is jarring.
- **RESET** forces a window boundary and reseeds the RNG, so Shuffle and Scatter patterns
  repeat against a sequencer. The seed is saved in the patch.
- **FREEZE** stops the write head, so the current window loops.

### Lights

- **Yellow, ASSESSMENT caption light** — window phase; it flashes at each boundary.
- **Green, WITHHOLDING caption light** — clock locked; dim means the edges have stopped.
- **White, in the FREEZE button** — frozen.
- **OVERDRAFT, below the display** — you are asking for more fade than the sub-block can
  pay for. Not a fault light; see below.

## Overdraft

`L` is clamped to `min(FADE, B/2, N/2)`, so a fade can never reach the next seam. Once
FADE exceeds half the sub-block — short window, high SUBDIV — the clamp is doing all the
work and the OVERDRAFT light comes on. It is a named region of the control space, not an
error. The behaviour there was found rather than designed — see the top of this page —
and the light exists so it can be found again, not to warn you off it.

**How much you actually hear depends on the mode's seam count.** Fades only fire at genuine
discontinuities, and consecutive blocks are often contiguous in the source:

| Mode | Seams per window | Overdraft is… |
|---|---|---|
| Identity | 0 | inaudible — the light is on, nothing changes |
| Reverse | 1 (window boundary only) | barely audible, for the same reason |
| everything else | up to `S` | clearly audible; smears toward a drone |

Reverse reads the whole window as one contiguous backwards run — `f(i) = N-1-i` — so there
are no internal sub-block seams to fade no matter how high SUBDIV goes. That is why the
light can be lit in Reverse with no change in the sound.

Practical floor for the modes that do seam: the fade stops eating the block at `B ≥ 4L`,
about 12 ms at 48 kHz, so SUBDIV 16 wants a window of roughly 190 ms or more for full punch.

## Building

Retroactive ships inside the Moon Technologies plugin, so it is built with the
rest of the family — see [BUILDING.md](BUILDING.md).

## The panel

The panel is generated. Edit `tools/panels/Retroactive.py` — the spec — never the
SVG, and never `src/PanelTheme.hpp` or `src/Retroactive/Panel.hpp`, all of which are
written from it along with both previews:

```bash
make panel-Retroactive      # artwork, the two headers, the browser mock
make preview-Retroactive    # ... and open the mock
make vcv-preview             # ... build, then render every panel through VCV Rack
```

`make panel` does the same for all three panels at once, which is what you want
after a change to `panelkit/`.

The palette, the shared hardware, the layout rules and the two constraints Rack's
renderer imposes are documented once, in [`../panelkit/README.md`](../panelkit/README.md),
which is also where the other two panels in the plugin get theirs. There is no
per-module copy of any of it.

Retroactive's panel runs at panelkit's `compact` density: six rows of controls
and a read-out at 14 HP leaves no room for the regular scale.

One constraint is Retroactive's alone rather than the family's: display numerals
use DSEG7, and characters missing from its `cmap` do *not* render as tofu — Rack
chains NotoSansJP as a fallback onto every font, so a stray `+` or `%` would
silently come out in a proportional Japanese sans. Keep segment strings to
`0-9 . : -`.

The OVERDRAFT light sits between TIME and CLK DIV, with a lime trace running to
the three controls that actually determine it — `FADE > B/2` where
`B = TIME / SUBDIV` — so the panel states the condition rather than just
reporting it. The trace is declared as a function of the solved layout in
`tools/panels/Retroactive.py` and breaks itself around any label it crosses, so it stays
correct when a row moves.

## Tests

The DSP core (`src/Retroactive/dsp/WindowPermuter.hpp`) is Rack-free C++11 with no allocation on the
audio thread, so it builds and runs standalone:

```bash
cd tests/Retroactive && make && ./test_permuter   # 277 checks under ASan + UBSan
make render_permuter && ./render_permuter --render out/
```

Under test the ring is filled with a sentinel (`-12345`) instead of zero: ASan catches
out-of-bounds but not uninitialised reads, and MSan is unavailable on macOS, so the
sentinel is what actually guards the crossfade's forward-walking tail.

`--render` writes `out_<mode>_<subdiv>.wav` for every mode plus `sweep.wav`, which runs
the window from 20 ms to 2 s over 30 s.

## Licence

GPL-3.0-or-later, with the rest of the plugin. See [../LICENSE](../LICENSE).
