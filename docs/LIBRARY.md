# VCV Library submission

Notes for submitting Moon Technologies to the
[VCV Library](https://github.com/VCVRack/library). Nothing here is required to
build or use the plugin.

## How submission works

You open an issue on [VCVRack/library](https://github.com/VCVRack/library) with
the repository URL and a git tag. VCV builds the plugin themselves, on their own
machines, using [rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain)
— the same toolchain this repo's CI uses. They do not run our binaries, and they
do not run Python, so **everything the compiler needs must be committed**, which
is why the generated panels and headers are in git.

Updates are then automatic: VCV rebuilds from the latest tag whose `version` is
newer than the one in the Library.

## Checklist

### Done

- [x] **One plugin, one repository, `plugin.json` at the repo root.** The build
      machines run `make` in the repo root; a monorepo with the plugin in a
      subdirectory cannot be submitted as-is.
- [x] **Unique slug** — `MoonTechnologies`. Slugs are permanent: changing one
      orphans every patch that used it, because Rack's fallback table
      (`Rack/src/plugin.cpp:374`) is maintained by VCV, not by plugin authors.
- [x] **Version is `2.x.y`.** The major must match the Rack major version.
- [x] **A free software licence** — GPL-3.0-or-later, with the full text
      committed. Panel artwork is CC BY-NC-ND with explicit permission for VCV to
      distribute it, which is the arrangement VCV uses for Fundamental.
- [x] **Manifest complete** — `brand`, `author`, `pluginUrl`, `sourceUrl`,
      `manualUrl`, `changelogUrl`, and a per-module `manualUrl`, `description`
      and `tags`.
- [x] **Tags are from Rack's official list** (`Rack/src/tag.cpp`). Invented tags
      are silently dropped.
- [x] **No third-party dependencies.** Everything links against the SDK only, so
      the toolchain build needs no `make dep`.
- [x] **Builds on all four targets** — `win-x64`, `mac-arm64`, `mac-x64` and
      `lin-x64`, verified in CI on every push against the official Rack SDK.
- [x] **No unresolved symbols.** PatchAudit's libcurl fast path is looked up at
      runtime rather than linked, because a plugin with an unresolved symbol does
      not fail to *run*, it fails to *load* — silently, for everyone.
- [x] **`destroy()` joins every worker thread** before Rack unmaps the library.

### Before submitting

- [ ] **Decide about PatchAudit.** It downloads patches from Patchstorage and can
      install plugins from the VCV Library on the user's behalf. Nothing in the
      Library guidelines forbids it, but it is the one module here that touches
      VCV's own infrastructure, and it is worth raising with VCV *before*
      submitting rather than having the review discover it. Retroactive and
      Uncertainty Policy raise no such question. Submitting the plugin without
      PatchAudit is possible but means a fourth module slug decision later.
- [ ] **Set `authorEmail` in `plugin.json`** (currently empty). VCV uses it to
      reach you about build failures. It becomes public.
- [ ] **Add a `donateUrl`** if you want one.
- [ ] **Tag a release** — `git tag v2.0.0 && git push --tags`. The tag is what
      you cite in the submission issue.
- [ ] **Confirm the repository is public** and the tag is visible.
- [ ] **Run `make test`** and the panel regeneration one last time, so that what
      is committed is what the spec produces.

## Things that will fail review

- A `version` that does not increase between updates. VCV rebuilds only when it
  does.
- Hand-edits to generated files that disagree with the spec — run `make panel`
  and commit the result rather than patching a header.
- Non-ASCII or space-containing module slugs. Slugs are `[a-zA-Z0-9_-]` only;
  display names are free-form (`Uncertainty Policy` is the *name*; the slug is
  `UncertaintyPolicy`).
