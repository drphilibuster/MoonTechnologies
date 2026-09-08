#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// What Transmittal asks ffmpeg to do, and when it asks for the next frame.
//
// Kept out of the .cpp because both halves are quietly easy to get wrong and
// neither announces itself:
//
//   * the argv. One wrong token and ffmpeg exits with a message nobody sees,
//     leaving a module that looks like it is running and produces nothing. The
//     input side in particular has to describe the raw frames exactly -- pixel
//     format, size and rate -- because rawvideo carries no header to correct a
//     wrong guess. Get the size wrong and ffmpeg does not fail: it shears the
//     picture and keeps going.
//
//   * the pacing. Frames arrive when the engine says so and have to leave at a
//     fixed rate, and the two are unrelated numbers. Accumulating a float
//     remainder is how a stream ends up slowly ahead of or behind real time.
//
// The transport is HLS: ffmpeg writes a rolling playlist and a few segments to
// a directory, and TouchDesigner's Video Stream In TOP reads the .m3u8. It is
// not the fast way -- a player sits a segment or two behind, so expect a couple
// of seconds -- but it needs nothing of ffmpeg beyond what every build has, and
// this machine's ffmpeg has neither SRT nor RTSP.

namespace transmittal {

/** The sizes offered on the panel. The first is Repossession's own frame size,
    so its thumbnail can go out without being scaled at all. */
struct Size { int w, h; const char* name; };
static const Size kSizes[] = {
	{ 160,  90, "160x90"   },
	{ 320, 180, "320x180"  },
	{ 640, 360, "640x360"  },
	{ 1280, 720, "1280x720" },
};
static const int kNumSizes = 4;

static const int kRates[] = { 12, 15, 24, 30 };
static const int kNumRates = 4;

/** Segment length. One second is the shortest the HLS muxer handles without
    the playlist churning faster than a player refetches it. */
static const int kSegmentSec = 1;
static const int kSegmentsKept = 3;

/** The command line. `ffmpeg` is the resolved binary, `dir` the directory the
    playlist and its segments are written into, and `hw` asks for the platform
    hardware encoder rather than libx264 -- every ffmpeg has one or the other,
    so the caller decides and the failure is visible either way. */
inline std::vector<std::string> argvFor(const std::string& ffmpeg,
                                        const std::string& playlist,
                                        int w, int h, int fps, bool hw,
                                        const char* hwCodec) {
	std::vector<std::string> a;
	a.push_back(ffmpeg);
	a.push_back("-hide_banner");
	a.push_back("-loglevel");
	a.push_back("error");
	// The input is headerless, so every one of these four is load-bearing.
	a.push_back("-f");            a.push_back("rawvideo");
	a.push_back("-pixel_format"); a.push_back("rgba");
	a.push_back("-video_size");   a.push_back(std::to_string(w) + "x" + std::to_string(h));
	a.push_back("-framerate");    a.push_back(std::to_string(fps));
	a.push_back("-i");            a.push_back("-");
	a.push_back("-c:v");          a.push_back(hw ? hwCodec : "libx264");
	if (!hw) {
		// Live, not archival: never wait for future frames.
		a.push_back("-preset");   a.push_back("veryfast");
		a.push_back("-tune");     a.push_back("zerolatency");
	}
	a.push_back("-pix_fmt");      a.push_back("yuv420p");
	// A keyframe per segment, or the muxer cannot cut where it says it will.
	a.push_back("-g");            a.push_back(std::to_string(fps * kSegmentSec));
	a.push_back("-f");            a.push_back("hls");
	a.push_back("-hls_time");     a.push_back(std::to_string(kSegmentSec));
	a.push_back("-hls_list_size"); a.push_back(std::to_string(kSegmentsKept));
	// delete_segments keeps the directory from growing without bound; the
	// stream may run for an hour. omit_endlist keeps it a live playlist rather
	// than one a player treats as a finished recording.
	a.push_back("-hls_flags");
	a.push_back("delete_segments+omit_endlist+independent_segments");
	a.push_back(playlist);
	return a;
}

/** Turns elapsed engine time into "send a frame now", without drift.

    The remainder is carried in whole seconds of accumulated time rather than
    by rounding a frame index, so a rate that does not divide the block size --
    which is all of them -- cannot walk away from real time over a long stream. */
struct Pacer {
	double acc = 0.0;
	double period = 1.0 / 15.0;

	void setRate(int fps) {
		period = (fps > 0) ? 1.0 / (double)fps : 1.0 / 15.0;
		if (acc > period)
			acc = period;
	}

	void reset() { acc = 0.0; }

	/** Advance by `dt` seconds; true when a frame is due. At most one frame per
	    call: falling behind must not turn into a burst that outruns the pipe,
	    which would only move the backlog into ffmpeg's buffer. */
	bool tick(double dt) {
		acc += dt;
		if (acc < period)
			return false;
		acc -= period;
		// More than a whole frame behind means the stream stalled -- the engine
		// was paused, or the source stopped. Those frames are gone, not owed:
		// start the next interval clean rather than leaving a frame's worth on
		// the clock, which would fire again on the very next call. This clamp
		// cannot touch normal running, where acc stays under one period and the
		// remainder carries exactly.
		if (acc > period)
			acc = 0.0;
		return true;
	}
};

/** Nearest-neighbour scale of an RGBA frame. Not a good resampler -- ffmpeg
    has a good one and will be given the frames at their published size when
    they already match -- but a source publishing 160x90 into a 1280x720 stream
    has to be enlarged somewhere, and doing it here keeps the pipe's frame size
    fixed for the whole run. Changing it mid-stream would mean restarting
    ffmpeg, because rawvideo's size is set once on the command line. */
inline void scaleRgba(const uint8_t* src, int sw, int sh,
                      uint8_t* dst, int dw, int dh) {
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
		return;
	for (int y = 0; y < dh; y++) {
		int sy = (int)((int64_t)y * sh / dh);
		const uint8_t* srow = src + (size_t)sy * (size_t)sw * 4u;
		uint8_t* drow = dst + (size_t)y * (size_t)dw * 4u;
		for (int x = 0; x < dw; x++) {
			int sx = (int)((int64_t)x * sw / dw);
			const uint8_t* p = srow + (size_t)sx * 4u;
			uint8_t* q = drow + (size_t)x * 4u;
			q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3];
		}
	}
}

} // namespace transmittal
