"""Emits src/PanelTheme.hpp.

This is the load-bearing half of the consolidation. The palette, the geometry,
the shared hardware and the entire silkscreen table are generated from the same
spec that draws the SVG, so the panel art and the runtime text physically cannot
drift apart. Previously each plugin kept its own copy of all four and they had
already drifted.
"""

from . import palette as P
from . import spec as S
from .layout import cap_h, desc_h, HEADER_H

TITLE_Y = 5.5           # masthead baseline
STUB_Y = 8.0            # the form-number stub, under the title on the right
TITLE_MAX = 10.5
TITLE_MIN = 5.4
TITLE_TRACK = 1.5
STUB_SIZE = 4.6
WHAT_SIZE = 5.4         # the descriptor under the title: read while patching
SCREW_CLEAR = 11.16     # x at which the corner screws stop, plus a millimetre

# The maker's mark: a dollar sign, backwards. It is the real ASCII "$" set in the
# panel's own face and drawn through a scale(-1, 1), rather than a curve drawn to
# look like one -- so it is the genuine glyph, it matches the wordmark beside it,
# and it stays sharp at any zoom. Panels draw all their text at runtime anyway
# (nanosvg discards <text>), so the flip costs one nvgSave/nvgRestore.
LOGO_SIZE = 7.4
LOGO_Y = 8.25
LOGO_W = 0.60 * LOGO_SIZE * S.MM_PER_PX
LOGO_GAP = 0.70


def margin_y(panel, sol, word, side):
    """Where turned text in a margin centres: the middle of the margin, unless
    something the solver placed reaches into the margin there -- then the middle of
    the free run nearest to it that is long enough."""
    from .layout import label_box, text_w
    lo, hi = margin_span(panel)
    need = text_w(word, STUB_SIZE, 0.4) + 2 * 1.6
    mid = (lo + hi) / 2
    if sol is None:
        return mid
    reach = 3.9                         # margin, plus the clearance two runs want
    hits = []
    for l in sol.labels:
        if not l["text"]:
            continue
        x0, y0, x1, y1 = label_box(l)
        if (side == "left" and x0 < reach) or (side == "right" and x1 > panel.w - reach):
            hits.append((y0, y1))
    for (x, y, hw, hh, _n) in sol.wells:
        if (side == "left" and x - hw < reach) or (side == "right" and x + hw > panel.w - reach):
            hits.append((y - hh, y + hh))
    hits.sort()
    free, cur = [], lo
    for y0, y1 in hits:
        if y0 > cur:
            free.append((cur, min(y0, hi)))
        cur = max(cur, y1)
    if cur < hi:
        free.append((cur, hi))
    runs = [(a, b) for a, b in free if b - a >= need]
    if not runs:
        return mid
    a, b = min(runs, key=lambda r: abs(min(max(mid, r[0] + need / 2), r[1] - need / 2) - mid))
    return min(max(mid, a + need / 2), b - need / 2)


def margin_span(panel):
    """The left margin's run between the masthead and the footer band, in mm."""
    from .layout import HEADER_H
    foot = getattr(panel, "band_top", None)
    return HEADER_H + 1.4, (foot if foot is not None else 123.0) - 1.4


def title_size(panel):
    """Largest size at which the masthead still clears both top screws."""
    avail = panel.w - 2 * SCREW_CLEAR
    n = max(len(panel.title), 1)
    size = (avail / (n * S.MM_PER_PX) - TITLE_TRACK) / 0.60
    return max(TITLE_MIN, min(TITLE_MAX, size))


def masthead(panel, sol=None):
    """The header labels, in the order they are drawn."""
    out = []
    if panel.subtitle:
        out.append(dict(x=SCREW_CLEAR + 0.4, y=TITLE_Y, text=panel.title,
                        size=TITLE_MAX, ink="PAPER", align="left",
                        tracking=TITLE_TRACK))
        out.append(dict(x=panel.w - SCREW_CLEAR - 0.4, y=TITLE_Y,
                        text=panel.subtitle, size=6.0, ink="SAGE",
                        align="right", tracking=0.0))
    else:
        out.append(dict(x=panel.w / 2, y=TITLE_Y, text=panel.title,
                        size=title_size(panel), ink="PAPER", align="center",
                        tracking=TITLE_TRACK))
    from .layout import cap_h
    # The margins, either side of the face, between masthead and footer band: a
    # strip every panel has and nothing else uses. Text turned on its side there
    # costs the panel nothing in either axis -- the margin is 2.4 mm, a stub's cap
    # height a little over one. render.py breaks the braid round it.
    lo, hi = margin_span(panel)
    in_margin = 1.05 + cap_h(STUB_SIZE)       # caps toward the edge, 1 mm clear of it
    if panel.what:
        # The line under the title says what the module is, across the face.
        if panel.brand:
            out.append(dict(x=1.4, y=LOGO_Y, text="$", size=LOGO_SIZE,
                            ink="LIME", align="left", tracking=0.0, mirror=True))
        out.append(dict(x=panel.w / 2, y=STUB_Y, text=panel.what, size=WHAT_SIZE,
                        ink="LIME", align="center", tracking=0.4))
        for l in out:
            l["ground"] = "dark"
        if panel.brand:
            out.append(dict(x=in_margin, y=margin_y(panel, sol, panel.brand, "left"),
                            text=panel.brand,
                            size=STUB_SIZE, ink="SAGE", align="center", tracking=0.4,
                            turn="up", ground="light"))
        if panel.form:
            out.append(dict(x=panel.w - in_margin, y=margin_y(panel, sol, panel.form, "right"),
                            text=panel.form,
                            size=STUB_SIZE, ink="SAGE", align="center", tracking=0.4,
                            turn="down", ground="light"))
        return out
    # Without a descriptor the masthead reads like a note's header: the issuing
    # office bottom-left, the series number bottom-right, the denomination across
    # the top. On a panel too narrow for both, the wordmark runs up the left
    # margin and the logo stays up here on its own -- see layout.masthead_mode.
    narrow = getattr(panel, "masthead_mode", "wide") == "narrow"
    if panel.brand:
        out.append(dict(x=SCREW_CLEAR + 0.4, y=LOGO_Y, text="$", size=LOGO_SIZE,
                        ink="LIME", align="left", tracking=0.0, mirror=True))
        if not narrow:
            out.append(dict(x=SCREW_CLEAR + 0.4 + LOGO_W + LOGO_GAP, y=STUB_Y,
                            text=panel.brand,
                            size=STUB_SIZE, ink="SAGE", align="left", tracking=0.4))
    if panel.form:
        out.append(dict(x=panel.w - 4.2, y=STUB_Y, text=panel.form,
                        size=STUB_SIZE, ink="SAGE", align="right", tracking=0.4))
    for l in out:
        l["ground"] = "dark"
    if panel.brand and narrow:
        out.append(dict(x=in_margin, y=margin_y(panel, sol, panel.brand, "left"),
                        text=panel.brand,
                        size=STUB_SIZE, ink="SAGE", align="center", tracking=0.4,
                        turn="up", ground="light"))
    return out



# The shared text vocabulary. Emitted verbatim into every plugin's PanelTheme.hpp.
#
# Kept as one block rather than a() per line because it is C++ that never varies
# with the spec -- and because it exists precisely so that three plugins stop
# each keeping their own copy of it. Before this, every read-out in the family
# hand-rolled its own loadFont / nvgFontFaceId / nvgFontSize / nvgTextAlign /
# nvgTextLetterSpacing sequence, and they had already drifted: Retroactive
# carried a comment about letter spacing leaking into the segment face, which is
# a bug the call site should never have been able to write.
TEXT_KIT = r"""// --- text ------------------------------------------------------------------
// Rack's nanosvg discards <text>, so every word on a family panel is drawn at
// runtime. That makes text drawing the single most repeated thing the plugins
// do, which is why the whole vocabulary for it lives here rather than being
// re-spelled at each call site.

/** The three faces the family uses. */
enum class Face { Ui, Mono, Seg };

inline std::string facePath(Face f) {
	switch (f) {
		case Face::Mono: return monoFont();
		case Face::Seg:  return segFont();
		default:         return uiFont();
	}
}

/** How one run of text is set.
 *
 * Bundled rather than passed as five arguments because two of these are sticky
 * global state on the NVGcontext: a run that sets letter spacing or an
 * alignment and does not put it back silently restyles whatever draws next.
 * Going through TextStyle makes that impossible to forget. */
struct TextStyle {
	Face face = Face::Ui;
	float size = 10.f;
	float tracking = 0.f;
	NVGcolor ink = PAPER;
	int align = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;

	TextStyle() {}
	TextStyle(Face face, float size, NVGcolor ink, int align, float tracking = 0.f)
		: face(face), size(size), tracking(tracking), ink(ink), align(align) {}

	/** A copy in a different ink -- the usual reason a call site wants a variant. */
	TextStyle inked(NVGcolor c) const { TextStyle s = *this; s.ink = c; return s; }
	TextStyle aligned(int a) const { TextStyle s = *this; s.align = a; return s; }
	TextStyle sized(float px) const { TextStyle s = *this; s.size = px; return s; }
};

/** Binds a style to the context. False means the face has not loaded yet and the
    caller must draw nothing: nvgText with no face bound is undefined. */
inline bool bindStyle(NVGcontext* vg, const TextStyle& s) {
	std::shared_ptr<window::Font> font = APP->window->loadFont(facePath(s.face));
	if (!font)
		return false;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, s.size);
	nvgTextAlign(vg, s.align);
	nvgTextLetterSpacing(vg, s.tracking);
	nvgFillColor(vg, s.ink);
	return true;
}

/** Draws one run, then clears the tracking it set. */
inline float text(NVGcontext* vg, const TextStyle& s, float x, float y, const char* str) {
	if (!bindStyle(vg, s))
		return x;
	float end = nvgText(vg, x, y, str, NULL);
	nvgTextLetterSpacing(vg, 0.f);
	return end;
}

inline float text(NVGcontext* vg, const TextStyle& s, float x, float y,
                  const std::string& str) {
	return text(vg, s, x, y, str.c_str());
}

inline float text(NVGcontext* vg, const TextStyle& s, Vec p, const std::string& str) {
	return text(vg, s, p.x, p.y, str.c_str());
}

/** Width of `str` in `s`, in local units. Leaves no tracking behind. */
inline float textWidth(NVGcontext* vg, const TextStyle& s, const std::string& str) {
	if (!bindStyle(vg, s))
		return 0.f;
	float bounds[4];
	nvgTextBounds(vg, 0, 0, str.c_str(), NULL, bounds);
	nvgTextLetterSpacing(vg, 0.f);
	return bounds[2] - bounds[0];
}

/** Draws a run reflected about its anchor -- the maker's mark, and anything else
    that wants a genuine glyph rather than a traced curve. The alignment flips
    with the axis, so the glyph lands on the side of `x` it would have occupied
    unmirrored. */
inline void textMirrored(NVGcontext* vg, const TextStyle& s, float x, float y,
                         const char* str) {
	TextStyle f = s;
	if (f.align & NVG_ALIGN_LEFT)
		f.align = (f.align & ~NVG_ALIGN_LEFT) | NVG_ALIGN_RIGHT;
	else if (f.align & NVG_ALIGN_RIGHT)
		f.align = (f.align & ~NVG_ALIGN_RIGHT) | NVG_ALIGN_LEFT;
	nvgSave(vg);
	nvgTranslate(vg, x, y);
	nvgScale(vg, -1.f, 1.f);
	text(vg, f, 0.f, 0.f, str);
	nvgRestore(vg);
}

/** The uniform scale of the context's current transform. */
inline float transformScale(NVGcontext* vg) {
	float xf[6];
	nvgCurrentTransform(vg, xf);
	return std::sqrt(std::fabs(xf[0] * xf[3] - xf[1] * xf[2]));
}

/** `str` trimmed to `maxWidth` with a trailing ellipsis, remembered until one of
 * its inputs changes.
 *
 * Both halves matter. The measuring is a binary search over codepoint
 * boundaries, not a character-at-a-time trim: a trim costs one text shaping pass
 * per character dropped, which for a long title is forty-odd passes -- every
 * frame, for every row on screen. The cache then takes the steady-state cost to
 * nothing, since the text of a panel changes far more rarely than it is drawn.
 *
 * The transform scale is part of the key because nvgTextBounds quantizes to the
 * rasterized glyph grid: the same string at the same nominal size measures a
 * shade wider at a different rack zoom, and a stale fit would clip. */
/** All-segments-on ghost behind a numeric read-out: every digit becomes an 8.
    This is what sells a seven-segment display -- without it the unlit segments
    are simply absent and the thing reads as text in an odd face. */
inline std::string segGhost(const std::string& s) {
	std::string g = s;
	for (size_t i = 0; i < g.size(); i++)
		if (g[i] >= '0' && g[i] <= '9')
			g[i] = '8';
	return g;
}

/** Draws a numeral in the segment face over its ghost, then its unit in the text
 * face, and returns the x advance.
 *
 * The family's read-out rule in one function: numerals in DSEG7, words in Share
 * Tech Mono. DSEG7's cmap carries only [0-9 . : -], so a unit MUST leave the
 * segment face or Rack's NotoSansJP fallback renders it as Japanese sans. Note
 * also that DSEG7's '.' has zero advance -- it overprints the previous glyph --
 * so "2.997" occupies four cells, not five, and anything measuring these strings
 * has to measure rather than count. */
inline float segValue(NVGcontext* vg, float x, float y, float size,
                      const std::string& num, const std::string& unit, NVGcolor ink) {
	TextStyle seg(Face::Seg, size, alpha(ink, 0.13f),
	              NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	text(vg, seg, x, y, segGhost(num));
	float end = text(vg, seg.inked(ink), x, y, num);
	if (!unit.empty()) {
		TextStyle u(Face::Mono, size * 0.72f, alpha(ink, 0.7f),
		            NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		end = text(vg, u, end + 1.5f, y, unit);
	}
	return end;
}

struct FittedText {
	const std::string& get(NVGcontext* vg, const TextStyle& style,
	                       const std::string& str, float maxWidth) {
		float sc = transformScale(vg);
		if (maxWidth != cachedWidth || style.size != cachedSize
		    || style.face != cachedFace || sc != cachedScale || str != source) {
			source = str;
			cachedWidth = maxWidth;
			cachedSize = style.size;
			cachedFace = style.face;
			cachedScale = sc;
			result = fit(vg, style, str, maxWidth);
		}
		return result;
	}

	/** The uncached form. Prefer get() anywhere this runs per frame. */
	static std::string fit(NVGcontext* vg, const TextStyle& style,
	                       const std::string& str, float maxWidth) {
		if (textWidth(vg, style, str) <= maxWidth)
			return str;

		// Every index at which a UTF-8 codepoint starts, plus the end. Trimming
		// bytes instead would split multi-byte glyphs.
		std::vector<size_t> stops;
		stops.push_back(0);
		for (size_t i = 1; i <= str.size(); i++)
			if (i == str.size() || ((unsigned char) str[i] & 0xC0) != 0x80)
				stops.push_back(i);

		static const char* const ELLIPSIS = "\xE2\x80\xA6";   // U+2026

		// Largest prefix that still fits once the ellipsis is added. lo is the
		// empty prefix, which is just the ellipsis: if even that overflows we
		// return it rather than an empty line, because a blank row reads as a
		// bug and a lone ellipsis reads as a truncation.
		size_t lo = 0, hi = stops.size() - 1;
		while (lo < hi) {
			size_t mid = lo + (hi - lo + 1) / 2;
			if (textWidth(vg, style, str.substr(0, stops[mid]) + ELLIPSIS) <= maxWidth)
				lo = mid;
			else
				hi = mid - 1;
		}
		return str.substr(0, stops[lo]) + ELLIPSIS;
	}

private:
	std::string source;
	std::string result;
	float cachedWidth = -1.f;
	float cachedSize = -1.f;
	float cachedScale = -1.f;
	Face cachedFace = Face::Ui;
};
"""

ALIGN = {"center": "NVG_ALIGN_CENTER", "left": "NVG_ALIGN_LEFT",
         "right": "NVG_ALIGN_RIGHT"}


# --- the two generated headers ----------------------------------------------
# The family ships as one plugin, so all three panels' C++ is linked into one
# binary. That splits this file's output in two, along the only line that
# matters -- linkage:
#
#   src/PanelTheme.hpp      identical in every translation unit: the palette,
#                           the text vocabulary, the shared hardware. Everything
#                           here is inline or a template, so one definition is
#                           shared across the whole plugin and the One Definition
#                           Rule is satisfied by the definitions being the same.
#
#   src/<Module>/Panel.hpp  this panel's numbers: HP, the widget positions, the
#                           silkscreen table. All `static`, i.e. internal
#                           linkage, so each module gets its own copy and three
#                           different LABELS tables cannot collide.
#
# Both live in `namespace panel`, so a module's C++ says panel::LIME and
# panel::TIME_POS exactly as it did when each module was its own plugin. The one
# rule this imposes: a translation unit may include one Panel.hpp, never two.
# Including two is a duplicate-definition error at compile time, which is the
# failure mode you want -- loud, and impossible to ship.


#: A pair of buttons, one over the other, that step a selector by one. Emitted as C++ into
#: PanelTheme.hpp (one definition for the whole plugin) so every preset selector is the same
#: object, with the same repeat rate and the same tooltip, and none of them is open-coded.
STEP_PAIR = r"""/** UP over DOWN, a hair apart: the control for a selector that is a count and not an angle.
 *
 *  A preset knob is the wrong shape for what it does. Nobody turns to the three hundred and
 *  seventeenth sound; they press once for the next one, and a knob makes that a drag you have
 *  to land. This is bound to the same param the knob was, so a patch, a CV map and the param's
 *  own tooltip are all unchanged -- it only moves the value by one, the top button up and the
 *  bottom down, clamped to the param's own range (or wrapped, for a ring of sounds that has no
 *  end). Hold either to repeat, after a beat.
 *
 *  The pair is a knob's height (9.6 mm) and the spec reserves exactly that, so it stands
 *  where the knob stood. Colour comes from the palette and nothing else: the buttons are
 *  SAGE on the dark well, and light to LIME while pressed. */
struct StepPair : app::ParamWidget {
	//: Wrap from the last value to the first (and back) instead of stopping. Ignored
	//: when the param's range is unbounded, which is how an endless encoder is configured.
	bool wrap = false;
	float stepSize = 1.f;

	int held = 0;               // +1 the top button is down, -1 the bottom one, 0 neither
	int hover = 0;              // which half the pointer is over
	double nextAt = 0.0;
	float oldValue = NAN;       // the value when the press began: one undo step per hold

	static constexpr float BTN = 4.5f / 9.6f;       // each button's share of the pair's height

	/** No SVG gives this a size, so it takes the spec's: EXTENT["stepper"] in panelkit/spec.py. */
	StepPair() { box.size = mm(@W@f, @H@f); }

	/** The half (+1 top, -1 bottom) a point inside the widget is over. */
	int halfAt(float y) const { return y < box.size.y / 2.f ? +1 : -1; }

	void nudge(int dir) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq) return;
		const float lo = pq->getMinValue(), hi = pq->getMaxValue(), v = pq->getValue();
		float n = v + dir * stepSize;
		if (wrap && std::isfinite(lo) && std::isfinite(hi))
			n = n > hi ? lo : n < lo ? hi : n;
		pq->setValue(n);        // clamps, and snaps a snapping param
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			if (engine::ParamQuantity* pq = getParamQuantity()) {
				e.consume(this);
				oldValue = pq->getValue();
				held = halfAt(e.pos.y);
				nudge(held);
				nextAt = system::getTime() + 0.45;
			}
			return;
		}
		ParamWidget::onButton(e);   // the right-click menu
	}

	void onDragEnd(const DragEndEvent& e) override {
		held = 0;
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && module && !std::isnan(oldValue) && pq->getValue() != oldValue) {
			history::ParamChange* h = new history::ParamChange;
			h->name = "change preset";
			h->moduleId = module->id;
			h->paramId = paramId;
			h->oldValue = oldValue;
			h->newValue = pq->getValue();
			APP->history->push(h);
		}
		oldValue = NAN;
		ParamWidget::onDragEnd(e);
	}

	void onHover(const HoverEvent& e) override {
		hover = halfAt(e.pos.y);
		ParamWidget::onHover(e);
	}
	void onLeave(const LeaveEvent& e) override { hover = 0; ParamWidget::onLeave(e); }

	void step() override {
		ParamWidget::step();
		if (held && system::getTime() >= nextAt) {
			nudge(held);
			nextAt += 0.07;
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const float w = box.size.x, bh = box.size.y * BTN;
		for (int dir = +1; dir >= -1; dir -= 2) {
			const float y = dir > 0 ? 0.f : box.size.y - bh;
			const bool down = held == dir;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 0.5f, y + 0.5f, w - 1.f, bh - 1.f, 2.2f);
			nvgFillColor(vg, down ? LIME : alpha(SAGE, hover == dir ? 0.46f : 0.30f));
			nvgFill(vg);
			nvgStrokeColor(vg, alpha(RULE, 0.9f));
			nvgStrokeWidth(vg, 0.6f);
			nvgStroke(vg);
			// the arrow: a triangle pointing the way the button steps
			const float cx = w / 2.f, cy = y + bh / 2.f, a = bh * 0.22f, b = bh * 0.17f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy - dir * a);
			nvgLineTo(vg, cx + a * 1.35f, cy + dir * b);
			nvgLineTo(vg, cx - a * 1.35f, cy + dir * b);
			nvgClosePath(vg);
			nvgFillColor(vg, down ? INK : PAPER);
			nvgFill(vg);
		}
		ParamWidget::draw(args);
	}
};"""

#: The controls that live on a screen. Emitted into PanelTheme.hpp for the same reason StepPair
#: is: every display in the family edits its values the same way, so none of them is open-coded.
SCREEN_FIELDS = r"""/** The pointer a screen field asks for while it is under the mouse: up/down arrows over a
 *  value you hold and drag, a hand over a thing you click. Rack never sets a cursor shape of
 *  its own, so the one set here stays until a field puts the default back. */
struct ScreenCursor {
	static GLFWcursor* shape(int which) {
		static GLFWcursor* arrows = NULL;
		static GLFWcursor* hand = NULL;
		GLFWcursor*& c = which == GLFW_HAND_CURSOR ? hand : arrows;
		if (!c) c = glfwCreateStandardCursor(which);
		return c;
	}
	static void set(int which) {
		if (APP && APP->window && APP->window->win)
			glfwSetCursor(APP->window->win, which ? shape(which) : NULL);
	}
};

/** Something on a screen you can take hold of.
 *
 *  A display that prints a value is the obvious place to change it, so a value printed on a
 *  screen is a control: the field is a ParamWidget whose box is a cell of the glass (FIELD_<NAME>
 *  in Panel.hpp), bound to the same param a knob would be. That keeps everything a param is
 *  owed for free -- the tooltip, the right-click menu with typed entry, MIDI-Map learn, undo,
 *  double-click to reset. The field draws no value of its own; the module's display draws it in
 *  the same rectangle, and this only says, under the pointer, that it can be taken hold of. */
struct ScreenField : app::ParamWidget {
	bool hovered = false;
	bool dragging = false;

	virtual int cursorShape() { return GLFW_VRESIZE_CURSOR; }

	void onEnter(const EnterEvent& e) override {
		hovered = true;
		ScreenCursor::set(cursorShape());
		ParamWidget::onEnter(e);
	}
	void onLeave(const LeaveEvent& e) override {
		hovered = false;
		if (!dragging) ScreenCursor::set(0);
		ParamWidget::onLeave(e);
	}
	void endDrag() {
		dragging = false;
		if (!hovered) ScreenCursor::set(0);
	}
	~ScreenField() {
		if (hovered || dragging) ScreenCursor::set(0);
	}

	/** Drawn on the light layer so it shows at any room brightness, like the display it sits on. */
	void drawLayer(const DrawArgs& args, int layer) override {
		ParamWidget::drawLayer(args, layer);
		if (layer != 1 || !(hovered || dragging)) return;
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.6f);
		nvgFillColor(vg, alpha(LIME, dragging ? 0.10f : 0.06f));
		nvgFill(vg);
		nvgStrokeColor(vg, alpha(LIME, dragging ? 0.75f : 0.45f));
		nvgStrokeWidth(vg, 0.7f);
		nvgStroke(vg);
	}
};

/** A value: hold it and drag up or down. The pointer is up/down arrows the whole time, so the
 *  hand reads as moving the number rather than turning something. Ctrl drags fine, Shift
 *  coarse, the same modifiers as a knob; the wheel nudges when Rack's own knob-scroll setting is
 *  on (it is off by default because it would steal the scroll that moves the rack). */
struct ScreenKnob : ScreenField {
	//: Multiplies the drag rate. 1 crosses the whole range in about as far as a knob does.
	float speed = 1.f;
	float oldValue = NAN;
	float snapDelta = 0.f;
	float dragged = 0.f;

	static float modSpeed() {
		int mods = APP->window->getMods() & RACK_MOD_MASK;
		if (mods == RACK_MOD_CTRL) return 1 / 10.f;
		if (mods == GLFW_MOD_SHIFT) return 4.f;
		if (mods == (RACK_MOD_CTRL | GLFW_MOD_SHIFT)) return 1 / 100.f;
		return 1.f;
	}

	/** How far one pixel of travel moves the value. Matched to Rack's linear knob mode
	 *  (sensitivity over a 300-degree sweep), so a field and a knob feel like one hand. */
	virtual float perPixel(engine::ParamQuantity* pq) {
		const float range = pq->isBounded() ? pq->getRange() : 1.f;
		return settings::knobLinearSensitivity * speed * range / (300.f / 360.f);
	}

	/** A click that did not become a drag. A value has nothing to do; a choice opens. */
	virtual void click() {}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		if (engine::ParamQuantity* pq = getParamQuantity()) oldValue = pq->getValue();
		snapDelta = 0.f;
		dragged = 0.f;
		dragging = true;
		ParamWidget::onDragStart(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq) return;
		dragged += std::fabs(e.mouseDelta.y) + std::fabs(e.mouseDelta.x);
		float delta = -e.mouseDelta.y * perPixel(pq) * modSpeed();
		if (pq->snapEnabled) {
			snapDelta += delta;
			delta = std::trunc(snapDelta);
			snapDelta -= delta;
		}
		pq->setValue(pq->getValue() + delta);
		ParamWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		pushHistory("move");
		endDrag();
		if (dragged < 4.f) click();
		ParamWidget::onDragEnd(e);
	}

	void pushHistory(const char* what) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && module && !std::isnan(oldValue) && pq->getValue() != oldValue) {
			history::ParamChange* h = new history::ParamChange;
			h->name = std::string(what) + " " + pq->getLabel();
			h->moduleId = module->id;
			h->paramId = paramId;
			h->oldValue = oldValue;
			h->newValue = pq->getValue();
			APP->history->push(h);
		}
		oldValue = NAN;
	}

	void onHoverScroll(const HoverScrollEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!settings::knobScroll || !pq) { ParamWidget::onHoverScroll(e); return; }
		float d = e.scrollDelta.y;
		if (d == 0.f) return;
		oldValue = pq->getValue();
		if (pq->snapEnabled)
			pq->setValue(pq->getValue() + (d > 0.f ? 1.f : -1.f));
		else
			pq->setValue(pq->getValue() + d * settings::knobScrollSensitivity * speed * modSpeed()
			             * (pq->isBounded() ? pq->getRange() : 1.f));
		pushHistory("scroll");
		e.consume(this);
	}
};

/** One of a set of named options. Hold and drag scrolls through them, a step every
 *  `pxPerStep` pixels, whatever the range; a click lists them, to pick one directly. The names
 *  come from the param's own SwitchQuantity labels, or from `nameOf` when the module computes
 *  them (a program list, say), or else the numbers. */
struct ScreenSelect : ScreenKnob {
	float pxPerStep = 14.f;
	//: Wrap from the last option to the first while dragging, for a ring with no end.
	bool wrap = false;
	std::function<std::string(int)> nameOf;

	float perPixel(engine::ParamQuantity*) override { return 1.f / pxPerStep; }

	void onDragMove(const DragMoveEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!wrap || !pq || e.button != GLFW_MOUSE_BUTTON_LEFT || !pq->isBounded()) {
			ScreenKnob::onDragMove(e);
			return;
		}
		dragged += std::fabs(e.mouseDelta.y) + std::fabs(e.mouseDelta.x);
		snapDelta += -e.mouseDelta.y * perPixel(pq) * modSpeed();
		float delta = std::trunc(snapDelta);
		snapDelta -= delta;
		if (delta == 0.f) return;
		const float lo = pq->getMinValue(), n = pq->getMaxValue() - lo + 1.f;
		pq->setValue(lo + math::eucMod(std::round(pq->getValue() - lo + delta), n));
	}

	std::string optionName(engine::ParamQuantity* pq, int i) {
		if (nameOf) return nameOf(i);
		if (auto* sq = dynamic_cast<engine::SwitchQuantity*>(pq))
			if (i >= 0 && i < (int) sq->labels.size()) return sq->labels[i];
		return string::f("%d", (int) (pq->getMinValue() + i));
	}

	void click() override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq || !pq->isBounded()) return;
		const float lo = pq->getMinValue();
		const int n = (int) std::round(pq->getMaxValue() - lo) + 1;
		const int cur = (int) std::round(pq->getValue() - lo);
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(pq->getLabel()));
		const int id = paramId;
		const int64_t moduleId = module ? module->id : -1;
		for (int i = 0; i < n; i++) {
			menu->addChild(createCheckMenuItem(optionName(pq, i), "",
				[=]() { return i == cur; },
				[=]() {
					engine::Module* m = APP->engine->getModule(moduleId);
					if (!m) return;
					engine::ParamQuantity* q = m->paramQuantities[id];
					float before = q->getValue();
					q->setValue(lo + i);
					if (q->getValue() == before) return;
					history::ParamChange* h = new history::ParamChange;
					h->name = std::string("select ") + q->getLabel();
					h->moduleId = moduleId;
					h->paramId = id;
					h->oldValue = before;
					h->newValue = q->getValue();
					APP->history->push(h);
				}));
		}
	}
};

/** A switch on the screen: a click steps it to its next position (Ctrl-click the previous
 *  one), wrapping, like Rack's own switch. `momentary` makes it a button that holds its
 *  maximum while the mouse is down. The pointer is a hand -- this is pressed, not dragged. */
struct ScreenSwitch : ScreenField {
	bool momentary = false;

	int cursorShape() override { return GLFW_HAND_CURSOR; }

	void onDoubleClick(const DoubleClickEvent& e) override {
		// A double click is two presses here, not a reset.
		widget::OpaqueWidget::onDoubleClick(e);
	}

	void onDragStart(const DragStartEvent& e) override {
		ParamWidget::onDragStart(e);
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		dragging = true;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq) return;
		if (momentary) {
			pq->setMax();
			return;
		}
		const float before = pq->getValue();
		if ((APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL)
			pq->isMin() ? pq->setMax() : pq->setValue(std::round(before) - 1.f);
		else
			pq->isMax() ? pq->setMin() : pq->setValue(std::round(before) + 1.f);
		if (module && pq->getValue() != before) {
			history::ParamChange* h = new history::ParamChange;
			h->name = std::string("switch ") + pq->getLabel();
			h->moduleId = module->id;
			h->paramId = paramId;
			h->oldValue = before;
			h->newValue = pq->getValue();
			APP->history->push(h);
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		ParamWidget::onDragEnd(e);
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		endDrag();
		if (momentary)
			if (engine::ParamQuantity* pq = getParamQuantity()) pq->setMin();
	}
};

/** A button on the screen: held while the mouse is down, like a VCVButton. */
struct ScreenButton : ScreenSwitch {
	ScreenButton() { momentary = true; }
};

/** A word on the screen that opens a menu: for an action rather than a value --
 *  a preset to load, a file to choose -- which has no param to bind to. The
 *  module fills the menu; the field only gives it the family's pointer and
 *  hover, so it reads as one of the screen's controls. */
struct ScreenMenu : widget::OpaqueWidget {
	std::function<void(ui::Menu*)> fill;
	bool hovered = false;

	void onEnter(const EnterEvent& e) override { hovered = true; ScreenCursor::set(GLFW_HAND_CURSOR); }
	void onLeave(const LeaveEvent& e) override { hovered = false; ScreenCursor::set(0); }
	~ScreenMenu() { if (hovered) ScreenCursor::set(0); }

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
			e.consume(this);
			if (fill) fill(createMenu());
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		OpaqueWidget::drawLayer(args, layer);
		if (layer != 1 || !hovered) return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.6f);
		nvgFillColor(args.vg, alpha(LIME, 0.06f));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, alpha(LIME, 0.45f));
		nvgStrokeWidth(args.vg, 0.7f);
		nvgStroke(args.vg);
	}
};

inline ScreenMenu* createMenuField(const Rect& cell, std::function<void(ui::Menu*)> fill) {
	ScreenMenu* o = new ScreenMenu;
	o->box.pos = mm(cell.pos.x, cell.pos.y);
	o->box.size = mm(cell.size.x, cell.size.y);
	o->fill = fill;
	return o;
}

/** Place a field over its cell of the glass. `cell` is the FIELD_<NAME> rectangle from
 *  Panel.hpp, in panel millimetres. */
template <class TField>
TField* createField(const Rect& cell, engine::Module* module, int paramId) {
	TField* o = createParam<TField>(mm(cell.pos.x, cell.pos.y), module, paramId);
	o->box.size = mm(cell.size.x, cell.size.y);
	return o;
}"""

def common():
    """src/PanelTheme.hpp -- the half that is the same for every panel."""
    o = []
    a = o.append
    a("// Generated by panelkit. Do not edit; edit panelkit/emit.py.")
    a("//")
    a("// The family's shared panel vocabulary: the palette, the millimetre grid, the")
    a("// text kit and the hardware. Every panel in the plugin draws through this and")
    a("// nothing else -- no module may call nvgFontFaceId, nvgFontSize,")
    a("// nvgTextLetterSpacing, nvgTextBounds, nvgText or loadFont itself. `make panel`")
    a("// prints every place that does.")
    a("//")
    a("// This file is included by every src/<Module>/Panel.hpp, which adds that one")
    a("// panel's numbers. Include the Panel.hpp, not this.")
    a("//")
    a("// Everything below has external linkage (inline or template), so it is defined")
    a("// once for the whole plugin. Anything that varies per panel belongs in")
    a("// Panel.hpp instead, as `static`.")
    a("#pragma once")
    a("#include <rack.hpp>")
    a("")
    a("#include <cmath>")
    a("#include <cstddef>")
    a("#include <memory>")
    a("#include <string>")
    a("#include <vector>")
    a("")
    a("// Declared here rather than pulled in from plugin.hpp, so this header can be")
    a("// included from anywhere in the plugin without dragging the module in with it.")
    a("extern rack::Plugin* pluginInstance;")
    a("")
    a("namespace panel {")
    a("")
    a("using namespace rack;")
    a("")
    a("// --- geometry --------------------------------------------------------------")
    a("/** The standard Eurorack 3U face. Panel width is per-module: see Panel.hpp. */")
    a("static const float PANEL_H = %.4ff;  // mm" % S.PANEL_H)
    a("")
    a("// --- palette ---------------------------------------------------------------")
    a("// `static` because NVGcolor is not a literal type under C++11, which is what")
    a("// the Rack SDK compiles plugins as -- so these cannot be inline variables and")
    a("// each translation unit gets its own copy. The values are generated, so the")
    a("// copies cannot disagree.")
    for name, hexstr, note in P.CPP:
        r, g, b = P.rgb(hexstr)
        a("static const NVGcolor %-6s = nvgRGB(0x%02x, 0x%02x, 0x%02x);   // %s"
          % (name, r, g, b, note))
    a("")
    a("inline NVGcolor alpha(NVGcolor c, float a) { c.a = a; return c; }")
    a("")
    a("// --- fonts -----------------------------------------------------------------")
    a("// UI is Nunito Bold (the silkscreen), read-outs are Share Tech Mono, and")
    a("// numerals in a read-out are DSEG7. DSEG7's cmap carries only [0-9 . : -];")
    a("// Rack chains NotoSansJP as a fallback onto every font, so a stray '%' in the")
    a("// segment face renders as proportional Japanese sans rather than a tofu box.")
    a('inline std::string uiFont()   { return asset::system("res/fonts/Nunito-Bold.ttf"); }')
    a('inline std::string monoFont() { return asset::system("res/fonts/ShareTechMono-Regular.ttf"); }')
    a('inline std::string segFont()  { return asset::system("res/fonts/DSEG7ClassicMini-Bold.ttf"); }')
    a("")
    a("inline Vec mm(float x, float y) { return mm2px(Vec(x, y)); }")
    a("inline Rect mmRect(float x, float y, float w, float h) {")
    a("\treturn Rect(mm2px(Vec(x, y)), mm2px(Vec(w, h)));")
    a("}")
    a("")
    a(TEXT_KIT)
    a("// --- shared hardware -------------------------------------------------------")
    a("/** Hex-head brass screw. The SVG is exactly 15 x 14.9989 px, matching stock")
    a("    ScrewSilver, because SvgScrew takes its box size straight from the SVG. */")
    a("struct ScrewHex : app::SvgScrew {")
    a("\tScrewHex() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/ScrewHex.svg")));')
    a("\t}")
    a("};")
    a("")
    a("/** Brass-collared jacks on the stock PJ301M canvas. The stock chrome collar is")
    a("    the loudest off-palette object on a green panel; brass matches the screws.")
    a("    The output variant rings the throat in mint. */")
    a("struct PortIn : app::SvgPort {")
    a("\tPortIn() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/PortIn.svg")));')
    a("\t}")
    a("};")
    a("")
    a("/** The same jacks, with a thin lime line inside the throat: this one")
    a(" *  carries timing -- a clock, a trigger, a gate, a reset -- rather than")
    a(" *  a level. The band still says which way it goes. */")
    a("struct PortTrigIn : app::SvgPort {")
    a("\tPortTrigIn() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/PortTrigIn.svg")));')
    a("\t}")
    a("};")
    a("")
    a("struct PortTrigOut : app::SvgPort {")
    a("\tPortTrigOut() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/PortTrigOut.svg")));')
    a("\t}")
    a("};")
    a("struct PortOut : app::SvgPort {")
    a("\tPortOut() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/PortOut.svg")));')
    a("\t}")
    a("};")
    a("")
    a("/** The main input and the main output: the same jacks with the collar")
    a(" *  struck in gold instead of brass. Use them for the one port a patch")
    a(" *  reaches for first -- a mix output beside four voice outputs, the two")
    a(" *  of a stereo pair -- and nowhere else. A panel where everything is")
    a(" *  gold says the same as a panel where nothing is. */")
    for _n in ("PortInMain", "PortOutMain", "PortTrigInMain", "PortTrigOutMain"):
        a("struct %s : app::SvgPort {" % _n)
        a("\t%s() {" % _n)
        a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/%s.svg")));' % _n)
        a("\t}")
        a("};")
        a("")
    # The readout's size is a family constant, not a per-panel one: a module
    # placing a MiniDisplay needs it, and typing the millimetres at the call
    # site is exactly the drift this kit exists to stop.
    _rw, _rh = S.EXTENT["readout"]
    a("//: The plate a MiniDisplay fills, in mm -- the readout widget's own size.")
    a("static const float READOUT_W = %.4ff;" % (2 * _rw))
    a("static const float READOUT_H = %.4ff;" % (2 * _rh))
    a("")
    a("/** A little lit plate that names the control above it.")
    a(" *")
    a(" *  For a control whose meaning is not fixed: a macro knob that is DECAY")
    a(" *  on one program and SPREAD on the next cannot be engraved, and a panel")
    a(" *  that engraves it anyway is lying two thirds of the time. The plate")
    a(" *  itself is drawn in the panel art as the widget's own well, so this")
    a(" *  only writes the word into it.")
    a(" *")
    a(" *  Point `name` at a `const char*` the module keeps up to date. A null")
    a(" *  pointer, or a null string, draws the family's own way of saying")
    a(" *  nothing: two dashes. That is deliberate -- a knob the running program")
    a(" *  has no use for should say so where the knob is, not go blank and look")
    a(" *  broken. */")
    a("struct MiniDisplay : widget::Widget {")
    a("\t//: Point this at a const char* the module keeps up to date.")
    a("\tconst char* const* name = NULL;")
    a("\tfloat size = 5.2f;")
    a("\tFittedText fitted;")
    a("")
    a("\tvoid drawLayer(const DrawArgs& args, int layer) override {")
    a("\t\tif (layer != 1) return;")
    a("\t\tconst char* t = (name && *name) ? *name : \"--\";")
    a("\t\tTextStyle st(Face::Mono, size, LIME,")
    a("\t\t             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.f);")
    a("\t\t// Ellipsized to the plate: a long name shortens rather than")
    a("\t\t// spilling over the two knobs either side of it. Cached, because")
    a("\t\t// this runs every frame and the name changes only on a program.")
    a("\t\ttext(args.vg, st, box.size.x / 2.f, box.size.y / 2.f,")
    a("\t\t     fitted.get(args.vg, st, t, box.size.x - 2.f));")
    a("\t}")
    a("};")
    a("")
    a("/** A level arc struck into the seat ring around a knob.")
    a(" *")
    a(" *  The recessed seat every widget stands in is already a dark band a")
    a(" *  little wider than the knob it holds; drawing the meter there costs the")
    a(" *  panel no width and no height at all, which is what makes it affordable")
    a(" *  on a control that has four of itself in a row. It sweeps the knob's own")
    a(" *  travel, so full scale is where the pointer would be at maximum.")
    a(" *")
    a(" *  Point `value` at a float the module keeps in 0..1. A null pointer draws")
    a(" *  nothing, which is what a browser preview and a module-less panel want. */")
    a("struct MeterArc : widget::Widget {")
    a("\tfloat* value = NULL;")
    a("\t// px. A knob is 4.80 mm and its well 5.70, which at Rack's 2.9528 px")
    a("\t// per mm is a band from 14.17 to 16.83 -- the dark seat already drawn")
    a("\t// around every widget. The arc runs up the middle of it.")
    a("\tfloat r = 15.5f;")
    a("\tNVGcolor ink = LIME;")
    a("")
    a("\tvoid drawLayer(const DrawArgs& args, int layer) override {")
    a("\t\tif (layer != 1 || !value) return;")
    a("\t\tfloat v = *value;")
    a("\t\tif (!(v > 0.f)) return;")
    a("\t\tif (v > 1.f) v = 1.f;")
    a("\t\t// Rack turns a knob from -0.83*pi to +0.83*pi, measured from")
    a("\t\t// straight up; nvg measures from the positive x axis.")
    a("\t\tconst float span = 0.83f * M_PI;")
    a("\t\tfloat a0 = -M_PI / 2.f - span;")
    a("\t\tnvgBeginPath(args.vg);")
    a("\t\tnvgArc(args.vg, box.size.x / 2.f, box.size.y / 2.f, r,")
    a("\t\t       a0, a0 + 2.f * span * v, NVG_CW);")
    a("\t\tnvgStrokeColor(args.vg, ink);")
    a("\t\tnvgStrokeWidth(args.vg, 1.6f);")
    a("\t\tnvgLineCap(args.vg, NVG_ROUND);")
    a("\t\tnvgStroke(args.vg);")
    a("\t}")
    a("};")
    a("")
    _sw, _sh = S.EXTENT["stepper"]
    for _ln in STEP_PAIR.replace("@W@", "%.4f" % (2 * _sw)).replace("@H@", "%.4f" % (2 * _sh)).splitlines():
        a(_ln)
    a("")
    for _ln in SCREEN_FIELDS.splitlines():
        a(_ln)
    a("")
    a("/** Every stock light derives from GrayModuleLightWidget, which hard-codes a")
    a("    #333333 socket that reads as a grey hole punched in a green panel. These")
    a("    restyle the socket as well as the emitter. */")
    gr, gg, gb = P.rgb(P.GLASS)
    rr, rg, rb = P.rgb(P.RULE)
    a("template <typename TBase = GrayModuleLightWidget>")
    a("struct TSocketLight : TBase {")
    a("\tTSocketLight() {")
    a("\t\tthis->bgColor = nvgRGBA(0x%02x, 0x%02x, 0x%02x, 0xff);" % (gr, gg, gb))
    a("\t\tthis->borderColor = nvgRGBA(0x%02x, 0x%02x, 0x%02x, 0xa0);" % (rr, rg, rb))
    a("\t}")
    a("};")
    a("")
    for nm, cols in [("Lime", ["LIME"]), ("Mint", ["MINT"]),
                     ("Paper", ["PAPER"]), ("Clay", ["CLAY"]),
                     ("Verdict", ["MINT", "CLAY"])]:
        a("template <typename TBase = GrayModuleLightWidget>")
        a("struct T%sLight : TSocketLight<TBase> {" % nm)
        a("\tT%sLight() { %s }"
          % (nm, " ".join("this->addBaseColor(%s);" % c for c in cols)))
        a("};")
        a("using %sLight = T%sLight<>;" % (nm, nm))
    a("")
    a("/** A light whose colour the module picks rather than the panel: three base")
    a("    colours the caller mixes by setting their brightnesses. For an indicator")
    a("    that has to be told apart from its neighbours -- one step of a sequence,")
    a("    one slot of a bank -- where a fixed ink can only say on or off. Feed it")
    a("    the components of an NVGcolor and it wears that colour; the family socket")
    a("    is kept, so it still reads as part of the panel and not as a stock LED. */")
    a("template <typename TBase = GrayModuleLightWidget>")
    a("struct TRgbLight : TSocketLight<TBase> {")
    a("\tTRgbLight() {")
    a("\t\tthis->addBaseColor(nvgRGB(0xff, 0x00, 0x00));")
    a("\t\tthis->addBaseColor(nvgRGB(0x00, 0xff, 0x00));")
    a("\t\tthis->addBaseColor(nvgRGB(0x00, 0x00, 0xff));")
    a("\t}")
    a("};")
    a("using RgbLight = TRgbLight<>;")
    a("")
    a("/** The four corner screws, at the positions every panel in the family uses.")
    a("    Width comes from the widget's own box, so this needs no per-panel number. */")
    a("inline void addScrews(app::ModuleWidget* mw) {")
    a("\tmw->addChild(createWidget<ScrewHex>(Vec(RACK_GRID_WIDTH, 0)));")
    a("\tmw->addChild(createWidget<ScrewHex>(Vec(mw->box.size.x - 2 * RACK_GRID_WIDTH, 0)));")
    a("\tmw->addChild(createWidget<ScrewHex>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));")
    a("\tmw->addChild(createWidget<ScrewHex>(Vec(mw->box.size.x - 2 * RACK_GRID_WIDTH,")
    a("\t                                        RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));")
    a("}")
    a("")
    a("// --- silkscreen ------------------------------------------------------------")
    a("/** One word on the panel. The table itself is per-panel and lives in")
    a("    Panel.hpp; this is only its shape and how it is drawn. */")
    a("struct Label {")
    a("\tfloat x, y, size, tracking;")
    a("\tNVGcolor ink;")
    a("\tint align;")
    a("\tint turn;          // 0 upright, 1 mirrored (the maker's mark), 2 reads up, 3 reads down")
    a("\tconst char* text;")
    a("};")
    a("")
    a("/** The style one silkscreen entry is set in. */")
    a("inline TextStyle labelStyle(const Label& l) {")
    a("\treturn TextStyle(Face::Ui, l.size, l.ink, l.align, l.tracking);")
    a("}")
    a("")
    a("/** Draws a panel's silkscreen. Rack's nanosvg ignores <text>, so this is the")
    a("    only way a label reaches the panel. The table is passed in rather than")
    a("    referenced by name so that one definition of this widget serves all three")
    a("    panels in the plugin. */")
    a("struct Labels : widget::Widget {")
    a("\tconst Label* labels = NULL;")
    a("\tsize_t count = 0;")
    a("")
    a("\tvoid draw(const DrawArgs& args) override {")
    a("\t\tfor (size_t i = 0; i < count; i++) {")
    a("\t\t\tconst Label& l = labels[i];")
    a("\t\t\tVec p = mm(l.x, l.y);")
    a("\t\t\tif (l.turn == 1)")
    a("\t\t\t\ttextMirrored(args.vg, labelStyle(l), p.x, p.y, l.text);")
    a("\t\t\telse if (l.turn >= 2) {")
    a("\t\t\t\tnvgSave(args.vg);")
    a("\t\t\t\tnvgTranslate(args.vg, p.x, p.y);")
    a("\t\t\t\tnvgRotate(args.vg, (l.turn == 2 ? -M_PI : M_PI) / 2.f);")
    a("\t\t\t\ttext(args.vg, labelStyle(l), 0.f, 0.f, l.text);")
    a("\t\t\t\tnvgRestore(args.vg);")
    a("\t\t\t}")
    a("\t\t\telse")
    a("\t\t\t\ttext(args.vg, labelStyle(l), p.x, p.y, l.text);")
    a("\t\t}")
    a("\t\tWidget::draw(args);")
    a("\t}")
    a("};")
    a("")
    a("inline void addLabels(app::ModuleWidget* mw, const Label* labels, size_t count) {")
    a("\tLabels* l = new Labels;")
    a("\tl->labels = labels;")
    a("\tl->count = count;")
    a("\tl->box.pos = Vec(0, 0);")
    a("\tl->box.size = mw->box.size;")
    a("\tmw->addChild(l);")
    a("}")
    a("")
    a("} // namespace panel")
    return "\n".join(o) + "\n"


def panel_header(panel, sol):
    """src/<Module>/Panel.hpp -- this panel's numbers, and nothing else."""
    o = []
    a = o.append
    a("// Generated by panelkit from tools/panels/%s.py. Do not edit; edit the spec."
      % panel.slug)
    a("//")
    a("// Everything the panel's C++ needs to agree with res/%s.svg: the millimetre" % panel.slug)
    a("// grid, the widget positions and the whole silkscreen. Rack's nanosvg discards")
    a("// <text>, so labels cannot live in the SVG -- they are drawn at runtime from")
    a("// the table below, generated from the same spec that drew the artwork.")
    a("//")
    a("// Every declaration here is `static`, i.e. private to the translation unit, so")
    a("// the three panels in this plugin cannot collide. That also means no")
    a("// translation unit may include two of these.")
    a("#pragma once")
    a('#include "../PanelTheme.hpp"')
    a("")
    a("namespace panel {")
    a("")
    a("// --- identity --------------------------------------------------------------")
    a("static const int   HP = %d;" % panel.hp)
    a("static const float W  = %.4ff;  // mm" % panel.w)
    a("static const float H  = %.4ff;  // mm" % panel.h)
    if panel.what:
        a("//: What the module is, as the masthead's second line says it.")
        a('static const char* const WHAT = "%s";' % panel.what.replace('"', '\\"'))
    a("")
    if sol.glass:
        gy, gh = sol.glass
        a("// --- the read-out well: the spec's numbers, not the widget's ---------------")
        a("// A display used to be positioned by hand in the module's C++, in the same")
        a("// millimetres the spec had already chosen -- two copies of one number, and")
        a("// they drifted. The widget takes them from here now.")
        gx0, gx1 = sol.glass_x
        a("static const float GLASS_X = %.4ff;" % gx0)
        a("static const float GLASS_Y = %.4ff;" % gy)
        a("static const float GLASS_W = %.4ff;" % (gx1 - gx0))
        a("static const float GLASS_H = %.4ff;" % gh)
        a("")
    if sol.fields:
        a("// --- controls on the glass: where each is drawn and where it is grabbed --")
        a("// One rectangle per Field in the spec, in panel mm. Place the widget with")
        a("// createField<Screen...>(FIELD_X, ...) -- or createMenuField for a \"menu\" --")
        a("// and draw the value in")
        a("// inGlass(FIELD_X), the same rectangle in the display's own pixels.")
        for name, x, y, w, h, kind in sol.fields:
            a("static const Rect FIELD_%s = Rect(Vec(%.4ff, %.4ff), Vec(%.4ff, %.4ff));  // %s: %s"
              % (name.upper(), x, y, w, h, kind, S.FIELD_KINDS[kind]))
        a("")
        a("/** A field's rectangle in the display's pixels, for a display placed on the glass. */")
        a("static inline Rect inGlass(const Rect& f) {")
        a("\treturn mmRect(f.pos.x - GLASS_X, f.pos.y - GLASS_Y, f.size.x, f.size.y);")
        a("}")
        a("")
    if panel.metrics:
        a("// --- panel metrics, mirrored from tools/panels/%s.py ---" % panel.slug)
        for k in sorted(panel.metrics):
            v = panel.metrics[k]
            if isinstance(v, int):
                a("static const int   %-14s = %d;" % (k, v))
            else:
                a("static const float %-14s = %.4ff;" % (k, v))
        a("")
    a("// --- silkscreen ------------------------------------------------------------")
    a("static const Label LABELS[] = {")
    for l in masthead(panel, sol) + [x for x in sol.labels if x["text"]]:
        # The role is what the spec said; the constant is what it lands as on
        # the ground it sits on -- dark ink on the face, pale on the bands.
        cname = P.ink(l["ink"], l.get("ground", "light"))[0]
        a('\t{%8.4ff, %8.4ff, %5.2ff, %.2ff, %-9s, %-17s %-6s "%s"},'
          % (l["x"], l["y"], l["size"], l["tracking"], cname,
             ALIGN[l["align"]] + " | NVG_ALIGN_BASELINE,",
             "1," if l.get("mirror") else {"up": "2,", "down": "3,"}.get(l.get("turn"), "0,"),
             l["text"].replace("\\", "\\\\").replace('"', '\\"')))
    a("};")
    a("")
    a("/** Draws this panel's silkscreen. `static` so each module keeps its own,")
    a("    bound to its own table. */")
    a("static inline void addLabels(app::ModuleWidget* mw) {")
    a("\taddLabels(mw, LABELS, sizeof(LABELS) / sizeof(LABELS[0]));")
    a("}")
    a("")
    a("// --- widget positions, by the names used in tools/panels/%s.py -----" % panel.slug)
    a("// Positions are mm; feed them to mm() or createParamCentered.")
    for name, x, y, kind in sol.widgets:
        a("static const Vec %s_POS = Vec(%.4f, %.4f);" % (name.upper(), x, y))
    a("")
    a("} // namespace panel")
    return "\n".join(o) + "\n"
