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

## MAME: ES5510 and MC6803 (Apportionment)

`mame/` holds the parts of [MAME](https://github.com/mamedev/mame) that
Apportionment's Ensoniq DP/4 emulation runs on. Both cores are under the
3-clause BSD licence, which is compatible with this plugin's GPL-3.0-or-later;
the licence line and copyright holders at the top of every file are kept.

* `es5510/` -- the Ensoniq ESP (ES5510) core by Christian Brunschen
  (`src/devices/cpu/es5510/`), carrying a three-line patch, recorded in
  `es5510/es5510-dp4.patch` against upstream: END now lands on step 0 (upstream
  skipped it on every pass after the first), and halting resets the DOL FIFO
  pointers, as the ESP specification (Rev. 2.4, section 3.4) says it must.
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
