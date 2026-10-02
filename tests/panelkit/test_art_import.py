#!/usr/bin/env python3
"""The Illustrator round trip: template -> import -> panel.

    python3 tests/panelkit/test_art_import.py

Runs against a scratch art/panels directory, so it never touches the real one.
Needs rsvg-convert for the pixel comparisons (SKIPs those without it).
"""

import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import redirect_stdout

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import art_import as AI        # noqa: E402
import art_templates as AT     # noqa: E402
from panelkit import art as ART, render   # noqa: E402
from panelkit.layout import solve         # noqa: E402

tmp = tempfile.mkdtemp()
ART.DIR = os.path.join(tmp, "panels")
fails = []


def check(name, ok, detail=""):
    print("%s  %s%s" % ("ok  " if ok else "FAIL", name, ("  -- " + detail) if detail else ""))
    if not ok:
        fails.append(name)


def run_import(path, *extra, module=None):
    args = argparse.Namespace(file=path, module=module, clear=None, dry_run=False,
                              strict=False, status=False, reset=None)
    for k, v in extra:
        setattr(args, k, v)
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = AI.main([path] + ([module] if module else []) + [
            "--%s" % k.replace("_", "-") if v is True else "--%s=%s" % (k.replace("_", "-"), v)
            for k, v in extra])
    return rc, buf.getvalue()


def png(svg, name, w_mm, h_mm):
    src = os.path.join(tmp, name + ".svg")
    open(src, "w").write(svg)
    dst = os.path.join(tmp, name + ".png")
    subprocess.run(["rsvg-convert", "-w", str(round(w_mm * 8)), "-h", str(round(h_mm * 8)),
                    "-o", dst, src], check=True)
    from PIL import Image
    return Image.open(dst).convert("RGBA")


def same(a, b, tol=6):
    from PIL import ImageChops
    d = ImageChops.difference(a, b)
    box = d.getbbox()
    if box is None:
        return True, 0
    worst = max(max(p) for p in d.getdata())
    return worst <= tol, worst


panel = [p for p in AI.specs().values() if p.slug == "Toll"][0]
sol = solve(panel)
have_rsvg = shutil.which("rsvg-convert") is not None
default_svg = render.panel_svg(panel, sol)

# 1. the template, imported unchanged, is the same panel ---------------------
template = AT.build_panel(panel, sol)
t1 = os.path.join(tmp, "Toll.svg")
open(t1, "w").write(template)
rc, out = run_import(t1)
check("template imports cleanly", rc == 0, out.strip().splitlines()[-1] if out.strip() else "")
a = ART.load("Toll")
check("all 14 stages stored", a is not None and len(a.stages) == 14,
      str(len(a.stages)) if a else "none")
check("signature recorded and current", a is not None and not a.stale(panel, sol))
if have_rsvg:
    ok, worst = same(png(default_svg, "d1", panel.w, panel.h),
                     png(render.panel_svg(panel, sol), "i1", panel.w, panel.h))
    check("imported panel renders as the generated one", ok, "max channel diff %d" % worst)

# 2. an Illustrator-shaped export: pt viewBox, CSS classes, per-layer transform ---
k = 72 / 25.4
ai = template.replace('viewBox="0 0 %.4f %.4f"' % (panel.w, panel.h),
                      'viewBox="0 0 %.4f %.4f"' % (panel.w * k, panel.h * k))
ai = re.sub(r'(<g id="EXPORT_\d\d_[^"]*")', r'\1 transform="scale(%.6f)"' % k, ai)
fills = {}


def to_class(m):
    fills.setdefault(m.group(1), "st%d" % len(fills))
    return 'class="%s"' % fills[m.group(1)]


ai = re.sub(r'fill="(#[0-9a-fA-F]{6})"', to_class, ai)
style = "<style type=\"text/css\">" + "".join(".%s{fill:%s;}" % (c, f) for f, c in fills.items()) \
    + "</style>"
ai = ai.replace("<g id=\"EXPORT_01_Paper\"", style + "<g id=\"EXPORT_01_Paper\"", 1)
ai = ai.replace('<?xml version="1.0" encoding="UTF-8"?>',
                '<?xml version="1.0" encoding="utf-8"?>\n<!-- Generator: Adobe Illustrator -->', 1)
t2 = os.path.join(tmp, "Toll.svg")
open(t2, "w").write(ai)
os.unlink(ART.path("Toll"))
rc, out = run_import(t2)
check("CSS classes, a points viewBox and layer transforms import", rc == 0, out.strip()[:300])
if have_rsvg and rc == 0:
    ok, worst = same(png(default_svg, "d2", panel.w, panel.h),
                     png(render.panel_svg(panel, sol), "i2", panel.w, panel.h))
    check("... and render as the generated panel", ok, "max channel diff %d" % worst)

# 3. one stage at a time ------------------------------------------------------
os.unlink(ART.path("Toll"))
only = re.search(r'<g id="EXPORT_11_Wells".*?</g>', template, re.S).group(0)
hdr = template[:template.index(">", template.index("<svg")) + 1]
t3 = os.path.join(tmp, "Toll.svg")
open(t3, "w").write(hdr + "\n" + only.replace("#181e15", "#ff0000").replace("#181E15", "#ff0000")
                    + "\n</svg>")
rc, out = run_import(t3)
check("a file with only one layer imports", rc == 0, out.strip()[:200])
mixed = render.panel_svg(panel, sol)
check("only that stage is replaced", ART.load("Toll") is not None
      and list(ART.load("Toll").stages) == ["11_Wells"]
      and mixed.count('id="EXPORT_11_Wells"') == 1 and "<rect" in mixed
      and "#ff0000" in mixed)

# 4. things Rack would silently get wrong are refused, and name the object ---------
os.unlink(ART.path("Toll"))
cases = {
    "clipping mask": ('<rect x="1" y="1" width="3" height="3" clip-path="url(#c)"/>', "clipping"),
    "dashed stroke": ('<path d="M0 0 L5 5" stroke="#7b9a6d" stroke-dasharray="1 1"/>', "dashed"),
    "live text": ('<text x="3" y="3">HELLO</text>', "type"),
    "raster image": ('<image width="3" height="3" href="x.png"/>', "raster"),
    "filter": ('<rect width="3" height="3" style="filter:url(#f)"/>', "live effects"),
}
for label, (frag, word) in cases.items():
    f = hdr + '\n<g id="EXPORT_07_Rules">' + frag + "</g>\n</svg>"
    path = os.path.join(tmp, "Toll.svg")
    open(path, "w").write(f)
    rc, out = run_import(path)
    check("refuses %s" % label, rc == 1 and word in out and "EXPORT_07_Rules" in out
          and not os.path.exists(ART.path("Toll")), out.strip().splitlines()[-1][:120])

bad = template.replace('viewBox="0 0 %.4f %.4f"' % (panel.w, panel.h),
                       'viewBox="0 0 %.4f %.4f"' % (panel.w, panel.h * 1.2))
open(path, "w").write(bad)
rc, out = run_import(path)
check("refuses an artboard that is not panel-shaped", rc == 1 and "artboard" in out)

unknown = hdr + '\n<g id="EXPORT_77_Mystery"><rect width="1" height="1"/></g></svg>'
open(path, "w").write(unknown)
rc, out = run_import(path)
check("refuses a stage that does not exist", rc == 1 and "EXPORT_77" in out)

# 5. a gradient keeps only its ends, and says so -----------------------------------
grad = (hdr + '<defs><linearGradient id="g" x1="0" y1="0" x2="10" y2="0" '
        'gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#7B9A6D"/>'
        '<stop offset="0.5" stop-color="#E4EAE1"/><stop offset="1" stop-color="#181E15"/>'
        '</linearGradient></defs><g id="EXPORT_04_Margin_ribbons">'
        '<rect x="1" y="10" width="10" height="20" fill="url(#g)"/></g></svg>')
open(path, "w").write(grad)
rc, out = run_import(path)
a = ART.load("Toll")
check("a three-stop gradient imports with a warning",
      rc == 0 and "3 stops" in out and a is not None
      and a.defs.count("<stop") == 2 and "linearGradient" in a.defs, out.strip()[:200])

# 6. staleness ----------------------------------------------------------------------
p = ART.path("Toll")
s = open(p).read()
open(p, "w").write(re.sub(r'data-layout="[0-9a-f]+"', 'data-layout="000000000000"', s))
ART._cache.clear()
check("a moved layout is reported as stale", ART.load("Toll").stale(panel, sol))

# 7. hardware ---------------------------------------------------------------------------
hw = AT.build_hardware()["PortIn"]
hp = os.path.join(tmp, "_Hardware_PortIn.svg")
open(hp, "w").write(hw)
rc, out = run_import(hp)
check("a jack imports", rc == 0, out.strip()[:160])
over = render.port_svg()
check("and replaces res/PortIn.svg", "EXPORT_" in over and render.port_svg(accent="#fff")
      .count("EXPORT_") == 0)
os.unlink(os.path.join(ART.DIR, "_Hardware_PortIn.svg"))

# 8. ink follows ground: dark paper under dark ink is reported, and only that ---------
os.unlink(ART.path("Toll"))
dark_paper = hdr + ('\n<g id="EXPORT_06_Section_blocks"><rect x="3" y="10.8" width="%.3f" '
                    'height="94" fill="#181e15"/></g></svg>' % (panel.w - 6))
open(path, "w").write(dark_paper)
if have_rsvg:
    rc, out = run_import(path)
    check("dark ground under dark words is warned about",
          rc == 0 and "needs 3:1" in out and "TRIG" in out, out.strip().splitlines()[-2][:140])
    check("... but not the words on the bands", "TOLL" not in out)
    os.unlink(ART.path("Toll"))
    open(path, "w").write(template)
    rc, out = run_import(path)
    check("the template itself raises no word warnings", "needs 3:1" not in out)

# 9. a rule across the bottom screws is warned about ------------------------------------
os.unlink(ART.path("Toll"))
span = hdr + ('\n<g id="EXPORT_07_Rules"><rect x="0" y="124.8" width="%.3f" height="0.5" '
              'fill="#7b9a6d"/></g></svg>' % panel.w)
open(path, "w").write(span)
rc, out = run_import(path)
check("full-width art below the screws is warned about", rc == 0 and "bottom screws" in out)

# 10. --strict turns warnings into refusals, --reset undoes an import ------------------------
os.unlink(ART.path("Toll"))
rc, out = run_import(path, ("strict", True))
check("--strict refuses on a warning", rc == 1 and not os.path.exists(ART.path("Toll")))

shutil.rmtree(tmp)
print("\n%d failed" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
