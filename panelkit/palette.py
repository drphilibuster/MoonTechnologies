"""The one palette. Every panel, every generator, every runtime label reads it
from here, so nothing can drift.

The five anchors are the "High Contrast" palette (icolorpalette.com/color/7b9a6d):
two near-blacks, two off-whites and one sage green. The panel is a banknote
now rather than a tax form on green felt: a pale engraved face, dark ink, sage
guilloche, and dark bands top and bottom that the pale text sits on. The names
below are still a stationery metaphor -- ink, paper, felt, band, glass, rule --
and still describe the *role* a colour plays, not its hue, so a future re-skin
only has to change the hex.

Anything not one of the five anchors is derived from them (lighten/darken/mix),
so the family stays inside the palette even where it needs a luminous LED tint
or a legible secondary ink on the pale face.
"""


def rgb(hexstr):
    """'#00e95c' -> (0x00, 0xe9, 0x5c)"""
    h = hexstr.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def _hex(r, g, b):
    return "#%02x%02x%02x" % (max(0, min(255, int(round(r)))),
                              max(0, min(255, int(round(g)))),
                              max(0, min(255, int(round(b)))))


def mix(a, b, t):
    """Linear blend from colour a to colour b, t in 0..1."""
    ra, ga, ba = rgb(a)
    rb, gb, bb = rgb(b)
    return _hex(ra + (rb - ra) * t, ga + (gb - ga) * t, ba + (bb - ba) * t)


# --- the five anchors --------------------------------------------------------
A_DARK_GREEN = "#181E15"    # engraving ink, bands
A_PALE_GREEN = "#E4EAE1"    # the paper
A_SAGE       = "#7B9A6D"    # the guilloche
A_DARK_PLUM  = "#1B151E"    # displays and wells
A_PALE_PLUM  = "#E7E1EA"    # section blocks

# --- the face ----------------------------------------------------------------
PAPER = A_PALE_GREEN   # the board itself: the note's paper, and pale ink on dark bands
FELT  = A_PALE_PLUM    # section blocks: the raised panel a form is filled out on
INK   = A_DARK_GREEN   # the engraving ink: text on the face, the masthead and footer
BAND  = A_DARK_GREEN   # masthead / footer bands (the same ink, named for its role)
GLASS = A_DARK_PLUM    # displays, read-outs and the well behind every widget
RULE  = A_SAGE         # the guilloche: rules, frames, traces, ornament

# --- accents -----------------------------------------------------------------
# "look here" (index tabs, the primary ring, traces) is the sage on the face and
# a lifted sage on dark ground. Outputs ring in a mint lifted from the same
# green; warnings are the plum, lifted -- warm-ish, and still inside the palette.
LIME  = mix(A_SAGE, A_PALE_GREEN, 0.45)   # accent on dark ground, LED emitters
MINT  = mix(A_SAGE, "#ffffff", 0.35)      # affirmatives: outputs, locks, accepted filings
CLAY  = mix(A_DARK_PLUM, A_PALE_PLUM, 0.62)   # "action required": refusals, warnings

# --- silkscreen ink ----------------------------------------------------------
# Text on the dark bands and in displays is PAPER (primary) and SAGE (secondary),
# as before. Text on the pale face is INK (primary) and a darkened sage
# (secondary) -- the emitter picks by ground, see ink().
SAGE       = A_SAGE
SAGE_DARK  = mix(A_SAGE, A_DARK_GREEN, 0.45)  # secondary ink on the pale face
MINT_DARK  = mix(MINT, A_DARK_GREEN, 0.55)    # output label on the pale face
LIME_DARK  = mix(A_SAGE, A_DARK_GREEN, 0.25)  # accent label on the pale face
CLAY_DARK  = mix(A_DARK_PLUM, A_PALE_PLUM, 0.35)

# --- hardware ----------------------------------------------------------------
# Brass, so the screws and jack collars read as office-furniture fittings; on a
# pale note they are the coin-coloured seals.
BRASS_RIM  = "#6d6440"
BRASS      = "#a29260"
BRASS_MID  = "#8d7f52"
BRASS_DARK = "#3b3520"

# The main jack, and only the main one. A panel with four voice outputs and a
# mix has one cable you reach for first; a stereo pair has two. Struck in gold
# against the brass of everything else, so it is found without reading a word --
# and by colour rather than by a ring, because a ring costs the panel width and
# this must be affordable on every module in the family.
GOLD      = "#d5b459"
GOLD_RIM  = "#5c4a17"

#: Roles a label may be inked in. The generator refuses any other name, which is
#: what stops a one-off hex creeping into a single panel. The role is what the
#: spec says; the hex it lands as depends on whether the label sits on the pale
#: face or on a dark band -- see ink().
INKS = {
    "PAPER": PAPER,
    "SAGE": SAGE,
    "LIME": LIME,
    "MINT": MINT,
    "CLAY": CLAY,
}

#: role -> (C++ constant, hex) on the pale face ("light") and on a dark band or
#: display ("dark"). PAPER means "primary", SAGE "secondary", regardless of hue.
_INK_BY_GROUND = {
    "dark": {
        "PAPER": ("PAPER", PAPER),
        "SAGE": ("SAGE", SAGE),
        "LIME": ("LIME", LIME),
        "MINT": ("MINT", MINT),
        "CLAY": ("CLAY", CLAY),
    },
    "light": {
        "PAPER": ("INK", INK),
        "SAGE": ("SAGE_DARK", SAGE_DARK),
        "LIME": ("LIME_DARK", LIME_DARK),
        "MINT": ("MINT_DARK", MINT_DARK),
        "CLAY": ("CLAY_DARK", CLAY_DARK),
    },
}


def ink(role, ground="light"):
    """(C++ constant name, hex) for a label role on a given ground."""
    if role not in INKS:
        raise ValueError("unknown ink role %r; use one of %s" % (role, sorted(INKS)))
    return _INK_BY_GROUND[ground][role]


#: Emitted into the generated C++ header, in this order.
CPP = [
    ("INK", INK, "engraving ink: the dark bands, and primary text on the face"),
    ("PAPER", PAPER, "the face, and primary text on dark ground"),
    ("FELT", FELT, "section blocks"),
    ("BAND", BAND, "masthead and footer bands"),
    ("GLASS", GLASS, "displays and widget wells"),
    ("RULE", RULE, "the guilloche: rules, frames, traces"),
    ("LIME", LIME, "accent on dark ground; LED emitters"),
    ("MINT", MINT, "affirmatives: outputs"),
    ("CLAY", CLAY, "action required"),
    ("SAGE", SAGE, "secondary text on dark ground"),
    ("SAGE_DARK", SAGE_DARK, "secondary text on the face"),
    ("LIME_DARK", LIME_DARK, "accent text on the face"),
    ("MINT_DARK", MINT_DARK, "output text on the face"),
    ("CLAY_DARK", CLAY_DARK, "warning text on the face"),
]
