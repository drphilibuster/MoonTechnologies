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
are not compiled. One change, marked `MoonTechnologies` in the source: the JIT chose
its calling convention with `_MSC_VER`, so a MinGW build (which is what Rack on
Windows is) would emit System V code into a Win64 process. `jitregtypes.h` and
`jitstackhelper.cpp` now test `_WIN32`, as `jittrampoline.cpp` already did.

`gearmulator/hardwareLib/` is gearmulator's HD44780 controller model and character
ROM (<https://github.com/dsp56300/gearmulator>, GPLv3, `gearmulator/LICENSE.md`), at
the commit in `gearmulator/COMMIT`.

`src/Contagion/VirusC.cpp` is the only translation unit of ours that includes
either; the Makefile compiles them as C++17.

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
