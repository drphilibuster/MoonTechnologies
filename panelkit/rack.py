"""The authoritative preview: the panel as VCV Rack itself draws it.

Rack's standalone binary takes `--screenshot <zoom>`, which renders every model
of every loaded plugin to a PNG through the real widget tree -- panel SVG, stock
component art, runtime silkscreen, lights, the lot -- and exits. That is the only
preview that cannot lie, so it is the one the pipeline ends on.

Two details make it usable as a build step rather than a chore:

  * `--user <dir>` moves Rack's whole user directory, and plugins are loaded from
    `<dir>/plugins-<os>-<cpu>`. Pointing that at a scratch directory holding a
    single symlink to the plugin under test means Rack loads Core, Fundamental
    and *only* that plugin, so the run takes seconds instead of minutes and the
    real Rack install is never touched. The symlink points at the plugin's own
    build directory, so no install step is needed either.

  * `screenshotModules()` skips any model whose PNG already exists, so the
    screenshots directory has to be wiped first or nothing is regenerated.

A second Rack instance is fine on macOS and Linux; only the Windows build takes
a single-instance mutex. On Windows, close Rack first. Even where it is allowed,
bringing up a second GL context alongside a running Rack is occasionally refused
by the window server -- the instance then aborts in Window::Window -- so this
retries, and reports what the log actually said rather than "no PNG".
"""

import os
import platform
import shutil
import subprocess
import sys

MAC_APP = "/Applications/VCV Rack 2 Pro.app"
MAC_FREE = "/Applications/VCV Rack 2 Free.app"


def _binary():
    for app in (MAC_APP, MAC_FREE):
        exe = os.path.join(app, "Contents/MacOS/Rack")
        if os.path.exists(exe):
            return exe
    for exe in ("/usr/local/bin/Rack", os.path.expanduser("~/Rack2/Rack")):
        if os.path.exists(exe):
            return exe
    return None


def _arch():
    osname = {"Darwin": "mac", "Linux": "lin", "Windows": "win"}[platform.system()]
    cpu = "arm64" if platform.machine() in ("arm64", "aarch64") else "x64"
    return "%s-%s" % (osname, cpu)


def _real_user_dir():
    if platform.system() == "Darwin":
        return os.path.expanduser("~/Library/Application Support/Rack2")
    if platform.system() == "Windows":
        return os.path.expandvars(r"%LOCALAPPDATA%\Rack2")
    return os.path.expanduser("~/.local/share/Rack2")


def render(plugin_dir, slug, out_png, zoom=3.0, scratch=None, timeout=300,
           tries=3, model=None):
    """Render one model's panel through Rack. Returns (ok, message).

    `slug` is the *plugin* slug -- the directory Rack loads the plugin under,
    and the directory it files the screenshots under. `model` is the module
    within it; a plugin with three modules renders all three in one run, so
    this says which PNG to keep. It defaults to `slug`, which is right for a
    plugin holding a single module of the same name."""
    exe = _binary()
    if not exe:
        return False, "VCV Rack not found; looked in %s" % MAC_APP
    if not os.path.exists(os.path.join(plugin_dir, "plugin.json")):
        return False, "%s has no plugin.json" % plugin_dir
    dylib = [f for f in os.listdir(plugin_dir) if f.startswith("plugin.")
             and f.rsplit(".", 1)[-1] in ("dylib", "so", "dll")]
    if not dylib:
        return False, "%s has no built plugin library -- run `make` first" % plugin_dir

    scratch = scratch or os.path.join(plugin_dir, "tools", ".rackshot")
    plugins = os.path.join(scratch, "plugins-" + _arch())
    shots = os.path.join(scratch, "screenshots")
    shutil.rmtree(shots, ignore_errors=True)
    shutil.rmtree(plugins, ignore_errors=True)
    os.makedirs(plugins, exist_ok=True)
    link = os.path.join(plugins, slug)
    os.symlink(os.path.abspath(plugin_dir), link)

    # Rack Pro reads its entitlement out of settings.json; without one it would
    # come up unauthorised in the scratch directory and render nothing.
    src_settings = os.path.join(_real_user_dir(), "settings.json")
    if os.path.exists(src_settings) and not os.path.exists(os.path.join(scratch, "settings.json")):
        shutil.copy(src_settings, os.path.join(scratch, "settings.json"))

    for attempt in range(tries):
        shutil.rmtree(shots, ignore_errors=True)
        try:
            subprocess.run([exe, "-u", os.path.abspath(scratch), "-t", str(zoom)],
                           check=False, timeout=timeout,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            _kill(scratch)
            if attempt == tries - 1:
                return False, ("Rack hung for %ds in window creation. Quit the "
                               "running Rack and try again." % timeout)
            continue
        if _found(shots, slug):
            break
        why = _why(scratch)
        if attempt == tries - 1:
            return False, why

    # Rack files screenshots as <plugin slug>/<model slug>.png, and renders every
    # model of every loaded plugin in one pass -- so a three-module plugin costs
    # one Rack launch for all three panels, and this picks the one asked for.
    mine = os.path.join(shots, slug)
    src = os.path.join(mine, (model or slug) + ".png")
    if not os.path.exists(src) and os.path.isdir(mine):
        pngs = sorted(f for f in os.listdir(mine) if f.endswith(".png"))
        if pngs and not model:
            src = os.path.join(mine, pngs[0])
    if not os.path.exists(src):
        found = []
        for root, _, files in os.walk(shots):
            found += [f for f in files if f.endswith(".png")]
        return False, ("Rack rendered no PNG for %s (it rendered %d others). "
                       "Check tools/.rackshot/log.txt" % (model or slug, len(found)))
    os.makedirs(os.path.dirname(os.path.abspath(out_png)) or ".", exist_ok=True)
    shutil.copy(src, out_png)
    return True, out_png


def _found(shots, slug):
    d = os.path.join(shots, slug)
    return os.path.isdir(d) and any(f.endswith(".png") for f in os.listdir(d))


def _kill(scratch):
    """Reap an instance of ours that never got past window creation."""
    subprocess.run(["pkill", "-f", os.path.basename(os.path.abspath(scratch))],
                   check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _why(scratch):
    """Turn Rack's log into something actionable."""
    log = os.path.join(scratch, "log.txt")
    if not os.path.exists(log):
        return "Rack wrote no log; is the binary runnable?"
    tail = open(log, errors="replace").read()[-4000:]
    if "Fatal signal" in tail and "Window::Window" in tail:
        return ("Rack aborted bringing up a second OpenGL context. This is the "
                "window server refusing a second Rack; retrying usually clears "
                "it, and quitting the running Rack always does.")
    if "Fatal signal" in tail:
        return "Rack crashed; see %s" % log
    return "Rack exited without rendering; see %s" % log
