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
