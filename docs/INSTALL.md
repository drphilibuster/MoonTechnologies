# Installing Moon Technologies

One plugin, twenty modules — from **PatchAudit** and **Retroactive** to the
Modular-in-a-Week banks. Installing the plugin installs all of them; they appear in Rack's module
browser under the brand **Moon Technologies**.

Requires **VCV Rack 2** (Free or Pro), version 2.0 or newer.

---

## The short version

1. Download the `.vcvplugin` for your operating system and CPU from the
   [latest release](https://github.com/drphilibuster/MoonTechnologies/releases/latest).
2. Drop the file — **do not unzip it** — into your Rack *plugins folder*
   (below).
3. **Restart Rack.**

Rack unpacks any `.vcvplugin` it finds in that folder at startup and then
deletes the archive (`Rack/src/plugin.cpp:225`, `extractPackages`). So the file
disappearing is what success looks like, and **the modules do not appear until
Rack has been restarted** — a rack that still shows the old panels almost always
just needs quitting and reopening.

---

## Which file do I want?

| Your machine | File |
| --- | --- |
| Windows (any PC since ~2010) | `MoonTechnologies-<version>-win-x64.vcvplugin` |
| macOS, Apple Silicon (M1/M2/M3/M4) | `MoonTechnologies-<version>-mac-arm64.vcvplugin` |
| macOS, Intel | `MoonTechnologies-<version>-mac-x64.vcvplugin` |
| Linux, 64-bit x86 | `MoonTechnologies-<version>-lin-x64.vcvplugin` |

VCV publishes Rack and its SDK for those four targets only, so those are the four
builds here. On anything else — Linux on ARM, for instance — you would have to
build both Rack and this plugin yourself; see [BUILDING.md](BUILDING.md).

On a Mac, if you are not sure which you have:  > About This Mac. "Chip:
Apple M…" means arm64; "Processor: Intel…" means x64.

A file for the wrong architecture is not dangerous — Rack simply logs that it
could not load it and carries on.

---

## Where the plugins folder is

### Windows

```
%LOCALAPPDATA%\Rack2\plugins-win-x64
```

Paste that into the File Explorer address bar and press Enter. Typically it
expands to `C:\Users\<you>\AppData\Local\Rack2\plugins-win-x64`. `AppData` is
hidden by default, which is why pasting the path is easier than clicking to it.

### macOS

```
~/Library/Application Support/Rack2/plugins-mac-arm64
```

(or `plugins-mac-x64` on an Intel Mac).

`~/Library` is hidden in Finder. Use **Go > Go to Folder…** (Shift-Cmd-G) and
paste the path.

### Linux

```
~/.local/share/Rack2/plugins-lin-x64
```

If you have set `XDG_DATA_HOME`, it is `$XDG_DATA_HOME/Rack2/plugins-lin-x64`
instead.

> **If the folder does not exist**, create it — but check the spelling against
> the table above first. Rack only ever looks in the folder whose name matches
> its own OS and CPU, so a plugin sitting in `plugins-mac-x64` on an Apple
> Silicon machine is invisible to it.

You can also open this folder from inside Rack: **Help > Open user folder**,
then go into the `plugins-…` directory.

---

## Checking it worked

Restart Rack, then right-click on empty rack space to open the module browser
and type `Moon`. You should see twenty modules, all badged **Moon Technologies**.

If they are not there, open **Help > Open user folder** and read `log.txt`. A
plugin that failed to load says so explicitly, with the reason — that one line
is worth more than any amount of guessing.

---

## macOS: "cannot be opened because the developer cannot be verified"

Release builds are ad-hoc signed, not notarised by Apple, because notarisation
requires a paid Apple Developer account. macOS therefore quarantines the file
when your browser downloads it. If Rack refuses to load the plugin and `log.txt`
mentions a code signature, clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Application\ Support/Rack2/plugins-mac-arm64
```

Then restart Rack. This is the same step every unnotarised Rack plugin needs;
it is not specific to this one.

---

## Uninstalling

Quit Rack, delete the `MoonTechnologies` folder from your plugins folder, and
start Rack again.

Patches that used the modules will still open — Rack shows a placeholder where
each missing module was, and restores it fully if you reinstall the plugin. It
does not discard the rest of the patch.

---

## Building it yourself instead

See [BUILDING.md](BUILDING.md). Building from source is the only option on a
platform there is no release binary for, and it is how you install a development
version.
