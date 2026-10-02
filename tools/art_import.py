#!/usr/bin/env python3
"""Bring art drawn in Illustrator back into the family.

    python3 tools/art_import.py Toll.svg                 # a panel
    python3 tools/art_import.py Toll.svg Toll            # ... naming the module
    python3 tools/art_import.py _Hardware_PortIn.svg     # a jack or the screw
    python3 tools/art_import.py Toll.svg --clear 12,13   # stages to draw as empty
    python3 tools/art_import.py Toll.svg --dry-run       # check, write nothing
    python3 tools/art_import.py --status                 # what is imported, what is stale
    python3 tools/art_import.py --reset Toll             # back to the generated art

Starting point is the template from `make art`: open it in Illustrator, redraw
the `EXPORT_NN_*` layers, save as SVG (see art/README.md for the options), and
import. What happens:

  1. The file is cleaned down to what Rack draws (panelkit/svgclean.py). Anything
     that Rack would silently drop and that would change the look -- clipping
     masks, dashed strokes, type, raster images, patterns, effects -- is an
     **error that names the object and the fix**, and nothing is written.
  2. Each `EXPORT_NN_*` layer becomes that stage of the panel. A stage the file
     does not have stays generated, so art can arrive a layer at a time. Anything
     outside an EXPORT layer (the GUIDES) is ignored; so is a hidden layer's
     content, which makes that stage empty.
  3. The panel is rendered with the new art and every word is checked against the
     ground under it: pale ink needs a dark ground, dark ink a pale one. Only
     words the new art breaks are reported.
  4. The cleaned art is stored as art/panels/<Module>.svg -- committed source -- and
     `make panel` draws the panel from it from then on.

Exit status is 1 if the art was refused.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, ROOT)

from panelkit import art as ART, emit, palette as PAL, render, svgclean as C   # noqa: E402
from panelkit.layout import solve, label_box                                    # noqa: E402

EDGE_CLEAR = 30 * 25.4 / 75       # 10.16
FLOOR = 365 * 25.4 / 75           # 123.61
HARDWARE = {"ScrewHex": (15.0, 14.9989)}
for _n in ("PortIn", "PortOut", "PortTrigIn", "PortTrigOut", "PortInMain", "PortOutMain",
           "PortTrigInMain", "PortTrigOutMain"):
    HARDWARE[_n] = (23.7, 23.7)


def specs():
    import importlib.util
    out = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "tools", "panels", "*.py"))):
        slug = os.path.splitext(os.path.basename(f))[0]
        sp = importlib.util.spec_from_file_location("spec_" + slug, f)
        mod = importlib.util.module_from_spec(sp)
        sp.loader.exec_module(mod)
        out[slug] = mod.P
    return out


def read(path):
    text = C.strip_doctype(open(path, encoding="utf-8").read())
    try:
        return ET.fromstring(text)
    except ET.ParseError as e:
        raise SystemExit("%s is not well-formed SVG: %s" % (path, e))


def stage_attrs(layer, matrix):
    """Attributes for the stored stage group: the artboard-to-panel transform,
    followed by anything the layer itself carried (its own transform, opacity)."""
    tf = matrix
    attrs = {}
    if layer is not None:
        own = layer.get("transform")
        if own:
            tf = (tf + " " + own).strip()
        for k, v in layer.attrib.items():
            if k not in ("id", "transform"):
                attrs[k] = v
    out = ""
    if tf:
        out += ' transform="%s"' % tf
    for k, v in attrs.items():
        out += ' %s="%s"' % (k, v.replace('"', "&quot;"))
    return out


def fmt(v):
    return ("%.6f" % v).rstrip("0").rstrip(".")


def artboard(root, w_mm, h_mm, rep, what):
    """-> the transform that maps the file's units onto millimetres, or None."""
    vb = C.view(root)
    if vb is None:
        rep.err(what, "the SVG has no viewBox. Save from Illustrator with the artboard as "
                      "the whole drawing (File > Save As > SVG)")
        return None
    x, y, vw, vh = vb
    if abs(vw / vh - w_mm / h_mm) / (w_mm / h_mm) > 0.002:
        rep.err(what, "the artboard is not panel-shaped: it is %s x %s units (ratio %.4f) and "
                      "the panel is %.2f x %.2f mm (ratio %.4f). Illustrator's Save As SVG "
                      "sizes the file to all the artwork, not the artboard, so anything "
                      "hanging off it (the GUIDES layers do) changes the size. Use File > "
                      "Export > Export As > SVG with Use Artboards ticked, or hide the "
                      "GUIDES layers before saving" % (fmt(vw), fmt(vh), vw / vh, w_mm,
                                                        h_mm, w_mm / h_mm))
        return None
    s = w_mm / vw
    parts = []
    if abs(s - 1) > 1e-9:
        parts.append("scale(%s)" % fmt(s))
    if abs(x) > 1e-9 or abs(y) > 1e-9:
        parts.append("translate(%s %s)" % (fmt(-x), fmt(-y)))
    return " ".join(parts), s, x, y


# --- the stages ---------------------------------------------------------------

def collect(root, rep, matrix_info, pattern, name_of):
    """{stage name: (attrs, body)} cleaned, plus the cleaner (for gradients)."""
    cl = C.Cleaner(root, rep)
    matrix, s, ox, oy = matrix_info
    stages, seen = {}, set()
    layers = C.find_layers(root, pattern)
    if not layers:
        rep.err("file", "no EXPORT_NN_* layers found. Keep the template's layer names "
                        "(ids are written from layer names on export)")
    for m, el in layers:
        number = m.group(1)
        name = name_of(number, C.unescape_id(el.get("id")))
        if name is None:
            rep.err(C.unescape_id(el.get("id") or "?"),
                    "EXPORT_%s is not a stage of this file; the stages are %s"
                    % (number, ", ".join("EXPORT_" + s_ for s_ in ART.STAGES)))
            continue
        cl.where = []
        cleaned = cl.clean(el)
        if cleaned is None:
            rep.warn(C.unescape_id(el.get("id")), "layer is hidden, so this stage will be drawn empty")
            stages[name] = (stage_attrs(None, matrix), "", None)
            continue
        body = "".join(C.serialize(ch) for ch in cleaned)
        if name in stages:                       # a split layer: Illustrator numbers duplicates
            a, b0, g0 = stages[name]
            stages[name] = (a, b0 + body, g0)
        else:
            stages[name] = (stage_attrs(cleaned, matrix), body, cleaned)
        seen.add(name)
    return stages, cl


def defs_of(cl):
    return "".join(C.serialize(g) for g in cl.gradients.values())


# --- the checks ----------------------------------------------------------------

def geometry_warnings(stages, panel, rep, matrix_info):
    """Art past the panel edge, or running the full width below the bottom screws."""
    w, h = panel.w, panel.h
    for name, (attrs, _body, cleaned) in stages.items():
        if cleaned is None:
            continue
        m = C.parse_transform(attrs.split('transform="')[1].split('"')[0]
                              if 'transform="' in attrs else "")
        for ch in cleaned:
            b = C.bbox(ch, m)
            if not b:
                continue
            x0, y0, x1, y1 = b
            if x0 < -0.3 or y0 < -0.3 or x1 > w + 0.3 or y1 > h + 0.3:
                rep.warn("EXPORT_" + name, "an object runs %.1f mm past the panel edge; Rack "
                         "clips it, but check it is meant to" % max(-x0, -y0, x1 - w, y1 - h))
            skip = name in ("01_Paper", "02_Masthead_band", "03_Footer_band",
                            "04_Margin_ribbons")
            if (not skip and x1 - x0 >= w - 2 * EDGE_CLEAR and y1 > FLOOR + 0.05
                    and y0 < h):
                rep.warn("EXPORT_" + name, "an object spans the full width below %.2f mm, where "
                         "the bottom screws sit (art/README.md, rules)" % FLOOR)


def palette_warnings(rep):
    known = C.palette_hexes()
    off = {c: n for c, n in rep.colours.items() if c not in known}
    if off:
        top = sorted(off.items(), key=lambda kv: -kv[1])[:6]
        rep.warn("palette", "%d colour%s outside the family palette: %s. Load "
                 "art/Moon Technologies.ase to stay inside it"
                 % (len(off), "" if len(off) == 1 else "s",
                    ", ".join("%s (x%d)" % kv for kv in top)))


def lum(rgb):
    def f(c):
        c /= 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def ratio(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def render_png(svg, w_mm, h_mm, px_per_mm=10):
    from PIL import Image
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "p.svg"), os.path.join(d, "p.png")
        open(src, "w").write(svg)
        subprocess.run(["rsvg-convert", "-w", str(round(w_mm * px_per_mm)),
                        "-h", str(round(h_mm * px_per_mm)), "-b", "#ff00ff", "-o", dst, src],
                       check=True, capture_output=True)
        im = Image.open(dst).convert("RGB")
        im.load()
    return im


def bad_words(panel, sol, svg, px=10):
    """{label text: (ratio, fraction below 3:1)} for words whose ground fails them."""
    labels = [l for l in emit.masthead(panel) + sol.labels if l["text"]]
    im = render_png(svg, panel.w, panel.h, px)
    out = {}
    for l in labels:
        ink = PAL.rgb(PAL.ink(l["ink"], l.get("ground", "light"))[1])
        x0, y0, x1, y1 = label_box(l)
        box = [max(0, int(x0 * px)), max(0, int(y0 * px)),
               min(im.width, int(x1 * px) + 1), min(im.height, int(y1 * px) + 1)]
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        region = im.crop(box)
        px_list = list(getattr(region, "get_flattened_data", region.getdata)())
        low = sum(1 for p in px_list if ratio(ink, p) < 3.0)
        worst = min(ratio(ink, p) for p in px_list)
        if low / len(px_list) > 0.2:
            out[(l["text"], round(l["x"], 2), round(l["y"], 2))] = (worst, low / len(px_list))
    return out


# --- writing ---------------------------------------------------------------------

def write_panel(path, panel, sig, stages, defs, source):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- Imported from %s by tools/art_import.py. This is source art: commit it. "
             "To edit, run `make art`, open art/templates/%s.svg in Illustrator, and import "
             "again. -->" % (os.path.basename(source), panel.slug),
             '<svg xmlns="http://www.w3.org/2000/svg" width="%.4fmm" height="%.4fmm" '
             'viewBox="0 0 %.4f %.4f" version="1.1" data-module="%s" data-layout="%s">'
             % (panel.w, panel.h, panel.w, panel.h, panel.slug, sig)]
    if defs:
        lines.append("<defs>%s</defs>" % defs)
    for name in ART.STAGES:
        if name in stages:
            attrs, body, _ = stages[name]
            lines += ['<g id="EXPORT_%s"%s>' % (name, attrs), body, "</g>"]
    lines += ["</svg>", ""]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="utf-8").write("\n".join(lines))


def do_panel(args, panels, slug):
    panel = panels[slug]
    sol = solve(panel)
    rep = C.Report()
    root = read(args.file)
    mi = artboard(root, panel.w, panel.h, rep, os.path.basename(args.file))
    if mi is None:
        return finish(rep, args)
    pattern = re.compile(r"^EXPORT_(\d\d)")
    stages, cl = collect(root, rep, mi, pattern, lambda n, _id: ART.stage_name(n))
    for n in (args.clear or "").split(","):
        if n.strip():
            name = ART.stage_name(n.strip().zfill(2))
            if name is None:
                rep.err("--clear", "%s is not a stage number (01-14)" % n)
            else:
                stages[name] = (stage_attrs(None, mi[0]), "", None)
    if rep.errors:
        return finish(rep, args)
    geometry_warnings(stages, panel, rep, mi)
    palette_warnings(rep)

    sig = ART.signature(panel, sol)
    new = ART.Art({k: (v[0], v[1]) for k, v in stages.items()}, defs_of(cl), sig)

    try:
        before = bad_words(panel, sol, render.panel_svg(panel, sol, art=ART.Art({}, "", "")))
        after = bad_words(panel, sol, render.panel_svg(panel, sol, art=new))
        for key, (worst, frac) in sorted(after.items(), key=lambda kv: kv[0][2]):
            if key not in before:
                rep.warn("word %r" % key[0], "its ink only reaches %.1f:1 against the new "
                         "ground (needs 3:1) at %.0f%% of the label, at x=%.1f y=%.1f mm. "
                         "Pale ink needs a dark ground, dark ink a pale one"
                         % (worst, frac * 100, key[1], key[2]))
    except FileNotFoundError:
        rep.warn("contrast", "rsvg-convert not found, so labels were not checked against "
                             "the new ground (brew install librsvg)")

    if args.strict and rep.warnings:
        rep.errors.append("--strict: warnings count as errors")
    if rep.errors:
        return finish(rep, args)
    if not args.dry_run:
        write_panel(ART.path(slug), panel, sig, stages, new.defs, args.file)
    done = ", ".join(s.split("_", 1)[0] for s in ART.STAGES if s in stages)
    print("%s %s: stages %s from %s; the rest stay generated"
          % ("would import" if args.dry_run else "imported", slug, done,
             os.path.basename(args.file)))
    if not args.dry_run:
        print("  wrote %s -- now `make panel-%s`, and look at tools/previews/%s.png "
              "(`make vcv-%s`)" % (os.path.relpath(ART.path(slug), ROOT), slug, slug, slug))
    return finish(rep, args)


def do_hardware(args, name):
    w, h = HARDWARE[name]
    rep = C.Report()
    root = read(args.file)
    mi = artboard(root, w, h, rep, os.path.basename(args.file))
    if mi is None:
        return finish(rep, args)
    pattern = re.compile(r"^EXPORT_(\d\d)")
    stages, cl = collect(root, rep, mi, pattern,
                         lambda n, i: re.sub(r"[^A-Za-z0-9_]", "_", i[len("EXPORT_"):]) or None)
    if rep.errors:
        return finish(rep, args)
    palette_warnings(rep)
    if not args.dry_run:
        path = os.path.join(ART.DIR, "_Hardware_%s.svg" % name)
        os.makedirs(ART.DIR, exist_ok=True)
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 '<svg xmlns="http://www.w3.org/2000/svg" width="%gpx" height="%gpx" '
                 'viewBox="0 0 %g %g" version="1.1">' % (w, h, w, h)]
        if defs_of(cl):
            lines.append("<defs>%s</defs>" % defs_of(cl))
        for name_, (attrs, body, _) in sorted(stages.items()):
            lines += ['<g id="EXPORT_%s"%s>' % (name_, attrs), body, "</g>"]
        lines += ["</svg>", ""]
        open(path, "w", encoding="utf-8").write("\n".join(lines))
        print("imported %s: %d layers; wrote %s -- now `make panel` (it rewrites res/%s.svg)"
              % (name, len(stages), os.path.relpath(path, ROOT), name))
    return finish(rep, args)


def finish(rep, args):
    for w in rep.warnings:
        print("  warning: " + w)
    if rep.errors:
        print("refused: %d problem%s, nothing written" % (len(rep.errors),
                                                          "" if len(rep.errors) == 1 else "s"))
        for e in rep.errors:
            print("  - " + e)
        return 1
    return 0


def status(panels):
    rows = []
    for slug in sorted(panels):
        a = ART.load(slug)
        if a is None:
            continue
        stale = a.stale(panels[slug], solve(panels[slug]))
        rows.append("  %-18s %2d stages  %s" % (
            slug, len(a.stages),
            "STALE: the layout has moved since this was drawn; re-open the template and "
            "re-import" if stale else "current"))
    for f in sorted(glob.glob(os.path.join(ART.DIR, "_Hardware_*.svg"))):
        rows.append("  %-18s hardware" % os.path.basename(f)[1:-4])
    print("imported art:" if rows else "no imported art; every panel is generated")
    print("\n".join(rows))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?")
    ap.add_argument("module", nargs="?")
    ap.add_argument("--clear", help="comma-separated stage numbers to draw as empty")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--strict", action="store_true", help="warnings are errors")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--reset", metavar="MODULE")
    args = ap.parse_args(argv)

    panels = specs()
    if args.status:
        return status(panels)
    if args.reset:
        p = ART.path(args.reset)
        h = os.path.join(ART.DIR, "_Hardware_%s.svg" % args.reset)
        for q in (p, h):
            if os.path.exists(q):
                os.unlink(q)
                print("removed %s -- now `make panel`" % os.path.relpath(q, ROOT))
                return 0
        print("nothing imported for %s" % args.reset)
        return 1
    if not args.file:
        ap.print_help()
        return 2

    stem = os.path.splitext(os.path.basename(args.file))[0]
    target = args.module or stem
    if target.startswith("_Hardware_"):
        target = target[len("_Hardware_"):]
    if target in HARDWARE:
        return do_hardware(args, target)
    if target in panels:
        return do_panel(args, panels, target)
    print("%r is not a module or a hardware part. Name it: art_import.py FILE MODULE\n"
          "modules: %s\nhardware: %s" % (target, ", ".join(sorted(panels)),
                                         ", ".join(sorted(HARDWARE))))
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
