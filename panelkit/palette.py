"""The one palette. Every panel, every generator, every runtime label reads it
from here, so nothing can drift.

Sampled originally from hrblock.com, which is the joke: these are tax-prep
greens. The names are a stationery metaphor -- ink, felt, band, glass, rule,
paper -- and they describe the *role* a colour plays, not its hue, so a future
re-skin only has to change the hex.
"""

# --- board ------------------------------------------------------------------
INK   = "#003512"   # panel base, the board itself
FELT  = "#005d1f"   # section blocks: the raised felt a form is filled out on
BAND  = "#00230b"   # masthead / footer bands, and the well behind every widget
GLASS = "#001505"   # displays and read-outs
RULE  = "#2b7a45"   # muted divider rules and well borders

# --- accents ----------------------------------------------------------------
LIME  = "#d2fa52"   # "look here": index tabs, the primary action, traces
MINT  = "#00e95c"   # affirmatives: outputs, locks, accepted filings
CLAY  = "#e09a3c"   # "action required": refusals, warnings. Warm, never red.

# --- silkscreen ink ---------------------------------------------------------
PAPER = "#f6f4e9"   # primary labels
SAGE  = "#8fb99c"   # secondary labels and section captions

# --- hardware ---------------------------------------------------------------
# Brass, so the screws and jack collars read as office-furniture fittings rather
# than the stock chrome, which fights the green.
BRASS_RIM  = "#6d6440"
BRASS      = "#a29260"
BRASS_MID  = "#8d7f52"
BRASS_DARK = "#3b3520"

#: Roles a label may be inked in. The generator refuses any other name, which is
#: what stops a one-off hex creeping into a single panel.
INKS = {
    "PAPER": PAPER,
    "SAGE": SAGE,
    "LIME": LIME,
    "MINT": MINT,
    "CLAY": CLAY,
}

#: Emitted into the generated C++ header, in this order.
CPP = [
    ("INK", INK, "panel base"),
    ("FELT", FELT, "section blocks"),
    ("BAND", BAND, "bands and widget wells"),
    ("GLASS", GLASS, "displays"),
    ("RULE", RULE, "dividers and well borders"),
    ("LIME", LIME, "accents, index tabs, the primary action"),
    ("MINT", MINT, "affirmatives"),
    ("CLAY", CLAY, "action required"),
    ("PAPER", PAPER, "primary label ink"),
    ("SAGE", SAGE, "secondary label ink"),
]


def rgb(hexstr):
    """'#00e95c' -> (0x00, 0xe9, 0x5c)"""
    h = hexstr.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
