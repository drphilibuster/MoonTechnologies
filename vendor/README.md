# Vendored dependencies

## Syphon

`Syphon/` is the [Syphon Framework](https://github.com/Syphon/Syphon-Framework),
copyright 2010 bangnoise (Tom Butterworth) & vade (Anton Marini), under the
3-clause BSD licence in `Syphon/License.txt`. It is compatible with this
plugin's GPL-3.0-or-later, and its licence text is retained unmodified.

Vendored at commit `71351d4b484cd2d1917867f7846a5cdca724552d`.

It is here rather than linked as a framework because Transmittal compiles it
straight into `plugin.dylib`: there is no bundle to ship, nothing to
code-sign, and no runtime lookup that could find a different version than the
one this was built against. The Xcode project and documentation catalog are
removed -- the sources are built by this plugin's own Makefile, which supplies
the two things Xcode otherwise would (the prefix header, and `-Ivendor` so that
`<Syphon/...>` resolves against the directory's own name).

The Metal sources are excluded. Rack's context is OpenGL (`NANOVG_GL2`), so
only the GL server is reachable, and the Metal shaders would need a Metal
compiler this build does not invoke.

Nothing else in this plugin depends on it, and nothing outside macOS builds it.

## dsp56300 and gearmulator's HD44780 (Contagion)

`dsp56300/` is the Motorola DSP56300-family emulator by the dsp56300 project
(<https://github.com/dsp56300/dsp56300>), the core gearmulator runs on: `dsp56kEmu`
(the DSP, its peripherals, the JIT) and `dsp56kBase`, under GPLv3 (`LICENSE.md`),
with `asmjit` (zlib, `asmjit/LICENSE.md`) for the JIT. It is compatible with this
plugin's GPL-3.0-or-later. Vendored at the commit in `dsp56300/COMMIT` (taken from
gearmulator's submodule); only the build files are dropped. The unit tests in it
are not compiled. gearmulator builds it with MSVC on Windows; Rack plugins are MinGW,
so a few changes, each marked `MoonTechnologies` in the source, make it build and run
there:

* the JIT chose its calling convention with `_MSC_VER`, so a MinGW build would emit
  System V code into a Win64 process. `jitregtypes.h` and `jitstackhelper.cpp` now
  test `_WIN32`, as `jittrampoline.cpp` already did;
* `mmuhelper.cpp` raises `NTDDI_VERSION` under MinGW so its headers declare
  `VirtualAlloc2` and `MapViewOfFile3` (still looked up at run time);
* `threadtools.cpp` names threads with `SetThreadDescription` only under MinGW: the
  debugger-exception way needs MSVC's `__try`.

`gearmulator/hardwareLib/` is gearmulator's HD44780 controller model and character
ROM (<https://github.com/dsp56300/gearmulator>, GPLv3, `gearmulator/LICENSE.md`), at
the commit in `gearmulator/COMMIT`.

`src/Contagion/VirusC.cpp` is the only translation unit of ours that includes
either; the Makefile compiles them as C++17.

## gearmulator's Nord Lead 2X, mc68k and Musashi (Nordic Banking)

From gearmulator (<https://github.com/dsp56300/gearmulator>, GPLv3, `gearmulator/LICENSE.md`),
at the commit in `gearmulator/COMMIT`:

* `gearmulator/n2xLib/` -- the Nord Lead 2X's hardware: its DSPs, host ports, front panel,
  flash and ROM (`n2xdsp`, `n2xhdi08`, `n2xfrontpanel`, `n2xflash`, `n2xmc`, `n2xhardware`,
  `n2xrom`, `n2xromdata`, `n2xtypes.h`, `n2xmiditypes.h`). Its plugin side (device, state,
  ROM loader) is not vendored; `src/NordicBanking/Nord2x.cpp` takes its place.
* `gearmulator/mc68k/` -- the MC68331 around Musashi: SIM, QSM, GPT, ports, host interface.
* `gearmulator/hardwareLib/` `i2c`, `i2cFlash`, `sciMidi`; `gearmulator/baseLib/` `filesystem`,
  `semaphore.h`; `gearmulator/synthLib/` `midiTypes.h`, `midiBufferParser`, `audioTypes.h`,
  `deviceException`, `deviceTypes.h`, `os` -- the framework pieces those use.
* `gearmulator/mc68k/Musashi/` -- Karl Stenerud's Musashi 68000-family emulator (MIT; the
  licence is in `readme.txt` and atop each file).

Changes, each marked `MoonTechnologies` in the source:

* **Musashi is built without its FPU and PMMU.** The CPU32 core of the MC68331 has neither,
  and gearmulator runs the core as a 68020, whose FPU path is never taken. `m68kfpu.c`,
  `m68kmmu.h` and the SoftFloat 2b they need are not vendored -- SoftFloat 2b's licence adds
  an indemnification condition widely held to be incompatible with the GPL. `m68kcpu.h`
  keeps the 80-bit register slots as plain storage, and the three coprocessor entry points
  raise the F-line exception, as a CPU32 does.
* `mc68k/logging`: a settable sink (`setLogSink`) in place of unconditional stderr.
* `n2xfrontpanel`: `setButtonState` had its polarity inverted (the key lines are active-low,
  idle 0xff, as `getButtonState` already read them; upstream's plugin never presses panel
  buttons, so it never showed); and a write observer, from which the module models the LED
  multiplex.
* `n2xflash`, `n2xhardware`: no search of the disk for a ROM or "any 64 KB file" (a plugin
  must not pick up whatever lies around); the OS image is always given, and the flash's
  power-on contents are a constructor argument, set before the 68331 starts.
* `n2xdsp`: the debugger include is behind `DSP56300_DEBUGGER`, as its use already was.
* `i2cFlash::getData()` and `Microcontroller::getFlash()`, so a patch can keep the flash.
* MinGW (Rack's Windows toolchain): `baseLib/filesystem.cpp` and `synthLib/os.cpp` include
  `shlobj.h` where MSVC has `shlobj_core.h`; `synthLib/deviceException.cpp` includes
  `<cstdint>`, which MSVC supplied transitively. The Makefile also builds these sources
  without `UNICODE` on Windows, as gearmulator's own builds are: they call the narrow Win32
  APIs, which the SDK's `-municode` would turn into the wide ones.

## MAME: ES5510 and MC6803 (Apportionment)

`mame/` holds the parts of [MAME](https://github.com/mamedev/mame) that
Apportionment's Ensoniq DP/4 emulation runs on. Both cores are under the
3-clause BSD licence, which is compatible with this plugin's GPL-3.0-or-later;
the licence line and copyright holders at the top of every file are kept.

* `es5510/` -- the Ensoniq ESP (ES5510) core by Christian Brunschen
  (`src/devices/cpu/es5510/`), carrying a patch recorded in
  `es5510/es5510-dp4.patch` against upstream, each change citing the ESP
  specification (Rev. 2.4):
  * END lands on step 0 (upstream skipped it on every pass after the first).
  * Halting resets the DOL FIFO pointers (section 3.4).
  * A skippable step's condition is sampled as its results are written, not
    when it issues (sections 3.3.3 and 4.2.1). Upstream sampled it before the
    preceding step's CCR/CMR write-back, so a `MOV` to CMR failed to govern the
    skippable step after it -- the DP/4's noise gates never opened.
  * DADR is left-justified (section 5.1.3): a host DRAM access addresses the
    top 16 bits. Upstream used the low bits, and the host-loaded gain tables
    landed nowhere near where the programs read them.
  * 64K words of DRAM, as the DP/4's firmware configures (MEMSIZ = `$0000FF`,
    the specification's own value for 64K), not the 1M upstream allocated.
  * The PC runs through 256 values but there are 160 instruction words and 192
    GPRs: past them the core now reads zero, where upstream read past its
    arrays into the heap (at power-on, before the firmware halts the chips),
    which made a session's final state depend on whatever memory was there.
    `mac_overflow` is initialised; upstream never set it.
  `emu.h`, `logmacro.h`, `corestr.h` and `cpu/m68000/m68000.h` are not MAME's:
  they are a small stand-in for MAME's framework, just wide enough to compile
  the core outside MAME. `src/Apportionment/Esp.cpp` is the only translation
  unit that includes them.
* `m6800/` -- the MC6800-family opcode bodies (`6800ops.hxx`, Aaron Giles et
  al.) and, extracted verbatim from `m6800.cpp` / `m6801.cpp`, the flag tables,
  addressing macros and the MC6803 opcode and cycle tables.
  `src/Apportionment/M6803.cpp` wraps them in its own CPU shell with the 6801
  on-chip timer, SCI and ports; MAME's `m6801.cpp` itself is not used because
  it is bound to MAME's scheduler.

Neither directory is compiled on its own: the Makefile's `SOURCES` glob covers
`src/` only, and the two translation units above include what they need.

## Z80 (Depreciation)

`z80/` is Nicolas Allemand's [superzazu/z80](https://github.com/superzazu/z80), an MIT-licensed
Z80 interpreter in plain C (`z80.c`, `z80.h`; its `LICENSE` is retained unmodified). Depreciation
runs a Lexicon PCM 70's master and slave processors on two instances. It is compiled straight into
`plugin.dylib` like the other vendored sources, and the tests build it on its own. The copy is the
one the research emulation was validated with (the machine in `src/Pcm70Machine.hpp` reproduces that
emulation byte for byte); do not update it without re-running `tests/Depreciation`.

Local change: the Z80 `cyc` counter is widened from `unsigned long` to `uint64_t`, because it is 32 bits on Windows (LLP64) and wraps after about 22 minutes at 3.25 MHz.

## Shoal and the distingNT API (Ledger's golden tests)

`shoal/shoal.cpp` is Ormer Modular's Shoal, an 8-track generative melody
sequencer for the Expert Sleepers disting NT
(<https://github.com/ormermodular/shoal>), copyright 2026 Ormer Modular, under
the MIT licence in `shoal/LICENSE`. Vendored at commit
`845b8d0bbbc6315ffa09fd3338137362695f0f1a` (v1.2.1 + "Sync from shoal-dev@7b71cc6").

`distingNT_API/include/distingnt/api.h` is the header that file is written
against, from <https://github.com/expertsleepersltd/distingNT_API>, copyright
2025 Expert Sleepers Ltd, MIT (`distingNT_API/LICENSE`). Vendored at commit
`6975a630cb5f86de6f8c709f5467f4666bbd4579`; only `api.h` is kept.

Neither is compiled into the plugin. Ledger's engine (`src/Ledger/Shoal.hpp`)
is a transliteration of `shoal.cpp`'s sequencing code, credited there; these
two files exist so that `tests/Ledger/` can build the original against stub
`NT_*` functions and check, frame by frame, that the same parameters and seeds
produce the same gates, pitches, Currents, EOS pulses and MIDI bytes.
Updating Shoal means replacing `shoal.cpp`, bumping the commit above, and
re-running `make -C tests/Ledger`.
