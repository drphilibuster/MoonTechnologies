#!/usr/bin/env python3
"""The Kickback panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Kickback is the Modular in a Week drum bank, rebuilt as a drum machine: six
percussion voices from MiaW's Day 9 folder, its own clock, a Euclidean pattern
engine that plays any voice whose TRIG jack is empty, and a grid mode where each
voice runs at its own multiple or division of the clock instead.

The face is nine columns. The two at the left are the payroll -- the clock that
pays and the pattern it pays on. The six in the middle are the contractors, one
column each: KICK, SNARE, HAT and three TOMs. The one at the right is what the
rest of the rack gets, so the pattern engine can play things that are not in
this module.

    strike   TRIG in, lit when it fires    RUN      LEVEL     GATES out (poly)
    tune     TUNE / TONE                   RATE     ACCENT    VEL out
    colour   character, or model selector  CLK in   RST in    ACC in
    decay    DECAY                         DIV      FILL      -
    bend     BEND                          SWING    SEED      -
    ratio    RATIO, /256 .. x256           HUMAN    GATE len  -
    footer   OUT                           CLK out  MIX out   KICK gate

Two things about that order are load-bearing rather than taste. RATIO sits at
the bottom because it is a rhythm control rather than a sound one, and putting
it there leaves the row carrying the CLK and RST jacks as the one already made
tall by a three-position toggle.

And the gate column's labels stand *beside* its jacks rather than over them.
Three of those six sit in rows whose height is set by a trimpot, where a label
above or below is a whole extra line of text on the row -- about four
millimetres across the three of them, which is the difference between six
individual gate outputs and one polyphonic bus. A label alongside costs the row
nothing at all; it is paid for once, in width, which is the cheaper currency
here. That is the same trade VCV make when they name a row instead of every
knob in it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Kickback",
    title="KICKBACK",
    form="FORM 1099-NEC",
    density="compact",
    # The OUT jacks carry the section's own column indices, so they should sit
    # under the voices they belong to rather than on a grid of their own.
    footer_grid=True,
)

# The nine columns. CLK and PAT are the payroll strip, the six after them are
# one voice each in the order the C++ enum has them, and GATE is what the rest
# of the rack sees. The columns are solved, not typed: every row declares which
# of the nine it stands in, and the solver makes each as wide as the widest
# thing any row puts there.
CLK, PAT, KICK, SNARE, HAT, TOM1, TOM2, TOM3, GATE = range(9)

#: Three runs, so the solver puts a gutter on each side of the voices rather
#: than spacing all nine evenly. Without them the payroll reads as two more
#: drums and the gate column as a seventh.
GROUPS = (2, 6, 1)

P.sections = [
    # Box 1 of the 1099-NEC, and the only felt block on the face: six rows deep
    # is as much as 128.5 mm holds, and a second caption plus the gap between
    # two blocks is a row's worth of millimetres the panel does not have.
    Section("NONEMPLOYEE COMPENSATION", groups=GROUPS, rows=[
        # One TRIG per voice, named for the voice and lit when it fires. RUN
        # starts the internal clock; LEVEL is how much of everything reaches
        # the MIX jack.
        Row([Bezel("run", "RUN", col=CLK),
             Trim("level", "LEVEL", col=PAT),
             Jack("kick_trig", "KICK", ink="PAPER", light="kick_led", col=KICK),
             Jack("snare_trig", "SNARE", ink="PAPER", light="snare_led", col=SNARE),
             Jack("hat_trig", "HAT", ink="PAPER", light="hat_led", col=HAT),
             Jack("tom1_trig", "TOM I", ink="PAPER", light="tom1_led", col=TOM1),
             Jack("tom2_trig", "TOM II", ink="PAPER", light="tom2_led", col=TOM2),
             Jack("tom3_trig", "TOM III", ink="PAPER", light="tom3_led", col=TOM3),
             Jack("kick_gate", "KICK", ink="MINT", side="left", col=GATE)]),

        # Each voice's fundamental, and the clock's own rate.
        #
        # These are plain knobs rather than the oversized ones they used to be,
        # and that is what pays for the gate column. When the V/OCT jacks left
        # this panel they took a *paired* row with them -- one label line
        # serving two rows -- and the RATIO row that replaced them needs its
        # own. Shrinking this row's seats is the only place left to find those
        # millimetres that does not cost a control. RATE keeps the lime primary
        # ring, so the panel still says which knob is the one to reach for.
        Row([Knob("rate", "RATE", col=CLK, primary=True),
             Trim("accent", "ACCENT", col=PAT),
             Knob("kick_tune", "TUNE", col=KICK),
             Knob("snare_tune", "TUNE", col=SNARE),
             Knob("hat_tune", "TONE", col=HAT),
             Knob("tom1_tune", "TUNE", col=TOM1),
             Knob("tom2_tune", "TUNE", col=TOM2),
             Knob("tom3_tune", "TUNE", col=TOM3),
             Jack("snare_gate", "SNARE", ink="MINT", side="left", col=GATE)]),

        # The one thing each circuit is for. Two of them are model selectors --
        # KICK picks its oscillator, SNARE picks which of three circuits is
        # running -- because that is the control those two circuits have, and
        # they are real toggles because you should be able to see where a
        # selector is standing. A column with a toggle here is a column whose
        # BEND does double duty; see docs/Kickback.md.
        Row([Jack("clk_in", "CLK", col=CLK),
             Jack("rst_in", "RST", col=PAT),
             Switch("kick_model", "MODEL", col=KICK),
             Switch3("snare_mode", "MODE", col=SNARE),
             Trim("hat_rattle", "RATTLE", col=HAT),
             Trim("tom1_strike", "STRIKE", col=TOM1),
             Trim("tom2_strike", "STRIKE", col=TOM2),
             Trim("tom3_strike", "STRIKE", col=TOM3),
             Jack("hat_gate", "HAT", ink="MINT", side="left", col=GATE)]),
        # Every decay on one line. The three rows below the knobs are trimpots:
        # nine columns of five controls do not fit on a 3U face at knob pitch,
        # and the honest place to spend the millimetres is the one control per
        # column you reach for while playing. TUNE is that control.
        Row(span=[(KICK, TOM3, "DECAY")], items=[Trim("div", "DIV", col=CLK, steps=6),
             Trim("fill", "FILL", col=PAT),
             Trim("kick_decay", "", col=KICK),
             Trim("snare_decay", "", col=SNARE),
             Trim("hat_decay", "", col=HAT),
             Trim("tom1_decay", "", col=TOM1),
             Trim("tom2_decay", "", col=TOM2),
             Trim("tom3_decay", "", col=TOM3),
             Jack("tom1_gate", "TOM I", ink="MINT", side="left", col=GATE)]),

        # And every bend: how far the pitch falls as the strike's energy leaves
        # the head.
        Row(span=[(KICK, TOM3, "BEND")], items=[Trim("swing", "SWING", col=CLK),
             Trim("seed", "SEED", col=PAT, steps=16),
             Trim("kick_bend", "", col=KICK),
             Trim("snare_bend", "", col=SNARE),
             Trim("hat_bend", "", col=HAT),
             Trim("tom1_bend", "", col=TOM1),
             Trim("tom2_bend", "", col=TOM2),
             Trim("tom3_bend", "", col=TOM3),
             Jack("tom2_gate", "TOM II", ink="MINT", side="left", col=GATE)]),

        # RATIO: what this voice runs at against the clock in grid mode, from
        # /256 to x256 in thirty-nine detented steps of 2, 3, 5 and 7. Grid mode
        # is FILL at its bottom stop -- see docs/Kickback.md.
        Row(span=[(KICK, TOM3, "RATIO")], items=[Trim("human", "HUMAN", col=CLK),
             Trim("gatelen", "GATE", col=PAT),
             Trim("kick_ratio", "", col=KICK, steps=39),
             Trim("snare_ratio", "", col=SNARE, steps=39),
             Trim("hat_ratio", "", col=HAT, steps=39),
             Trim("tom1_ratio", "", col=TOM1, steps=39),
             Trim("tom2_ratio", "", col=TOM2, steps=39),
             Trim("tom3_ratio", "", col=TOM3, steps=39),
             # BURST belongs beside the knobs it arms, and there was a column's
             # worth of empty face between the ratio row and the gate outs to
             # put it in. `between` hangs it in that gap rather than opening a
             # column for it, so the switch costs the panel no width at all.
             Switch("burst", "BURST", between=(TOM3, GATE)),
             Jack("tom3_gate", "TOM III", ink="MINT", side="left", col=GATE)]),

    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 runs under one -- 118.9 already clips the corners.
def grid_trace(pos, m):
    """FILL at zero is where the pattern engine switches off and the RATIO knobs
    take over, and nothing on the face said so -- a mode you can only find by
    turning a knob to its stop and noticing the module behaves differently is a
    mode nobody finds. The wire runs from FILL's own zero mark to the box round
    the ratio knobs, which is what that setting hands the module to.

    It leaves FILL on the side the pointer faces at minimum (Rack turns a knob
    from -0.83*pi), crosses into the channel between the two left-hand columns
    -- the one lane down this side with no ink and no labels in it -- and comes
    back in above the gate trim to meet the box's top corner. Routing it under
    FILL instead is the obvious thing and does not work: FILL's own label is
    directly there, and the renderer breaks a wire around any label it crosses,
    so the end that mattered was the end that vanished.
    """
    fx, fy = pos["fill"]
    dx, _ = pos["div"]
    gx, gy = pos["gatelen"]
    rx, ry = pos["kick_ratio"]
    r = RADIUS["trim"]

    # It leaves on the low side of the knob's travel but *level* with it, not
    # under it: a label's box is padded by TRACE_CLEAR and FILL's begins a
    # fifth of a millimetre below the well, so a wire dropping straight out of
    # the bottom starts inside that padding and the renderer eats the very end
    # that had to be visible.
    start = (fx - (r + 1.2), fy + 1.6)
    lane = (dx + fx) / 2                     # between the two columns of trims
    bus = gy - r - 1.5                       # clear above the gate trim's well
    edge = rx - r - 0.9 - GROUP_PAD          # the group box's left side

    return [
        Trace([start, (lane, start[1])]),
        Trace([(lane, start[1]), (lane, bus)]),
        Trace([(lane, bus), (edge, bus)], dots=[(lane, bus), (edge, bus)]),
    ]


P.traces = [grid_trace]

P.footer = [
    Row([Jack("clk_out", "CLK", ink="MINT", col=CLK),
         Jack("mix_out", "MIX", ink="MINT", col=PAT),
         Jack("kick_out", "OUT", ink="MINT", col=KICK),
         Jack("snare_out", "OUT", ink="MINT", col=SNARE),
         Jack("hat_out", "OUT", ink="MINT", col=HAT),
         Jack("tom1_out", "OUT", ink="MINT", col=TOM1),
         Jack("tom2_out", "OUT", ink="MINT", col=TOM2),
         Jack("tom3_out", "OUT", ink="MINT", col=TOM3),
         Jack("acc_in", "ACC", col=GATE)], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))
