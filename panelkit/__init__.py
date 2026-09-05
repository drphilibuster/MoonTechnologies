"""panelkit -- one panel pipeline for the whole plugin family.

A module describes its panel once, in tools/panels/<Module>.py, as a Panel spec.
Running that file regenerates, from that single description:

    res/<slug>.svg                 the panel artwork, plus the components layer
    res/ScrewHex.svg               the family screw
    res/PortIn.svg                 the family jacks
    res/PortOut.svg
    src/PanelTheme.hpp             palette, hardware and the text kit -- shared
    src/<Module>/Panel.hpp         this panel's geometry and silkscreen
    tools/previews/<slug>.html     a browser mock at true radii and true fonts
    tools/previews/<slug>.png      the panel as Rack itself renders it   (--vcv)

Nothing is typed twice, so nothing can drift. The layout solver derives every
vertical coordinate from the rows you declare, the linter refuses geometry that
would collide with a screw or an edge, and the VCV render is the last word.

Usage inside tools/panels/<Module>.py:

    from panelkit import *
    P = Panel(slug="Retroactive", title="RETROACTIVE", form="FORM 1040-X", hp=12)
    ...
    if __name__ == "__main__":
        build(P, root=<repo root>)
"""

import os
import sys

from .spec import (BRAND, Panel, Section, Row, Widget, Glass, Trace, FreeLabel, Plate,
                   RADIUS, LABEL_SIDE, PX_PER_MM, MM_PER_PX, HP_MM, PANEL_H)
from . import palette
from .palette import (INK, FELT, BAND, GLASS as GLASS_COLOUR, RULE, LIME, MINT,
                      CLAY, PAPER, SAGE)
from .layout import solve, SCALE
from . import render, emit, preview, lint, rack

__all__ = ["BRAND", "Panel", "Section", "Row", "Widget", "Glass", "Trace", "FreeLabel",
           "Plate",
           "Knob", "BigKnob", "Trim", "Button", "Bezel", "Jack", "Light",
           "Switch", "Switch3",
           "Slider", "RADIUS", "build", "palette", "INK", "FELT", "BAND",
           "GLASS_COLOUR", "RULE", "LIME", "MINT", "CLAY", "PAPER", "SAGE",
           "PX_PER_MM", "MM_PER_PX", "HP_MM", "PANEL_H"]


# --- shorthand constructors, so a spec reads as a list of controls -----------
def _mk(kind, default_size, default_ink):
    def f(name, x, label="", ink=None, size=None, **kw):
        return Widget(name=name, x=x, kind=kind, label=label,
                      ink=ink or default_ink, size=size or default_size, **kw)
    f.__name__ = kind
    return f


BigKnob = _mk("knob_large", 7.0, "PAPER")
Knob = _mk("knob", 7.0, "PAPER")
Trim = _mk("trim", 6.2, "SAGE")
Slider = _mk("slider", 7.0, "PAPER")
Button = _mk("button", 6.2, "SAGE")
Bezel = _mk("bezel", 6.6, "LIME")
Jack = _mk("jack", 6.2, "SAGE")
Light = _mk("light", 5.8, "LIME")
# Two-position CKSS and three-position CKSSThree. Shorthands like the rest,
# rather than the bare Widget(name, x, "switch", label) a switch used to need:
# that form takes its arguments in a different order from every other control
# on the panel, which is a trap worth closing rather than documenting.
Switch = _mk("switch", 6.2, "SAGE")
Switch3 = _mk("switch3", 6.2, "SAGE")


def plugin_slug(root):
    """The plugin slug from plugin.json -- the directory name Rack loads under,
    which is what the screenshot renderer needs to find its output."""
    import json
    with open(os.path.join(root, "plugin.json")) as f:
        return json.load(f)["slug"]


def build(panel, root=None, vcv=False, force=False, quiet=False):
    """Generate every artefact for `panel`. Returns 0 on success."""
    root = root or os.getcwd()
    argv = sys.argv[1:]
    vcv = vcv or "--vcv" in argv
    force = force or "--force" in argv

    sol = solve(panel)
    labels = emit.masthead(panel) + sol.labels
    problems = lint.check(panel, sol, labels)

    if problems:
        print("panelkit: %d problem%s with %s"
              % (len(problems), "" if len(problems) == 1 else "s", panel.slug))
        for p in problems:
            print("  - " + p)
        if not force:
            print("  (nothing written; fix the spec, or pass --force to write anyway)")
            return 1

    def write(rel, text):
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        if not quiet:
            print("  wrote %s" % rel)

    svg = render.panel_svg(panel, sol)
    write("res/%s.svg" % panel.slug, svg)
    write("res/ScrewHex.svg", render.screw_svg())
    write("res/PortIn.svg", render.port_svg())
    write("res/PortOut.svg", render.port_svg(accent=palette.MINT))
    # Two headers, split by linkage: the shared vocabulary once for the whole
    # plugin, this panel's numbers next to the module that uses them. See the
    # note above emit.common(). PanelTheme.hpp is byte-identical whichever panel
    # writes it, so every panel writing it keeps `make panel-<Module>` complete
    # on its own.
    write("src/PanelTheme.hpp", emit.common())
    write("src/%s/Panel.hpp" % panel.src_dir, emit.panel_header(panel, sol))
    write("tools/previews/%s.html" % panel.slug, preview.html(panel, sol, svg, labels))

    if not quiet:
        print("panelkit: %s -- %d HP, %d widgets, %d labels, %d blocks, %s density"
              % (panel.slug, panel.hp, len(sol.widgets),
                 len([l for l in labels if l["text"]]), len(sol.blocks), panel.density))

    # Advisory, never fatal: the panel is already written. See lint.check_sources.
    strays = lint.check_sources(root)
    if strays and not quiet:
        print("panelkit: %d place%s still draw text by hand:"
              % (len(strays), "" if len(strays) == 1 else "s"))
        for s_ in strays:
            print("  - " + s_)

    if vcv:
        print("  rendering through VCV Rack ...")
        ok, msg = rack.render(root, plugin_slug(root), os.path.join(
            root, "tools/previews/%s.png" % panel.slug), model=panel.slug)
        print("  %s %s" % ("wrote tools/previews/%s.png from" % panel.slug if ok
                           else "VCV render failed:", msg if not ok else "Rack"))
        if not ok:
            return 2
    return 0
