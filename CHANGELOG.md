# Changelog

Versions follow the VCV convention: the major number is the Rack major version
these modules run on, so a Rack 2 plugin is always `2.x.y`.

## 2.0.0

First release of Moon Technologies as a single plugin.

PatchAudit, Retroactive and Uncertainty Policy previously existed as three
separate Rack plugins sharing a brand. They are now one plugin with three
modules, which is how the VCV Library expects a brand to ship: one entry, one
install, one version.

**If you have patches built with the old separate plugins**, the module slugs are
unchanged but the plugin slug is not, so Rack will not resolve them
automatically — it shows a placeholder where each module was and keeps the rest
of the patch intact. Re-place the module and reconnect its cables.

### Modules

- **PatchAudit** (26 HP, Utility/Visual) — browse Patchstorage from inside Rack,
  audit patches against your installed modules before opening them, and import
  straight into your rack or save to disk.
- **Retroactive** (12 HP, Effect/Delay/Granular) — windowed sample permutation
  with eight modes, clock sync, subdivision, crossfade and freeze.
- **Uncertainty Policy** (16 HP, Utility/Random) — signal-aware knob and cable
  randomizer that auditions each roll and reverts the ones that kill the sound.

### Under the hood

- Windows builds now work. PatchAudit's optional libcurl fast path looked its
  symbols up through `dlsym(RTLD_DEFAULT, …)`, which does not exist on Windows;
  it now goes through a small shim that uses `GetProcAddress` over the loaded
  modules there. Where the lookup fails on any platform the audit downloads
  large zipped uploads instead of streaming them, exactly as before.
- Panels are generated into two headers rather than one: a shared
  `src/PanelTheme.hpp` holding the vocabulary every panel draws through, and a
  per-module `src/<Module>/Panel.hpp` holding that panel's own numbers. Three
  panels can now be linked into one plugin without their silkscreen tables
  colliding.
- Prebuilt binaries for `win-x64`, `mac-arm64`, `mac-x64` and `lin-x64`, built
  by CI on every push against the official Rack SDK.
