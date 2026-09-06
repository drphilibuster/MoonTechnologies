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


def title_size(panel):
    """Largest size at which the masthead still clears both top screws."""
    avail = panel.w - 2 * SCREW_CLEAR
    n = max(len(panel.title), 1)
    size = (avail / (n * S.MM_PER_PX) - TITLE_TRACK) / 0.60
    return max(TITLE_MIN, min(TITLE_MAX, size))


def masthead(panel):
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
    # The masthead reads like a note's header: the issuing office bottom-left,
    # the series number bottom-right, the denomination across the top.
    if panel.brand:
        out.append(dict(x=SCREW_CLEAR + 0.4, y=LOGO_Y, text="$", size=LOGO_SIZE,
                        ink="LIME", align="left", tracking=0.0, mirror=True))
        out.append(dict(x=SCREW_CLEAR + 0.4 + LOGO_W + LOGO_GAP, y=STUB_Y,
                        text=panel.brand,
                        size=STUB_SIZE, ink="SAGE", align="left", tracking=0.4))
    if panel.form:
        out.append(dict(x=panel.w - 4.2, y=STUB_Y, text=panel.form,
                        size=STUB_SIZE, ink="SAGE", align="right", tracking=0.4))
    # Everything in the masthead sits on the dark band.
    for l in out:
        l["ground"] = "dark"
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
    a("struct PortOut : app::SvgPort {")
    a("\tPortOut() {")
    a('\t\tsetSvg(Svg::load(asset::plugin(pluginInstance, "res/PortOut.svg")));')
    a("\t}")
    a("};")
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
    a("\tbool mirror;")
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
    a("\t\t\tif (l.mirror)")
    a("\t\t\t\ttextMirrored(args.vg, labelStyle(l), p.x, p.y, l.text);")
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
    for l in masthead(panel) + [x for x in sol.labels if x["text"]]:
        # The role is what the spec said; the constant is what it lands as on
        # the ground it sits on -- dark ink on the face, pale on the bands.
        cname = P.ink(l["ink"], l.get("ground", "light"))[0]
        a('\t{%8.4ff, %8.4ff, %5.2ff, %.2ff, %-9s, %-17s %-6s "%s"},'
          % (l["x"], l["y"], l["size"], l["tracking"], cname,
             ALIGN[l["align"]] + " | NVG_ALIGN_BASELINE,",
             "true," if l.get("mirror") else "false,",
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
