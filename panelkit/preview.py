"""tools/preview.html -- the panel composited with every widget at its true
radius and every label at its true font and size.

The fast loop. It answers "does anything collide" in a browser refresh rather
than a rebuild-and-restart, and because it is generated from the same solved
spec as the artwork and the C++, it cannot show you something Rack will not.
For the slow, authoritative loop, see rack.py.

Labels are drawn as SVG <text>, not HTML, because SVG positions text by its
baseline -- the same thing nvgText() does -- so what the browser shows is where
Rack will put it. Positioning by an HTML box would be off by the line height.
"""

import base64
import os

from . import palette as P
from . import spec as S

FONTDIR = "/Applications/VCV Rack 2 Pro.app/Contents/Resources/res/fonts"
ZOOM = 2
K = S.PX_PER_MM

#: how the browser mock draws each widget class, so it reads like Rack does
FACE = {
    "knob_large": ("#2a2a2a", 0.13, -0.58),
    "knob": ("#2a2a2a", 0.13, -0.58),
    "trim": ("#2a2a2a", 0.13, -0.58),
    "slider": ("#d8d4c6", 0.0, 0.0),
    "button": ("#3a3a3a", 0.0, 0.0),
    "bezel": ("#3a3a3a", 0.55, 0.0),
    "jack": (P.BRASS, 0.37, 0.0),
    "light": (P.PAPER, 0.0, 0.0),
    "light_small": (P.PAPER, 0.0, 0.0),
    "switch": ("#d8d4c6", 0.0, 0.0),
    "switch3": ("#d8d4c6", 0.0, 0.0),
}
ANCHOR = {"center": "middle", "left": "start", "right": "end"}


def _font(name):
    p = os.path.join(FONTDIR, name)
    return base64.b64encode(open(p, "rb").read()).decode() if os.path.exists(p) else None


def html(panel, sol, svg, labels):
    faces = []
    for fam, fn in (("Nunito", "Nunito-Bold.ttf"), ("Mono", "ShareTechMono-Regular.ttf")):
        b = _font(fn)
        if b:
            faces.append("@font-face{font-family:%s;src:url(data:font/ttf;base64,%s)}" % (fam, b))

    wpx, hpx = panel.w * K, panel.h * K          # the panel in Rack pixels
    body = svg.replace('<?xml version="1.0" encoding="UTF-8"?>', "").strip()
    # Reproject the mm viewBox into Rack pixels so widgets and labels, which are
    # sized in pixels, share one coordinate system with the artwork.
    body = body.split(">", 1)[1].rsplit("</svg>", 1)[0]

    o = ['<g transform="scale(%.6f)">%s</g>' % (K, body)]
    for name, x, y, kind in sol.widgets:
        hw, hh = S.EXTENT.get(kind, (S.RADIUS[kind],) * 2)
        hw, hh = hw * K, hh * K
        fill, inner, off = FACE[kind]
        if abs(hw - hh) < 1e-6:
            o.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s" stroke="#0006"/>'
                     % (x * K, y * K, hw, fill))
        else:
            o.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="2" '
                     'fill="%s" stroke="#0006"/>'
                     % (x * K - hw, y * K - hh, 2 * hw, 2 * hh, fill))
        if inner:
            o.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#0a1f10"/>'
                     % (x * K, y * K + off * hh, inner * hw))
        o.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="none" '
                 'stroke="#ff5a5a" stroke-opacity="0.5" stroke-dasharray="3 3"/>'
                 % (x * K - hw, y * K - hh, 2 * hw, 2 * hh, min(hw, hh)))
    for l in labels:
        if not l["text"]:
            continue
        if l.get("mirror"):
            flip = {"start": "end", "end": "start", "middle": "middle"}
            o.append('<text transform="translate(%.2f,%.2f) scale(-1,1)" x="0" y="0" '
                     'font-family="Nunito" font-size="%.2f" fill="%s" text-anchor="%s"'
                     '>%s</text>'
                     % (l["x"] * K, l["y"] * K, l["size"],
                        P.ink(l["ink"], l.get("ground", "light"))[1],
                        flip[ANCHOR[l["align"]]], _esc(l["text"])))
            continue
        o.append('<text x="%.2f" y="%.2f" font-family="Nunito" font-size="%.2f" '
                 'fill="%s" text-anchor="%s" letter-spacing="%.2f">%s</text>'
                 % (l["x"] * K, l["y"] * K, l["size"],
                    P.ink(l["ink"], l.get("ground", "light"))[1],
                    ANCHOR[l["align"]], l["tracking"], _esc(l["text"])))

    n_lab = len([l for l in labels if l["text"]])
    return "\n".join([
        '<!doctype html><meta charset="utf-8"><title>%s panel</title>' % _esc(panel.title),
        "<style>", "\n".join(faces),
        "body{background:#2b2b2b;margin:0;padding:24px;font-family:Nunito,sans-serif;"
        "color:#bbb;font-size:13px;line-height:1.5}",
        "svg{background:#111}",
        "</style>",
        '<svg xmlns="http://www.w3.org/2000/svg" width="%.0f" height="%.0f" '
        'viewBox="0 0 %.2f %.2f">%s</svg>'
        % (wpx * ZOOM, hpx * ZOOM, wpx, hpx, "\n".join(o)),
        '<p style="max-width:%.0fpx">Dashed rings are each widget&rsquo;s true radius. '
        '%d widgets, %d labels, %d section blocks, %d HP. Regenerate with '
        '<code>make panel</code>; for the authoritative render, <code>make vcv-preview</code>.'
        '</p>' % (wpx * ZOOM, len(sol.widgets), n_lab, len(sol.blocks), panel.hp),
    ])


def _esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
