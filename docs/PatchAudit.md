# PatchAudit

A VCV Rack 2 module that browses [Patchstorage](https://patchstorage.com) from
inside Rack, audits patches against the modules you actually have, and imports
them into your rack -- without leaving the app.

26 HP, in the family's panel language: a green form you fill in, and a ruled
table of results you get back.

Part of the [Moon Technologies](../README.md) plugin.

## What it does

- **Search** the ~9,200 VCV Rack patches on Patchstorage as you type
  (350 ms debounce), filtered by any of the ten Patchstorage categories and
  sorted by newest, downloads, likes, views or title.
- **Import into rack** downloads the patch, unpacks it, and pastes its modules
  and cables into your current rack, centred in the viewport, as a single
  undoable action. Your existing rack is untouched, and one Ctrl/Cmd-Z removes
  the whole import. Module ids and per-module patch storage are carried across,
  so mapped knobs still point at what they were mapped to -- see below.
- **Before importing**, it scans the patch for modules this Rack can't
  instantiate, resolves the plugin names from the VCV Library, and offers a
  per-plugin link into the library (flagging premium plugins and ones that
  aren't in the library at all). Rack imports everything it can and skips the
  rest.
- **Save to disk** keeps a copy of a patch you like, defaulting to Rack's own
  patches folder.
- **Audit against your installed modules.** Off by default; in the Sort menu.
  "Show what's missing" badges every result -- `ALL 32` in mint when you have all
  32 of its modules, `-3 OF 20` in clay when you don't. "Playable first" floats
  the runnable patches to the top of the
  page and orders the rest by how few modules they're missing. "Only patches I
  can play" hides the rest entirely. The status line keeps a running count.
- **Favourites** are stored in the module, so the favourites view works with no
  network at all. So does any page you've already visited, from the disk cache.

## Pre-2.0 patches

There is no version gate here, deliberately. **Rack 2 opens Rack v1 patches.**
`patch::Manager::load` copies a `.vcv` that doesn't start with the Zstandard
magic number straight to `patch.json` (`Rack/src/patch.cpp:306-310`), and every
loader below it carries explicit legacy branches back to v0.3: `Module::fromJson`
maps v1's `"disabled"` to bypass and v0.6's `"paramId"` to `"id"`,
`plugin::getModelFallback` remaps renamed slugs, `RackWidget::fromJson` knows
v0.6 spelled cables `"wires"` and that <=v0.5 wrote positions in pixels.
`Manager::fromJson` only logs the version difference.

Verified rather than inferred, headlessly against Rack 2.6.6: a 1.1.6 patch
loaded with all its modules and cables and the legacy `Core`/`AudioInterface`
slug resolving to VCV Audio 8; a 0.6.2 patch with `"wires"` and `"paramId"`
loaded clean; and a 0.4.0 patch with no module ids, pixel positions and `"wires"`
loaded all three modules and both cables with no warnings.

`ps::applyImport` goes through the same `modelFromJson` / `Module::fromJson` /
`Cable::fromJson` that Rack uses, so it inherits all of that. What it has to know
itself, because it reimplements paste rather than calling it, is the handful of
things Rack's loader special-cases:

- `"pos"` is in pixels for <=v0.5 and in grid units after, from
  `ps::eraFromVersion` -- the same version set `RackWidget::fromJson` calls
  `legacyV05`, matched deliberately rather than approximated.
- The cable array is called `"wires"` before v0.6.
- **Before 1.0 a module carries no `"id"` at all**: the id *is* its index in the
  `"modules"` array, which is what that era's `"wires"` reference
  (`Rack/src/engine/Engine.cpp:1308-1311`). `ps::sourceModuleId` falls back to
  the index for exactly this reason, and the resolved id is written back into the
  JSON unconditionally -- leave it unset and `Module::fromJson` leaves the module
  at `id < 0`, `Engine::addModule` hands it a random id
  (`Engine.cpp:767-771`), and every cable in the patch loses its endpoints.

What actually stops an old patch running is its plugins never having been ported
to v2 -- which is a missing-module count like any other, and exactly what the
audit already measures.

## Why import keeps the original module ids

`ps::applyImport` reimplements `RackWidget::pasteJsonAction` rather than calling
it, for one reason.

Rack's paste calls `Module::jsonStripIds()` and lets the Engine hand out fresh
random ids. Any module that references another module *by id* inside its own data
blob then points at ids that no longer exist, and the patch arrives with every
such reference dead. That covers a lot of what makes a patch worth importing:
MindMeld PatchMaster's `maps`, MindMeld ShapeMaster, stoermelder MIDI-CAT and
uMAP, anything built on `ParamHandle`. A TB-303 patch with four PatchMasters
imports seventeen mappings, and Rack's paste loses all seventeen.

Nothing actually requires the renumbering:

- `Module::fromJson` sets the id only when it is still unset
  (`Rack/src/engine/Module.cpp:174-180`), so leaving `"id"` in the JSON preserves it.
- `Engine::addModule` keeps whatever id the module has unless it collides with one
  already in the rack (`Rack/src/engine/Engine.cpp:767-771`) -- and as it adds the
  module it re-points any existing `ParamHandle` whose `moduleId` matches. So
  mappings reconnect on their own.
- Ids are 53-bit random, so colliding with the open rack is vanishingly unlikely.
  When one does collide, only that module is renumbered and the status line says
  how many -- which is also what happens if you import the same patch twice, the
  second copy's internal mappings being the unavoidable casualty.

Keeping the ids also makes it possible to carry over each module's patch storage
directory from the archive, which Rack's paste drops on the floor.

## About the audit

Patchstorage's API says nothing about what a patch contains -- there is no module
list, no dependency field, nothing. The only way to know whether you can run a
patch is to fetch the patch and read it. So the audit genuinely downloads every
result on the page, which is why it is off by default and labelled
"downloads each result" in the menu.

What makes it tolerable: it runs on the download lane one patch at a time, most
VCV patches are a few KB, and every result is cached twice -- the archive in the
patch cache and the extracted module list in the JSON cache, both keyed by the
patch's `updated_at`. Re-visiting a page you've already audited costs nothing,
works offline, and pauses for nothing: the inter-request gap is only paid after a
patch that actually went to the network. Rows fill in progressively as each patch
lands, so the list is usable while the scan is still running.

### Big uploads are read, not downloaded

An upload over 16 MB is almost always a zip shipping the samples the patch loads.
"Building an Ambient Drone in VCV Rack" is 31,079,280 bytes, of which 31,073,369
are one WAV; the patch itself is a 50 KB `.vcv` compressed to **5,595 bytes**,
and it sits first in the archive.

Downloading 31 MB to read 5 KB is not worth it, so the audit doesn't. A byte
range would be the obvious fix and is not available: Patchstorage's download
endpoint is a WordPress handler on Apache that ignores `Range` outright --
answering `200` with no `Accept-Ranges`, no `Content-Range`, and the entire file
however narrow a range you ask for. Verified against the live endpoint.

What works instead is hanging up. A zip's local headers precede their data and
carry each entry's name and compressed size, so a reader walking the stream from
the front recognises the `.vcv` as it arrives, with no central directory. Once it
has gone past, the connection is dropped. For that patch the whole module list
comes out of the **first 16 KB** -- one curl chunk, 1,897x less than the upload.

`ps::zipFindPatchInPrefix` is the scanner and `ps::http` the transport, which
goes to libcurl directly because `network::requestDownload` writes to a `FILE*`
and cannot be told to stop. The curl symbols are resolved with `dlsym`, not
linked: an unresolved symbol would stop the plugin loading at all, and this is an
optimisation the audit can live without. `ps::http::available()` reports that,
and the caller falls back to badging the row with its size.

It gives up and shows the size when the patch is behind the bulk of the archive
(2 MB of streaming, then stop), when the zip was written as a stream so its entry
sizes trail the data, and when the upload isn't a zip at all. The bytes it does
pull are never cached as an archive -- only the extracted module list is -- so a
30 MB upload costs the cache nothing.

The worker only extracts plugin/model slugs. Deciding whether a model is
*available* means walking `plugin::plugins`, which happens on the UI thread when
the results are drained -- see `src/PatchAudit/ps/ModuleScan.hpp` for why.

## What "unavailable" actually means

Rack tells you a module is missing. The useful question is what you can *do*
about it, and answering that means asking the VCV Library -- which turned out to
be where most of this module's wrong answers came from.

**Slugs are normalized first.** Rack <=1.x let a plugin call itself
`Aepelzens Modules` or `Autodafe-Drum Kit`, spaces and all, and patches from that
era still carry those strings. Rack runs `plugin::normalizeSlug` before every
lookup; this module didn't, so `Autodafe-Drum Kit` never matched
`Autodafe-DrumKit` -- which is in the library, available, and built for this
machine. `ps::resolve::moduleResolves` now normalizes and then calls
`plugin::getModelFallback`, the same predicate `plugin::modelFromJson` uses, so
the badge, the missing-modules dialog and the importer cannot disagree. They used
to: the importer was the only one applying Rack's rename tables.

**A delisted plugin has no `available` key.** It does not carry
`available: false` -- measured against the live endpoint, 456 of 558 manifests
have the key and every one is true; the other 102 omit it, and those are exactly
the plugins the library shows as "Unavailable". The old parse fell back to
`status == "available"` for entries without the boolean, which reads sensibly and
is precisely backwards: only 67 entries carry `status`, every one of them is
inside the delisted 102, and every one says `"available"`. So the fallback
whitelisted 67 delisted plugins and the default argument whitelisted the other
35. All 102 were reported as installable.

`arches` is present on exactly the same 456 entries and says which builds exist,
so "in the library" and "installable here" are separate questions -- 15 available
plugins have no `mac-arm64` build.

Each missing plugin gets one verdict (`ps/Resolve.hpp`), and each verdict either
links somewhere or states why it can't:

| verdict | means |
|---|---|
| installable | in the library, built for this machine |
| premium | in the library, paid |
| update available | installed, and the library ships something newer |
| **module retired** | installed and current; this model no longer exists |
| needs newer Rack | `minRackVersion` is beyond this build |
| no build for this platform | available, but not for this OS/CPU |
| **delisted -- source on GitHub** | gone from the library, source published |
| delisted | gone, and no source |
| not in the library | never heard of the slug |

The two in bold are why this was worth doing. "Module retired" used to read
*"update for 1"*: the patch wants `FrozenWasteland/QuadEuclideanRhythm`, the
installed Frozen Wasteland 2.1.2 is current, and the module was renamed to
`QuadAlgorithmicRhythm`. No update will ever fix it, and the library page just
says "Installed". The row now names the module, links the plugin's changelog --
the one place a rename is written down -- and, where it can be confident,
suggests the successor.

That suggestion is **not** the SDK's `fuzzysearch::Database`. That matcher wants
every query word to hit, so it scores this exact case 0.000, indistinguishable
from a query of pure gibberish, because the middle word is the one that changed.
`suggestReplacement` matches on shared words instead, and answers only when one
candidate clearly wins -- so `QuadEuclideanRhythm` resolves to *Quad Algorithmic
Rhythm Generator*, `Drums - Snare` resolves by exact name match, and
`Drums - Open Hats` declines rather than guess between Open and Closed hats.
Nothing is ever substituted into a patch; that would change how it sounds without
saying so.

The badge carries the same distinction. `-4 OF 21` in clay means install these
and it runs; `x7 OF 21` in sage means at least one of them cannot be had on this
machine, so no amount of installing will help, and the row sorts below the
fixable ones under "Playable first". Nothing is judged a dead end while the
manifests are still loading -- and when they land, `reresolveAudits` re-judges
the page rather than leaving it pessimistic.

## What it deliberately doesn't do

**There is no "open this patch" button.** Loading a patch wholesale means
`APP->patch->loadAction()`, which clears the rack -- taking this module with it,
so you'd have to re-add the browser before you could look at another patch. It's
also the one operation here that is genuinely hazardous to implement:
`RackWidget::clear()` erases the module's node from the container's child list
while `Widget::step()` is iterating it, which is undefined behaviour anywhere
except a `ui::MenuItem::onAction`. Import does everything Open did except discard
your rack, and Save to disk + File > Open covers the rest. See `src/PatchAudit/ps/Apply.hpp`.

## Building

PatchAudit ships inside the Moon Technologies plugin, so it is built with the
rest of the family — see [BUILDING.md](BUILDING.md). Nothing extra is linked:
jansson, zstd, osdialog, libcurl and nanovg all come from the Rack SDK.

## The panel

The panel is generated. Edit `tools/panels/PatchAudit.py` — the spec — never the
SVG, and never `src/PanelTheme.hpp` or `src/PatchAudit/Panel.hpp`, all of which are
written from it along with both previews:

```bash
make panel-PatchAudit      # artwork, the two headers, the browser mock
make preview-PatchAudit    # ... and open the mock
make vcv-preview             # ... build, then render every panel through VCV Rack
```

`make panel` does the same for every panel in the plugin at once, which is what
you want after a change to `panelkit/`.

The palette, the shared hardware, the layout rules and the two constraints Rack's
renderer imposes are documented once, in [`../panelkit/README.md`](../panelkit/README.md),
which is also where every other panel in the plugin gets theirs. There is no
per-module copy of any of it.

Note the Makefile does **not** add `-Isrc`. `rack.hpp` contains
`#include <plugin.hpp>`, which would then resolve to this plugin's own
`src/plugin.hpp`. All internal includes are relative.

## Design notes

**Threading.** All network I/O runs on two worker lanes (`src/PatchAudit/ps/JobRunner.cpp`):
one for API requests, one for downloads. The API lane is single-threaded with a
250 ms floor between requests, which *is* the rate-limiting policy -- there is
never more than one request in flight to patchstorage.com. Results are published
into a mutex-guarded struct with an atomic dirty flag, drained once per frame in
`PatchAuditWidget::step()`; this is the same pattern Rack uses for its own
library sync. There are no timer threads: the search debounce is a deadline
polled in `step()`.

Threads are **joinable and joined**, not detached. Rack calls a plugin's
`extern "C" void destroy()` before `dlclose()`, so `JobRunner::shutdown()` can
guarantee no code from this library is executing when the image is unmapped.
Rack calls `destroy()` once per *plugin*, not once per module, so it lives in
the shared `src/plugin.cpp` rather than in PatchAudit's own sources.

**Lifetime.** `PatchstorageClient` holds only plain data -- never a pointer to a
Module, a Widget, or anything Rack owns. Every job captures one
`std::shared_ptr<PatchstorageClient>` by value, so the client outlives every job
and no worker can touch freed memory. Deleting the module calls `abandon()`,
which bumps every generation counter; in-flight results are then discarded rather
than published. Workers hand the UI thread file *paths*, never a `json_t*`,
because jansson refcounts aren't atomic.

**Missing-module scanning** runs on the UI thread on purpose: `plugin::getModel`
walks `plugin::plugins`, and nothing documents that container as thread-safe.

## API notes

Patchstorage's beta API (`https://patchstorage.com/api/beta`) needs no
authentication for reads. Three things about it are worth knowing:

- The list endpoint does **not** include `files`, so opening a patch always costs
  a second request to `/patches/{id}`.
- A patch object has its own `"code"` field -- the source-code field for
  text-based platforms, an empty string for VCV. It looks exactly like a
  WordPress error code, so the response classifier checks for `"id"` first.
- `X-WP-Total` is a response *header*, and `network::requestJson` exposes no
  headers, so the total page count is unknowable. The page readout says
  "page 3" until a short page proves it's the last, then "page 3 of 3".

Only `date`, `download_count`, `like_count`, `view_count`, `title` and `id` are
accepted for `orderby`; `relevance`, `rand` and `comment_count` return HTTP 400.

Not every attachment is a Rack 2 patch, and the file extension lies: uploads
named `.vcv` turn out to be zips, and pre-2.0 `.vcv` files are plain JSON, not
archives. In fact the all-time most-liked patches on the site are Rack v0.5 and
v1 files. So `ps/PatchFile.cpp` sniffs the leading bytes instead of trusting the
name, and unwraps as many layers as it takes -- a zip holding a `.vcv` holding a
`patch.json` is a normal upload. Whatever it can't open gets its own message
rather than a JSON parse error.

Zip support is hand-rolled (`ps/Zip.cpp`) because Rack can't help: its
`system::unarchiveToDirectory` enables only libarchive's tar format and zstd
filter (`Rack/src/system.cpp:537-539`), and although Rack links zlib it exports
none of it. The DEFLATE decoder is the one inside the SDK's own `stb_image.h`,
compiled with `STB_IMAGE_STATIC` and every image codec switched off, so nothing
is added to the plugin's exported symbols -- see `src/PatchAudit/ps/Inflate.cpp`.

**The User-Agent cannot be customised.** `Rack/src/network.cpp` hardcodes it to
`VCV Rack Free/<version>`, and `requestJson`'s only extension point is a cookie
map. This plugin's traffic is indistinguishable from Rack's own.

## Caching

- `<Rack user dir>/PatchAudit/cache/api/` -- JSON bodies. Search 10 min,
  patch detail 1 h, VCV Library manifests 24 h.
- `<Rack user dir>/PatchAudit/cache/patches/` -- downloaded `.vcv` files, named
  with the patch's `updated_at` so a re-upload misses and re-downloads. Pruned to
  200 MB, oldest first, at startup.

Both are in the module's right-click menu, with a size readout and a way to open
the folder or clear it.
