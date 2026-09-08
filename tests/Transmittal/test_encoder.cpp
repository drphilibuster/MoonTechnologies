// Transmittal's encoder command line, pacing and scaler, and the video bus.
//
// Everything here fails silently in the real module, which is why it is tested
// at all. A wrong argv token makes ffmpeg exit with a message that goes to a
// pipe nobody is watching, leaving a panel that says it is streaming and a
// playlist that never appears. A wrong -video_size does not even fail: rawvideo
// has no header to contradict it, so ffmpeg shears the picture and carries on.
// Pacing drift takes minutes to become visible and is invisible in a short
// test unless it is measured over a long one, so it is measured over an hour.

#include "../../src/Transmittal/Encoder.hpp"
#include "../../src/VideoBus.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace transmittal;

static int checks = 0;
static int failures = 0;

static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

/** The value following `flag`, or "" when the flag is absent. */
static std::string valueOf(const std::vector<std::string>& a, const char* flag) {
	for (size_t i = 0; i + 1 < a.size(); i++)
		if (a[i] == flag)
			return a[i + 1];
	return "";
}

static int countOf(const std::vector<std::string>& a, const char* tok) {
	int n = 0;
	for (size_t i = 0; i < a.size(); i++)
		if (a[i] == tok) n++;
	return n;
}

int main() {
	printf("Transmittal encoder and video bus\n");

	// --- the input side describes the frames exactly -------------------------
	// rawvideo carries no header, so these four are the only thing telling
	// ffmpeg what is on the pipe. A wrong one does not fail loudly.
	{
		bool ok = true;
		char detail[192] = "";
		for (int s = 0; s < kNumSizes && ok; s++) {
			for (int r = 0; r < kNumRates && ok; r++) {
				std::vector<std::string> a = argvFor("/usr/bin/ffmpeg", "/tmp/s.m3u8",
					kSizes[s].w, kSizes[s].h, kRates[r], true, "h264_videotoolbox");
				char want[32];
				snprintf(want, sizeof want, "%dx%d", kSizes[s].w, kSizes[s].h);
				if (valueOf(a, "-video_size") != want
				    || valueOf(a, "-pixel_format") != "rgba"
				    || valueOf(a, "-f") != "rawvideo"
				    || valueOf(a, "-framerate") != std::to_string(kRates[r])
				    || valueOf(a, "-i") != "-") {
					ok = false;
					snprintf(detail, sizeof detail,
					         "%dx%d @%d: size=%s fmt=%s f=%s rate=%s i=%s",
					         kSizes[s].w, kSizes[s].h, kRates[r],
					         valueOf(a, "-video_size").c_str(),
					         valueOf(a, "-pixel_format").c_str(),
					         valueOf(a, "-f").c_str(),
					         valueOf(a, "-framerate").c_str(),
					         valueOf(a, "-i").c_str());
				}
			}
		}
		check("the raw input is described exactly", ok, detail);
	}

	// --- the playlist is the last argument and the output is HLS -------------
	// ffmpeg takes the output as a bare trailing token. If anything were
	// appended after it, that token would become the output file instead and
	// the playlist would silently never be written.
	{
		std::vector<std::string> a = argvFor("/usr/bin/ffmpeg", "/tmp/out/stream.m3u8",
			320, 180, 15, true, "h264_videotoolbox");
		bool ok = a.back() == "/tmp/out/stream.m3u8"
		       && countOf(a, "-f") == 2                    // rawvideo in, hls out
		       && a[a.size() - 2] != "-f";                  // ...and not adjacent
		check("the playlist is the trailing argument", ok);
	}

	// --- a live playlist, not a recording ------------------------------------
	// Without omit_endlist a player treats the stream as finished; without
	// delete_segments the directory grows for as long as the stream runs.
	{
		std::string flags = valueOf(argvFor("/usr/bin/ffmpeg", "/tmp/s.m3u8",
			320, 180, 15, true, "h264_videotoolbox"), "-hls_flags");
		bool ok = flags.find("omit_endlist") != std::string::npos
		       && flags.find("delete_segments") != std::string::npos;
		check("the playlist is live and self-pruning", ok, flags.c_str());
	}

	// --- a keyframe every segment --------------------------------------------
	// The muxer can only cut a segment on a keyframe. With -g longer than the
	// segment, the segments run long and the playlist's own durations lie.
	{
		bool ok = true;
		char detail[96] = "";
		for (int r = 0; r < kNumRates; r++) {
			std::string g = valueOf(argvFor("/usr/bin/ffmpeg", "/tmp/s.m3u8",
				320, 180, kRates[r], true, "h264_videotoolbox"), "-g");
			if (g != std::to_string(kRates[r] * kSegmentSec)) {
				ok = false;
				snprintf(detail, sizeof detail, "%d fps gave -g %s", kRates[r], g.c_str());
			}
		}
		check("one keyframe per segment at every rate", ok, detail);
	}

	// --- software encoding never waits for future frames ---------------------
	// libx264's defaults buffer frames to look ahead, which is right for a file
	// and wrong for a stream: it adds latency to a path that already has HLS's.
	{
		std::vector<std::string> sw = argvFor("/usr/bin/ffmpeg", "/tmp/s.m3u8",
			320, 180, 15, false, "h264_videotoolbox");
		std::vector<std::string> hw = argvFor("/usr/bin/ffmpeg", "/tmp/s.m3u8",
			320, 180, 15, true, "h264_videotoolbox");
		bool ok = valueOf(sw, "-c:v") == "libx264"
		       && valueOf(sw, "-tune") == "zerolatency"
		       && valueOf(hw, "-c:v") == "h264_videotoolbox"
		       && valueOf(hw, "-tune") == "";     // meaningless to the hw encoder
		check("software encoding is tuned for live, hardware is left alone", ok);
	}

	// --- the pacer does not drift over an hour -------------------------------
	// The engine's block time and the frame period never divide each other, so
	// the remainder has to be carried. Rounding a frame index instead is the
	// bug this exists to prevent, and it takes minutes to become visible.
	{
		bool ok = true;
		char detail[160] = "";
		const double dt = 1.0 / 44100.0 * 64.0;      // a 64-sample block
		for (int r = 0; r < kNumRates && ok; r++) {
			Pacer p;
			p.setRate(kRates[r]);
			long frames = 0;
			const double hours = 1.0;
			long blocks = (long)(hours * 3600.0 / dt);
			for (long i = 0; i < blocks; i++)
				if (p.tick(dt)) frames++;
			double want = hours * 3600.0 * kRates[r];
			double err = std::fabs((double)frames - want) / want;
			if (err > 0.001) {
				ok = false;
				snprintf(detail, sizeof detail, "%d fps: %ld frames in an hour, wanted %.0f",
				         kRates[r], frames, want);
			}
		}
		check("the pacer holds rate over an hour", ok, detail);
	}

	// --- and never bursts -----------------------------------------------------
	// A long stall must not be repaid all at once. Frames owed are frames gone:
	// a burst only moves the backlog into ffmpeg's input buffer, where it turns
	// into latency that never comes back.
	{
		Pacer p;
		p.setRate(30);
		p.tick(5.0);                       // a five-second stall
		int burst = 0;
		for (int i = 0; i < 100; i++)
			if (p.tick(0.0)) burst++;
		char detail[96];
		snprintf(detail, sizeof detail, "%d frames released after a 5 s stall", burst);
		check("a stall is not repaid as a burst", burst == 0, detail);
	}

	// --- the scaler covers the destination and reads only the source ---------
	{
		const int sw = 160, sh = 90, dw = 1280, dh = 720;
		std::vector<uint8_t> src((size_t)sw * sh * 4), dst((size_t)dw * dh * 4, 7);
		for (int i = 0; i < sw * sh; i++) {
			src[i * 4 + 0] = (uint8_t)(i & 0xFF);
			src[i * 4 + 1] = (uint8_t)((i >> 8) & 0xFF);
			src[i * 4 + 2] = 0x5A;
			src[i * 4 + 3] = 0xFF;
		}
		scaleRgba(&src[0], sw, sh, &dst[0], dw, dh);
		int untouched = 0, wrongAlpha = 0;
		for (size_t i = 0; i < (size_t)dw * dh; i++) {
			if (dst[i * 4 + 2] != 0x5A) untouched++;
			if (dst[i * 4 + 3] != 0xFF) wrongAlpha++;
		}
		// Corners must map to corners, or the picture is offset.
		bool corners = dst[0] == src[0]
		            && dst[((size_t)(dh - 1) * dw + (dw - 1)) * 4]
		               == src[((size_t)(sh - 1) * sw + (sw - 1)) * 4];
		char detail[128];
		snprintf(detail, sizeof detail, "%d unwritten pixels, %d wrong alpha, corners %s",
		         untouched, wrongAlpha, corners ? "ok" : "off");
		check("the scaler fills every destination pixel", 
		      untouched == 0 && wrongAlpha == 0 && corners, detail);
	}

	// --- matching sizes are a pass-through, byte for byte --------------------
	// The common case: a 160x90 source into a 160x90 stream must not be
	// resampled at all. It is also the only case where an off-by-one in the
	// source index is visible -- at an 8x upscale a one-pixel shift lands on
	// the same source pixel and nothing downstream can tell.
	{
		const int w = 160, h = 90;
		std::vector<uint8_t> src((size_t)w * h * 4), dst((size_t)w * h * 4, 0);
		for (size_t i = 0; i < src.size(); i++)
			src[i] = (uint8_t)((i * 7 + (i >> 5)) & 0xFF);
		scaleRgba(&src[0], w, h, &dst[0], w, h);
		int diff = 0;
		for (size_t i = 0; i < src.size(); i++)
			if (src[i] != dst[i]) diff++;
		char detail[96];
		snprintf(detail, sizeof detail, "%d bytes differ", diff);
		check("a same-size frame passes through untouched", diff == 0, detail);
	}

	// --- the bus hands over exactly what was published -----------------------
	{
		videobus::Bus b;
		b.add(42, "Repossession 1");
		std::vector<uint8_t> f((size_t)8 * 4 * 4);
		for (size_t i = 0; i < f.size(); i++) f[i] = (uint8_t)(i * 3 + 1);
		b.publish(42, 8, 4, &f[0]);

		videobus::Frame out;
		bool got = b.latest(42, 0, out);
		bool same = got && out.w == 8 && out.h == 4
		         && out.rgba.size() == f.size()
		         && std::memcmp(&out.rgba[0], &f[0], f.size()) == 0;
		check("a published frame arrives byte for byte", same);
	}

	// --- seq is what stops a sink re-sending a stale frame -------------------
	{
		videobus::Bus b;
		b.add(1, "src");
		std::vector<uint8_t> f(4 * 4, 9);
		b.publish(1, 2, 2, &f[0]);
		videobus::Frame out;
		bool first = b.latest(1, 0, out);
		uint64_t seen = out.seq;
		bool againIsRefused = !b.latest(1, seen, out);   // nothing new
		b.publish(1, 2, 2, &f[0]);
		bool newOne = b.latest(1, seen, out);
		check("a sink only wakes for a frame it has not seen",
		      first && againIsRefused && newOne);
	}

	// --- an unregistered or departed source reads as nothing -----------------
	// A sink has to survive its source being deleted between any two frames,
	// and publishing to an id nobody registered must not invent one -- a frame
	// with no name could be selected and never described.
	{
		videobus::Bus b;
		videobus::Frame out;
		std::vector<uint8_t> f(4 * 4, 1);
		b.publish(99, 2, 2, &f[0]);                  // never added
		bool ghost = !b.latest(99, 0, out);
		b.add(7, "gone soon");
		b.publish(7, 2, 2, &f[0]);
		b.remove(7);
		bool departed = !b.latest(7, 0, out);
		bool listed = b.list().empty();
		check("an unknown or removed source reads as nothing",
		      ghost && departed && listed);
	}

	// --- a source that changes size is followed --------------------------------
	{
		videobus::Bus b;
		b.add(3, "resizer");
		std::vector<uint8_t> small((size_t)4 * 4 * 4, 2);
		std::vector<uint8_t> big((size_t)64 * 32 * 4, 3);
		b.publish(3, 4, 4, &small[0]);
		videobus::Frame out;
		b.latest(3, 0, out);
		bool wasSmall = out.w == 4 && out.rgba.size() == small.size();
		b.publish(3, 64, 32, &big[0]);
		b.latest(3, out.seq, out);
		bool nowBig = out.w == 64 && out.h == 32 && out.rgba.size() == big.size();
		check("a source that resizes is followed", wasSmall && nowBig);
	}

	printf("%s  %d checks, %d failures\n", failures ? "FAILED" : "ok",
	       checks, failures);
	return failures ? 1 : 0;
}
