# Building Moon Technologies

The plugin builds with the official **VCV Rack 2 Plugin SDK** on Windows, macOS
and Linux. There are no third-party dependencies to install: jansson, zstd,
libcurl, osdialog, nanovg and nanosvg all come from the SDK.

```bash
git clone https://github.com/drphilibuster/MoonTechnologies.git
cd MoonTechnologies
make -j$(nproc)          # -j$(sysctl -n hw.ncpu) on macOS
make install             # then restart Rack
```

That is the whole build, once the two prerequisites below are in place.

---

## 1. The Rack SDK

Download the SDK for **your** operating system and CPU from
<https://vcvrack.com/downloads> (bottom of the page, "Rack SDK"), and unpack it.
Match the SDK's Rack version to the Rack you run.

By default the Makefile looks for it, in order, at:

```
../Rack-SDK          ../../Rack-SDK          ~/Rack-SDK          ~/src/Rack-SDK
```

so unpacking it as a sibling of this repo needs no configuration:

```
somewhere/
├── Rack-SDK/
└── MoonTechnologies/
```

Anywhere else, point at it explicitly — either per build or once in your shell:

```bash
make RACK_DIR=/path/to/Rack-SDK
export RACK_DIR=/path/to/Rack-SDK
```

If the SDK is missing the build stops immediately with that message rather than
failing later inside the compiler.

## 2. A toolchain, plus `jq`

`jq` is not optional: the SDK's `plugin.mk` reads the slug and version out of
`plugin.json` with it, and without it the build produces a package with an empty
name.

### macOS

```bash
xcode-select --install     # Apple Clang, make
brew install jq
```

### Windows

Build inside [MSYS2](https://www.msys2.org), in the **MINGW64** shell (not the
MSYS or UCRT64 shell — Rack plugins are MinGW-w64 binaries):

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-jq make git
```

Then `cd` to the repo inside that shell and run `make`. Building from PowerShell,
cmd, or with MSVC will not work: Rack's plugin ABI on Windows is MinGW-w64's.

### Linux

```bash
sudo apt install build-essential git jq        # Debian/Ubuntu
sudo dnf install gcc-c++ make git jq           # Fedora
sudo pacman -S base-devel git jq               # Arch
```

### For panel work only

Regenerating panels additionally needs **Python 3.8+**. No packages — panelkit
is pure standard library. Building the plugin does not need Python: the
generated headers and SVGs are committed.

---

## What the targets do

| Target | Effect |
| --- | --- |
| `make` | Compiles `plugin.dylib` / `.so` / `.dll` in the repo root |
| `make dist` | Packages `dist/MoonTechnologies-<ver>-<os>-<cpu>.vcvplugin` |
| `make install` | `make dist`, then copies the package into your Rack plugins folder |
| `make clean` | Removes `build/`, `dist/` and the built library |
| `make panel` | Regenerates every panel: art, headers, previews |
| `make panel-Retroactive` | Just that one panel |
| `make preview-Retroactive` | ... and opens the browser mock |
| `make vcv-preview` | Renders every panel through VCV Rack itself |
| `make test` | Runs the host-side unit tests (no Rack needed) |

**`make install` does not take effect until Rack is restarted.** It leaves a
`.vcvplugin` archive in the plugins folder, which Rack unpacks over the existing
plugin directory at startup and then deletes (`Rack/src/plugin.cpp:225`). A
screenshot showing the old behaviour right after a build is almost always this,
not a broken build.

---

## Cross-compiling for all platforms

Release binaries for all four targets are built by CI
(`.github/workflows/build.yml`) using VCV's own
[rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain), which
is the same toolchain the VCV Library uses. To reproduce that locally you need
Docker:

```bash
git clone https://github.com/VCVRack/rack-plugin-toolchain.git
cd rack-plugin-toolchain
make docker-build                                    # ~1 hour, once
make docker-plugin-build PLUGIN_DIR=/path/to/MoonTechnologies
```

Results land in `rack-plugin-toolchain/plugin-build/`.

For everyday work, build natively — it takes seconds, and CI covers the rest.

---

## Tests

The parts that can be tested without Rack are:

```bash
make test                      # everything
cd tests/Retroactive && make   # the permutation DSP: 277 checks under ASan + UBSan
cd tests/UncertaintyPolicy && make
```

These compile the headers directly against a host compiler and never link
`libRack`, so they run anywhere — including in CI on every push.

---

## After any UI change

Panels are generated. `res/*.svg`, `src/PanelTheme.hpp` and
`src/<Module>/Panel.hpp` are **outputs**; the input is
`tools/panels/<Module>.py`. A hand edit to a generated file is destroyed by the
next `make panel`.

```bash
make panel && make -j8 && make vcv-preview
```

Look at `tools/previews/<Module>.png` before calling a panel done: it is rendered
by Rack itself, through the real widget tree, so it is the only preview that
cannot lie. A change to `panelkit/` touches every panel — regenerate and
look at all of them.

See [`../panelkit/README.md`](../panelkit/README.md) for the design language and
the spec API.
