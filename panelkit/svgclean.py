"""Reduce an SVG saved from Illustrator to what Rack will draw.

Rack renders panels through nanosvg and its own svgDraw(), which silently discard
a good deal of SVG (see render.py, constraint 1). "Silently" is the trouble: a
clipped shape is drawn unclipped, a dashed rule comes out solid, a gradient loses
its middle, and nobody is told. So this does the opposite. It reads the file the
way an artist saved it -- Illustrator's `<style>` classes, `style=` attributes,
`<use>`, nested transforms, gradients -- and either rewrites it into plain
presentation attributes on a small set of shapes, or **refuses and says which
object, in which layer, and what to do about it in Illustrator**.

What survives:

    g path rect circle ellipse line polyline polygon, with
    fill stroke stroke-width stroke-linecap stroke-linejoin stroke-miterlimit
    fill-rule opacity fill-opacity stroke-opacity transform,
    and linear/radial gradients reduced to their first and last stop.

What is an error (it would change how the art looks if dropped):

    text, image, pattern, clipPath/mask/filter use, dashed strokes, blend modes,
    anything not in the list above.

What is a warning: a gradient with more than two stops, a colour outside the
family palette, a shape past the panel edge, art running the full width below the
bottom screws.
"""

import copy
import re
import xml.etree.ElementTree as ET

from . import palette as PAL

SVG = "http://www.w3.org/2000/svg"
XLINK = "http://www.w3.org/1999/xlink"

SHAPES = {"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"}
GEOMETRY = {
    "path": ("d",), "rect": ("x", "y", "width", "height", "rx", "ry"),
    "circle": ("cx", "cy", "r"), "ellipse": ("cx", "cy", "rx", "ry"),
    "line": ("x1", "y1", "x2", "y2"), "polyline": ("points",), "polygon": ("points",),
    "g": (),
}
PAINT = ("fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin",
         "stroke-miterlimit", "fill-rule", "opacity", "fill-opacity", "stroke-opacity",
         "display", "visibility")
IGNORED_TAGS = {"defs", "title", "desc", "metadata", "style", "namedview", "sodipodi",
                "pgf", "linearGradient", "radialGradient"}
BAD_TAGS = {
    "text": "type is never drawn by Rack: every word is set at runtime from the spec. "
            "Delete it, or Type > Create Outlines if it is decoration",
    "tspan": "type is never drawn by Rack. Type > Create Outlines",
    "image": "raster images are discarded by Rack. Image Trace > Expand, or redraw as shapes",
    "foreignObject": "not drawn by Rack",
    "pattern": "patterns are discarded. Object > Expand to turn the fill into shapes",
    "switch": "not drawn by Rack",
}
# Properties that Rack drops and that change the look when dropped.
BAD_PROPS = {
    "clip-path": "clipping masks are dropped. Object > Clipping Mask > Release, then trim "
                 "the shapes (Pathfinder > Intersect or Crop)",
    "mask": "opacity masks are dropped. Release it and bake the result into shapes",
    "filter": "live effects are dropped. Object > Expand Appearance",
}


def local(tag):
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


def unescape_id(s):
    """Illustrator writes '_x20_' for a space and the like."""
    return re.sub(r"_x([0-9A-Fa-f]{2,4})_", lambda m: chr(int(m.group(1), 16)), s)


# --- colour -----------------------------------------------------------------

_NAMED = {"black": "#000000", "white": "#ffffff", "red": "#ff0000", "none": "none"}


def norm_colour(v):
    """'#abc' / 'rgb(...)' / a name -> '#rrggbb', or the value unchanged."""
    v = v.strip()
    m = re.fullmatch(r"#([0-9a-fA-F]{3})", v)
    if m:
        return "#" + "".join(c * 2 for c in m.group(1)).lower()
    m = re.fullmatch(r"#([0-9a-fA-F]{6})", v)
    if m:
        return v.lower()
    m = re.fullmatch(r"rgb\(\s*([\d.]+%?)\s*,\s*([\d.]+%?)\s*,\s*([\d.]+%?)\s*\)", v)
    if m:
        ch = [round(float(c[:-1]) * 2.55) if c.endswith("%") else round(float(c))
              for c in m.groups()]
        return "#%02x%02x%02x" % tuple(max(0, min(255, c)) for c in ch)
    return _NAMED.get(v.lower(), v)


def palette_hexes():
    hexes = {}
    for n in dir(PAL):
        v = getattr(PAL, n)
        if isinstance(v, str) and re.fullmatch(r"#[0-9a-fA-F]{6}", v):
            hexes[v.lower()] = n
    return hexes


# --- css ---------------------------------------------------------------------

def parse_css(text):
    """{class name: {prop: value}} from Illustrator's `.st0{fill:#fff;}` blocks."""
    rules, other = {}, []
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for sel, body in re.findall(r"([^{}]+)\{([^}]*)\}", text):
        props = dict(
            (k.strip(), v.strip()) for k, v in
            (d.split(":", 1) for d in body.split(";") if ":" in d))
        for s in sel.split(","):
            s = s.strip()
            if re.fullmatch(r"\.[\w-]+", s):
                rules.setdefault(s[1:], {}).update(props)
            elif s:
                other.append(s)
    return rules, other


def parse_style(attr):
    return dict((k.strip(), v.strip()) for k, v in
                (d.split(":", 1) for d in (attr or "").split(";") if ":" in d))


# --- the cleaner ------------------------------------------------------------

class Report:
    def __init__(self):
        self.errors, self.warnings = [], []
        self.colours = {}

    def err(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))


class Cleaner:
    def __init__(self, root, report):
        self.rep = report
        self.root = root
        self.byid = {}
        self.css, other = {}, []
        for el in root.iter():
            if el.get("id"):
                self.byid[el.get("id")] = el
            if local(el.tag) == "style" and el.text:
                rules, o = parse_css(el.text)
                for k, v in rules.items():
                    self.css.setdefault(k, {}).update(v)
                other += o
        if other:
            report.warn("style", "selectors other than .class are ignored: %s"
                        % ", ".join(sorted(set(other))[:5]))
        self.gradients = {}      # id -> cleaned element, in the order first used
        self.where = []

    # -- properties --------------------------------------------------------
    def props(self, el):
        p = {}
        for k in PAINT + ("stop-color", "stop-opacity", "stroke-dasharray", "mix-blend-mode",
                          "clip-path", "mask", "filter"):
            if el.get(k) is not None:
                p[k] = el.get(k).strip()
        for c in (el.get("class") or "").split():
            p.update(self.css.get(c, {}))
        p.update(parse_style(el.get("style")))
        for k in BAD_PROPS:
            if el.get(k):
                p[k] = el.get(k)
        return p

    def here(self, el):
        label = unescape_id(el.get("id") or "") or local(el.tag)
        return " > ".join(self.where + [label]) if self.where else label

    # -- elements ------------------------------------------------------------
    def clean(self, el, depth=0):
        tag = local(el.tag)
        if tag in IGNORED_TAGS or not isinstance(el.tag, str) or (
                "}" in el.tag and not el.tag.startswith("{" + SVG)):
            return None
        where = self.here(el)
        if tag in BAD_TAGS:
            self.rep.err(where, "<%s>: %s" % (tag, BAD_TAGS[tag]))
            return None
        if tag == "use":
            return self.inline_use(el, depth)
        if tag not in SHAPES and tag != "g":
            self.rep.err(where, "<%s> is not something Rack draws" % tag)
            return None

        p = self.props(el)
        for k, why in BAD_PROPS.items():
            if p.get(k, "none") not in ("none", ""):
                self.rep.err(where, "%s: %s" % (k, why))
        if p.get("stroke-dasharray", "none") not in ("none", ""):
            self.rep.err(where, "dashed stroke is dropped by Rack, so it would draw solid. "
                                "Object > Path > Outline Stroke, or draw the dashes as shapes")
        if p.get("mix-blend-mode", "normal") not in ("normal", ""):
            self.rep.err(where, "blend mode %s is dropped. Flatten it into plain colour"
                         % p["mix-blend-mode"])

        out = ET.Element(tag)
        for a in GEOMETRY[tag]:
            if el.get(a) is not None:
                out.set(a, el.get(a))
        if el.get("transform"):
            out.set("transform", el.get("transform").strip())
        for k in PAINT:
            if k not in p:
                continue
            v = p[k]
            if k in ("fill", "stroke"):
                v = self.paint(v, where)
            out.set(k, v)
        if p.get("display") == "none" or p.get("visibility") == "hidden":
            return None
        if tag == "g":
            gid = unescape_id(el.get("id") or "")
            if gid:
                out.set("id", gid)
            self.where.append(gid or "g")
            for ch in el:
                c = self.clean(ch, depth + 1)
                if c is not None:
                    out.append(c)
            self.where.pop()
        return out

    def paint(self, v, where):
        m = re.fullmatch(r"url\(\s*#?([^)\s]+)\s*\)", v)
        if m:
            g = self.gradient(m.group(1), where)
            return "url(#%s)" % g if g else "none"
        c = norm_colour(v)
        if c.startswith("#"):
            self.rep.colours[c] = self.rep.colours.get(c, 0) + 1
        return c

    def gradient(self, gid, where):
        if gid in self.gradients:
            return gid
        el = self.byid.get(gid)
        if el is None:
            self.rep.err(where, "fill refers to #%s, which is not in the file" % gid)
            return None
        tag = local(el.tag)
        if tag == "pattern":
            self.rep.err(where, "pattern fill: patterns are dropped. Object > Expand")
            return None
        if tag not in ("linearGradient", "radialGradient"):
            self.rep.err(where, "fill refers to <%s>, which Rack cannot paint with" % tag)
            return None
        # stops may live on a gradient this one points at
        src, seen = el, set()
        while not [c for c in src if local(c.tag) == "stop"]:
            href = src.get("{%s}href" % XLINK) or src.get("href")
            if not href or href in seen or href[1:] not in self.byid:
                break
            seen.add(href)
            src = self.byid[href[1:]]
        stops = [c for c in src if local(c.tag) == "stop"]
        if len(stops) < 2:
            self.rep.err(where, "gradient #%s has fewer than two stops" % gid)
            return None
        if len(stops) > 2:
            self.rep.warn(where, "gradient #%s has %d stops; Rack keeps only the first and "
                                 "last, so the middle will not show" % (gid, len(stops)))
        g = ET.Element(tag)
        g.set("id", gid)
        keys = {"linearGradient": ("x1", "y1", "x2", "y2"),
                "radialGradient": ("cx", "cy", "r", "fx", "fy")}[tag]
        for a in keys + ("gradientUnits", "gradientTransform", "spreadMethod"):
            if el.get(a) is not None:
                g.set(a, el.get(a))
        for s in (stops[0], stops[-1]):
            sp = self.props(s)
            st = ET.SubElement(g, "stop")
            st.set("offset", s.get("offset") or "0")
            col = norm_colour(sp.get("stop-color", "#000000"))
            if col.startswith("#"):
                self.rep.colours[col] = self.rep.colours.get(col, 0) + 1
            st.set("stop-color", col)
            if sp.get("stop-opacity"):
                st.set("stop-opacity", sp["stop-opacity"])
        self.gradients[gid] = g
        return gid

    def inline_use(self, el, depth):
        where = self.here(el)
        href = el.get("{%s}href" % XLINK) or el.get("href") or ""
        tgt = self.byid.get(href[1:])
        if tgt is None or depth > 8:
            self.rep.err(where, "<use> of %s cannot be resolved. Object > Expand/Ungroup the "
                                "symbol instance" % (href or "nothing"))
            return None
        if local(tgt.tag) == "symbol":
            wrapper = ET.Element("g")
            for ch in tgt:
                wrapper.append(copy.deepcopy(ch))
            tgt = wrapper
        g = ET.Element("g")
        tf = (el.get("transform") or "").strip()
        dx, dy = el.get("x"), el.get("y")
        if dx or dy:
            tf = (tf + " translate(%s %s)" % (dx or 0, dy or 0)).strip()
        if tf:
            g.set("transform", tf)
        for k in PAINT:
            if el.get(k) is not None:
                g.set(k, el.get(k))
        c = self.clean(copy.deepcopy(tgt), depth + 1)
        if c is not None:
            g.append(c)
        return g


# --- finding the layers ------------------------------------------------------

def strip_doctype(text):
    text = re.sub(r"<!DOCTYPE[^\[>]*(\[.*?\])?\s*>", "", text, flags=re.S)
    return re.sub(r"&ns_\w+;", "urn:x", text)


def find_layers(root, pattern):
    """[(match, element)] for every group whose id matches `pattern`, not
    descending into one once found."""
    found = []

    def walk(el):
        for ch in el:
            if local(ch.tag) == "g":
                m = pattern.match(unescape_id(ch.get("id") or ""))
                if m:
                    found.append((m, ch))
                    continue
            walk(ch)
    walk(root)
    return found


def view(root):
    vb = root.get("viewBox")
    if not vb:
        return None
    x, y, w, h = (float(v) for v in re.split(r"[ ,]+", vb.strip()))
    return x, y, w, h


def serialize(el):
    return ET.tostring(el, encoding="unicode", short_empty_elements=True)


# --- geometry, for the warnings --------------------------------------------

def _mat_mul(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def parse_transform(s):
    import math
    m = (1, 0, 0, 1, 0, 0)
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s or ""):
        v = [float(x) for x in re.split(r"[ ,]+", args.strip()) if x]
        if name == "matrix" and len(v) == 6:
            t = tuple(v)
        elif name == "translate":
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale":
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate":
            c, s_ = math.cos(math.radians(v[0])), math.sin(math.radians(v[0]))
            t = (c, s_, -s_, c, 0, 0)
            if len(v) == 3:
                t = _mat_mul(_mat_mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        else:
            continue
        m = _mat_mul(m, t)
    return m


def _apply(m, x, y):
    return m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]


def path_points(d):
    """Endpoints and control points of an SVG path: a conservative bounding box
    (a curve never leaves the hull of its control points)."""
    toks = re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    pts, i, cmd, x, y, sx, sy = [], 0, None, 0.0, 0.0, 0.0, 0.0
    n = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7, "Z": 0}
    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in "Zz":
                x, y = sx, sy
                continue
        if cmd is None:
            break
        k = n[cmd.upper()]
        a = [float(t) for t in toks[i:i + k]]
        if len(a) < k:
            break
        i += k
        rel = cmd.islower()
        c = cmd.upper()
        if c == "H":
            x = a[0] + (x if rel else 0)
            pts.append((x, y))
        elif c == "V":
            y = a[0] + (y if rel else 0)
            pts.append((x, y))
        elif c == "A":
            x, y = a[5] + (x if rel else 0), a[6] + (y if rel else 0)
            pts.append((x, y))
        else:
            for j in range(0, k, 2):
                px, py = a[j] + (x if rel else 0), a[j + 1] + (y if rel else 0)
                pts.append((px, py))
            x, y = pts[-1]
            if c == "M":
                sx, sy = x, y
                cmd = "l" if rel else "L"
    return pts


def bbox(el, m=(1, 0, 0, 1, 0, 0)):
    """(x0, y0, x1, y1) of a cleaned element in the coordinates above it, or None."""
    m = _mat_mul(m, parse_transform(el.get("transform")))
    tag = el.tag
    pts = []
    f = lambda a, d=0.0: float(el.get(a) or d)
    if tag == "g":
        bs = [b for b in (bbox(c, m) for c in el) if b]
        if not bs:
            return None
        return (min(b[0] for b in bs), min(b[1] for b in bs),
                max(b[2] for b in bs), max(b[3] for b in bs))
    if tag == "rect":
        x, y, w, h = f("x"), f("y"), f("width"), f("height")
        pts = [(x, y), (x + w, y), (x, y + h), (x + w, y + h)]
    elif tag == "circle":
        cx, cy, r = f("cx"), f("cy"), f("r")
        pts = [(cx - r, cy - r), (cx + r, cy + r), (cx - r, cy + r), (cx + r, cy - r)]
    elif tag == "ellipse":
        cx, cy, rx, ry = f("cx"), f("cy"), f("rx"), f("ry")
        pts = [(cx - rx, cy - ry), (cx + rx, cy + ry), (cx - rx, cy + ry), (cx + rx, cy - ry)]
    elif tag == "line":
        pts = [(f("x1"), f("y1")), (f("x2"), f("y2"))]
    elif tag in ("polyline", "polygon"):
        v = [float(t) for t in re.findall(r"-?\d*\.?\d+(?:[eE][-+]?\d+)?", el.get("points", ""))]
        pts = list(zip(v[0::2], v[1::2]))
    elif tag == "path":
        pts = path_points(el.get("d", ""))
    if not pts:
        return None
    pts = [_apply(m, x, y) for x, y in pts]
    sw = 0.0
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs) - sw, min(ys) - sw, max(xs) + sw, max(ys) + sw
