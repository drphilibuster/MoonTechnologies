#pragma once
#include <stdint.h>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// The three pictures, drawn into a plain RGBA buffer.
//
// CPU rasterisation rather than GL, for one reason: what leaves this module goes
// onto the video bus (src/VideoBus.hpp), which carries bytes, and Transmittal
// uploads those to a texture once. Rendering on the GPU here would mean reading
// back off it to get the bytes, which is the slowest thing in the whole path and
// would be done to save the fastest.
//
// Nothing here allocates, takes a lock, or knows about Rack. It is called from
// Projection's worker thread with a buffer it owns.

namespace projection {

/** Where the picture goes. `px` is w*h*4 bytes, RGBA, top row first -- the same
    orientation Repossession's decoder produces and the bus carries. */
struct Canvas {
	uint8_t* px = NULL;
	int w = 0;
	int h = 0;
	bool ok() const { return px && w > 0 && h > 0; }
	size_t bytes() const { return (size_t) w * (size_t) h * 4u; }
};

/** What the controls add up to, after CV. All 0..1 unless said otherwise. */
struct Look {
	float hue = 0.33f;     // 0..1 round the wheel
	float sat = 0.8f;
	float scale = 0.5f;
	float warp = 0.3f;
	float trail = 0.5f;    // 0 = no persistence, 1 = almost none lost per frame
	float flash = 0.f;     // 0..1, decays outside; added to everything
};

/** HSV to 8-bit RGB. Cheap and branchy rather than clever: it runs per band or
    per pixel-block, not per pixel, everywhere it is used. */
inline void hsv(float h, float s, float v, uint8_t* rgb) {
	h = h - std::floor(h);
	if (s < 0.f) s = 0.f;
	if (s > 1.f) s = 1.f;
	if (v < 0.f) v = 0.f;
	if (v > 1.f) v = 1.f;
	float c = v * s;
	float x = c * (1.f - std::fabs(std::fmod(h * 6.f, 2.f) - 1.f));
	float m = v - c;
	float r = 0.f, g = 0.f, b = 0.f;
	int seg = (int) (h * 6.f) % 6;
	switch (seg) {
		case 0: r = c; g = x; break;
		case 1: r = x; g = c; break;
		case 2: g = c; b = x; break;
		case 3: g = x; b = c; break;
		case 4: r = x; b = c; break;
		default: r = c; b = x; break;
	}
	rgb[0] = (uint8_t) ((r + m) * 255.f + 0.5f);
	rgb[1] = (uint8_t) ((g + m) * 255.f + 0.5f);
	rgb[2] = (uint8_t) ((b + m) * 255.f + 0.5f);
}

inline void clear(Canvas c) {
	if (!c.ok())
		return;
	std::memset(c.px, 0, c.bytes());
	for (size_t i = 3; i < c.bytes(); i += 4)
		c.px[i] = 255;
}

/** Fade what is already there, which is what makes a trail a trail.

    TRAIL is a per-second decay rather than a per-frame one, so a trail lasts as
    long at 24 fps as it does at 60. A per-frame factor would make the picture
    change character with the output rate, which is a setting nobody expects to
    be a look control. */
inline void fade(Canvas c, float trail, float dt) {
	if (!c.ok())
		return;
	if (trail <= 0.f) {
		clear(c);
		return;
	}
	// trail 1.0 would never fade at all and the picture would fill in solid, so
	// the top of the range is a long tail rather than an infinite one.
	float tau = 0.02f + trail * 1.5f;
	float k = std::exp(-dt / tau);
	// 256, not 255. Scaling by 255 and shifting by 8 is a division by 256, so
	// every frame loses an extra 1/256 that has nothing to do with the time
	// constant -- and at 60 fps that happens two and a half times as often as at
	// 24, which makes TRAIL quietly mean something different at each output
	// rate. The whole point of the control is that it does not.
	int m = (int) (k * 256.f + 0.5f);
	if (m < 0) m = 0;
	if (m > 256) m = 256;
	size_t n = c.bytes();
	for (size_t i = 0; i < n; i += 4) {
		for (int q = 0; q < 3; q++) {
			int p = c.px[i + q];
			// Rounded rather than truncated, so the error has no direction and
			// does not accumulate with the frame rate -- but then a dim pixel
			// rounds back to itself and would sit there forever, so anything
			// still lit is forced down by one. That is what makes a trail
			// actually reach black instead of leaving a ghost the picture
			// slowly fills in with.
			int v = (p * m + 128) >> 8;
			if (v >= p && p > 0)
				v = p - 1;
			c.px[i + q] = (uint8_t) v;
		}
	}
}

/** Additive plot of one point, clipped rather than wrapped.

    Clipping matters: a signal past the edge of the screen should pile up on the
    edge, the way an oscilloscope's does. Wrapping would put a loud left channel
    on the right of the picture, which reads as a glitch nobody asked for. */
inline void splat(Canvas c, float x, float y, const uint8_t* rgb, float a) {
	if (!c.ok() || !(a > 0.f))
		return;
	int xi = (int) (x + 0.5f), yi = (int) (y + 0.5f);
	if (xi < 0) xi = 0;
	if (xi >= c.w) xi = c.w - 1;
	if (yi < 0) yi = 0;
	if (yi >= c.h) yi = c.h - 1;
	if (a > 1.f) a = 1.f;
	uint8_t* p = c.px + ((size_t) yi * (size_t) c.w + (size_t) xi) * 4u;
	for (int i = 0; i < 3; i++) {
		int v = p[i] + (int) (rgb[i] * a);
		p[i] = (uint8_t) (v > 255 ? 255 : v);
	}
}

/** MODE 1 -- the XY scope. `xs`/`ys` are +/-1 nominal; anything past that is
    clipped to the edge. Consecutive samples are joined so a sparse signal reads
    as a line rather than as dots. */
inline void renderScope(Canvas c, const float* xs, const float* ys, int n,
                        const Look& look) {
	if (!c.ok() || n <= 0)
		return;
	uint8_t rgb[3];
	hsv(look.hue, look.sat, 1.f, rgb);
	float zoom = 0.25f + look.scale * 1.5f;
	float cx = c.w * 0.5f, cy = c.h * 0.5f;
	float rx = cx * zoom, ry = cy * zoom;
	float px = 0.f, py = 0.f;
	bool have = false;
	for (int i = 0; i < n; i++) {
		float x = cx + xs[i] * rx;
		float y = cy - ys[i] * ry;
		if (!(x == x) || !(y == y))     // a NaN in the audio must not draw
			continue;
		if (have) {
			// Join to the previous sample. Step count from the longer axis, so
			// the line has no gaps whichever way it runs.
			float dx = x - px, dy = y - py;
			int steps = (int) (std::fabs(dx) > std::fabs(dy)
				? std::fabs(dx) : std::fabs(dy));
			if (steps > 64) steps = 64;   // a jump across the screen is not a line
			for (int s = 1; s <= steps; s++) {
				float t = (float) s / (float) (steps + 1);
				splat(c, px + dx * t, py + dy * t, rgb, 0.5f);
			}
		}
		splat(c, x, y, rgb, 0.9f);
		px = x; py = y; have = true;
	}
}

/** MODE 2 -- spectrum bars, one per band, growing from the bottom. */
inline void renderBars(Canvas c, const float* bands, int n, const Look& look) {
	if (!c.ok() || n <= 0)
		return;
	float gain = 0.5f + look.scale * 6.f;
	int bw = c.w / n;
	if (bw < 1) bw = 1;
	for (int i = 0; i < n; i++) {
		float v = bands[i] * gain;
		if (!(v > 0.f)) v = 0.f;
		if (v > 1.f) v = 1.f;
		int top = (int) ((1.f - v) * (float) c.h);
		if (top < 0) top = 0;
		if (top > c.h) top = c.h;
		uint8_t rgb[3];
		// Hue walks across the spectrum, so which band is loud is readable as
		// colour and not only as height.
		hsv(look.hue + look.warp * (float) i / (float) n, look.sat, 1.f, rgb);
		int x0 = i * bw, x1 = x0 + bw - 1;
		if (i == n - 1) x1 = c.w - 1;
		for (int y = top; y < c.h; y++) {
			uint8_t* row = c.px + (size_t) y * (size_t) c.w * 4u;
			for (int x = x0; x <= x1 && x < c.w; x++) {
				uint8_t* p = row + (size_t) x * 4u;
				for (int k = 0; k < 3; k++) {
					int val = p[k] + rgb[k];
					p[k] = (uint8_t) (val > 255 ? 255 : val);
				}
			}
		}
	}
}

/** MODE 3 -- a per-pixel field, warped by the bands.

    Computed on a coarse lattice and expanded, because the honest per-pixel
    version at 1280x720 is nearly a million transcendental functions a frame and
    this has to share a machine with an audio engine. The lattice is 4x4, which
    at 720p is 57,600 evaluations -- and the field is smooth enough that the
    difference does not show. */
inline void renderField(Canvas c, const float* bands, int n, float phase,
                        const Look& look) {
	if (!c.ok() || n <= 0)
		return;
	const int STEP = 4;
	float lo = bands[0];
	float mid = bands[n / 2];
	float hi = bands[n - 1];
	float zoom = 1.f + look.scale * 7.f;
	float warp = look.warp * 6.f;
	for (int y = 0; y < c.h; y += STEP) {
		float v = (float) y / (float) c.h - 0.5f;
		for (int x = 0; x < c.w; x += STEP) {
			float u = (float) x / (float) c.w - 0.5f;
			float a = std::sin((u * zoom + phase) * 3.14159265f
			                   + lo * warp);
			float b = std::sin((v * zoom - phase * 0.7f) * 3.14159265f
			                   + mid * warp);
			float d = std::sin((u + v) * zoom * 1.7f + hi * warp + phase * 1.3f);
			float f = (a + b + d) * (1.f / 3.f);
			float val = 0.5f + 0.5f * f;
			uint8_t rgb[3];
			hsv(look.hue + f * 0.25f, look.sat, val, rgb);
			for (int yy = y; yy < y + STEP && yy < c.h; yy++) {
				uint8_t* row = c.px + (size_t) yy * (size_t) c.w * 4u;
				for (int xx = x; xx < x + STEP && xx < c.w; xx++) {
					uint8_t* p = row + (size_t) xx * 4u;
					for (int k = 0; k < 3; k++) {
						int q = p[k] + rgb[k];
						p[k] = (uint8_t) (q > 255 ? 255 : q);
					}
				}
			}
		}
	}
}

/** A gate's flash, added over whatever was drawn. */
inline void applyFlash(Canvas c, float flash) {
	if (!c.ok() || !(flash > 0.f))
		return;
	if (flash > 1.f)
		flash = 1.f;
	int add = (int) (flash * 255.f);
	size_t nb = c.bytes();
	for (size_t i = 0; i < nb; i += 4)
		for (int k = 0; k < 3; k++) {
			int v = c.px[i + k] + add;
			c.px[i + k] = (uint8_t) (v > 255 ? 255 : v);
		}
}

} // namespace projection
