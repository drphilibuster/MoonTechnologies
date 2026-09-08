#pragma once
/** Seizing the asset: fetch, decode, and the store the module plays out of.

One worker thread does the whole pipeline, because every step of it is either a
child process or a multi-megabyte file read and none of it may happen on the
audio thread or the UI thread:

    yt-dlp   the URL          -> a container in the cache
    ffmpeg   the container    -> <id>-<rate>.pcm    stereo float32, engine rate
    ffmpeg   the container    -> <id>.rgba          160x90 RGBA at 12 fps
    read     the .pcm         -> Media::pcm, plus the decimated peaks

Progress is published as an atomic phase plus a short message behind a mutex the
UI thread takes once a frame; the finished Media arrives as a shared_ptr the UI
thread swaps in. The worker never sees the Module, the widget, or anything Rack
owns -- only the plain Request it was handed by value.

Nothing here is cancel-proof by luck: `cancel` is passed down into rp::run(),
which kills the child, so the destructor's join() returns in milliseconds even
in the middle of a download.

The .pcm carries the engine sample rate in its name. A rate change therefore
misses the cache and re-decodes rather than silently playing back at the wrong
speed -- and the player still divides by the file's rate, so a file decoded at
another rate stays correct until the re-decode lands. */
#include "../Process.hpp"

#include <rack.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rp {


// --- the video cache -------------------------------------------------------
// Frames are cached as concatenated JPEGs rather than raw RGBA, and the reason
// is arithmetic. Raw is 0.69 MB/s at the thumbnail size this module started
// with, which is nothing -- but the same format at 720p30 is 111 MB/s, or about
// 20 GB for a three-minute clip, so raw simply cannot be made bigger. MJPEG at
// 720p is roughly 70 KB a frame: a couple of hundred megabytes for that same
// clip, a hundred-odd times smaller, and still every frame independently
// seekable, which is the property that actually matters here. Regions jump
// around on clock edges, and a format that had to decode forward from a
// keyframe would hitch on every step.
//
// Decoding costs nothing extra to ship: stb_image is already inside libRack
// (nanovg uses it) and its header is in the SDK, so the JPEG decoder is linked
// whether or not this module exists.

static const int FRAME_FPS = 12;        // the thumbnail preset's rate

/** What the cache is decoded at. THUMB is what this module shipped with and is
    still the right answer for a panel that is 56 mm wide; the other two exist
    because the frames now leave the module through Transmittal and end up on a
    projector, where 160x90 is not a picture. */
struct VideoPreset { int w, h, fps; const char* name; };
static const VideoPreset kVideoPresets[] = {
	{  160,  90, 12, "160x90, 12 fps (panel only)" },
	{  640, 360, 24, "640x360, 24 fps"             },
	{ 1280, 720, 24, "1280x720, 24 fps"            },
};
static const int kNumVideoPresets = 3;
static const int kDefaultVideoPreset = 0;

// The legacy raw thumbnail, still read when a cache predates the JPEG format so
// that a saved patch does not go blank on upgrade. Nothing writes it any more.
static const int FRAME_W = 160;
static const int FRAME_H = 90;
static const size_t FRAME_BYTES = (size_t) FRAME_W * (size_t) FRAME_H * 4;

/** Buckets in the decimated waveform drawn on the timeline. One per ~0.15 mm of
    strip at 34 HP, which is finer than the panel can show at any Rack zoom. */
static const int PEAK_BUCKETS = 1024;

static const int NUM_SLOTS = 8;


/** What the module plays. Immutable once published: the audio thread reads it
    through a raw pointer, so nothing may resize or rewrite it afterwards. */
struct Media {
	std::string id;             // the cache key: a yt-dlp video id, or a path hash
	std::string source;         // the URL or file path the user gave
	std::string title;
	std::string pcmPath;
	std::string rgbaPath;       // legacy raw thumbnail, if this cache predates JPEG
	std::string mjpgPath;       // concatenated JPEGs at `videoW` x `videoH`
	std::vector<uint64_t> frameOffsets;   // into mjpgPath, one per frame

	int sampleRate = 44100;
	int64_t frames = 0;         // stereo frames in the .pcm on disk
	int videoFrames = 0;
	int videoW = FRAME_W;
	int videoH = FRAME_H;
	int videoFps = FRAME_FPS;
	double duration = 0.0;      // seconds

	std::vector<float> peaks;   // PEAK_BUCKETS entries, 0..1

	/** The audio itself is NOT here. It stays in the .pcm at `pcmPath` and only
	    the windows the module actually plays are read into RAM -- see
	    Windows.hpp. What a Media holds is everything needed to *find* audio: the
	    length, the rate, and enough of a waveform to draw. That is about four
	    kilobytes for a clip of any length, where holding the audio was 230 MB
	    for ten minutes. */
	bool playable() const { return frames > 1 && !pcmPath.empty(); }
};


enum Phase {
	PHASE_IDLE = 0,
	PHASE_FETCHING,
	PHASE_AUDIO,
	PHASE_VIDEO,
	PHASE_READING,
	PHASE_READY,
	PHASE_FAILED,
	PHASE_CANCELLED
};


/** Everything the worker needs, by value. Deliberately holds no pointer into
    anything Rack owns. */
struct Request {
	std::string source;
	bool isPath = false;
	std::string id;             // a known cache id, so a reopen skips the network
	int sampleRate = 44100;
	double maxSeconds = 180.0;
	std::string toolDir;        // the user's extra tools directory, may be empty
	int videoPreset = kDefaultVideoPreset;   // index into kVideoPresets
};


/** The cache: ~/.../Rack2/MoonTechnologies/Repossession/. Containers, .pcm and
    .rgba all live here, keyed by id, and are reused across sessions. */
inline std::string cacheDir() {
	return rack::asset::user("MoonTechnologies/Repossession/");
}

/** FNV-1a, as hex. Only used to key a local file's decode products by its path,
    so it needs to be stable and short, not cryptographic. */
inline std::string pathKey(const std::string& s) {
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < s.size(); i++) {
		h ^= (uint64_t) (unsigned char) s[i];
		h *= 1099511628211ull;
	}
	char buf[32];
	std::snprintf(buf, sizeof(buf), "local-%016llx", (unsigned long long) h);
	return buf;
}

inline std::string pcmPathFor(const std::string& id, int rate) {
	return cacheDir() + id + "-" + std::to_string(rate) + ".pcm";
}

inline std::string rgbaPathFor(const std::string& id) {
	return cacheDir() + id + ".rgba";
}

/** The JPEG stream for one preset, and the frame index beside it. The preset is
    in the name so switching quality is a different file rather than a rewrite,
    and switching back is free. */
inline std::string mjpgPathFor(const std::string& id, const VideoPreset& p) {
	return cacheDir() + id + rack::string::f("-%dx%d@%d.mjpg", p.w, p.h, p.fps);
}

inline std::string idxPathFor(const std::string& id, const VideoPreset& p) {
	return cacheDir() + id + rack::string::f("-%dx%d@%d.idx", p.w, p.h, p.fps);
}

/** Byte offset and length of every frame in a concatenated JPEG stream.

    Built by scanning for the start-of-image marker, which is safe to look for
    because JPEG escapes any 0xFF inside entropy-coded data as 0xFF00 -- a bare
    FF D8 can only be a frame boundary. Written out beside the stream so it is
    built once per decode rather than once per patch load. */
inline std::vector<uint64_t> buildJpegIndex(const std::string& mjpg) {
	std::vector<uint64_t> offs;
	FILE* f = std::fopen(mjpg.c_str(), "rb");
	if (!f)
		return offs;
	const size_t CHUNK = 1 << 16;
	std::vector<uint8_t> buf(CHUNK + 2);
	uint64_t base = 0;            // file offset of buf[0]
	size_t keep = 0;              // bytes carried over from the last chunk
	while (true) {
		size_t got = std::fread(&buf[keep], 1, CHUNK, f);
		if (got == 0)
			break;
		size_t total = keep + got;
		for (size_t i = 0; i + 2 < total; i++)
			if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF)
				offs.push_back(base + (uint64_t) i);
		// Carry the last two bytes, so a marker straddling the boundary is
		// still seen on the next pass rather than falling down the crack.
		size_t newKeep = (total >= 2) ? 2 : total;
		base += (uint64_t)(total - newKeep);
		std::memmove(&buf[0], &buf[total - newKeep], newKeep);
		keep = newKeep;
		if (got < CHUNK)
			break;
	}
	std::fclose(f);
	return offs;
}

inline bool writeIndex(const std::string& path, const std::vector<uint64_t>& offs) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = offs.empty()
		|| std::fwrite(&offs[0], sizeof(uint64_t), offs.size(), f) == offs.size();
	std::fclose(f);
	return ok;
}

inline std::vector<uint64_t> readIndex(const std::string& path) {
	std::vector<uint64_t> offs;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return offs;
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (n > 0) {
		offs.resize((size_t)n / sizeof(uint64_t));
		if (!offs.empty()
		    && std::fread(&offs[0], sizeof(uint64_t), offs.size(), f) != offs.size())
			offs.clear();
	}
	std::fclose(f);
	return offs;
}

inline std::string trimmed(const std::string& s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

inline std::vector<std::string> splitLines(const std::string& s) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start <= s.size()) {
		size_t end = s.find('\n', start);
		if (end == std::string::npos)
			end = s.size();
		std::string line = trimmed(s.substr(start, end - start));
		if (!line.empty())
			out.push_back(line);
		if (end == s.size())
			break;
		start = end + 1;
	}
	return out;
}

/** The container yt-dlp left in the cache for `id`, if it is still there. Found
    by stem rather than remembered, because the extension depends on the format
    the site served and we would rather not persist it. */
inline std::string findContainer(const std::string& id) {
	std::vector<std::string> entries;
	try {
		entries = rack::system::getEntries(cacheDir(), 1);
	}
	catch (...) {
		return "";
	}
	for (size_t i = 0; i < entries.size(); i++) {
		const std::string& p = entries[i];
		if (rack::system::getStem(p) != id)
			continue;
		std::string ext = rack::system::getExtension(p);
		if (ext == ".pcm" || ext == ".rgba")
			continue;
		if (rack::system::isFile(p))
			return p;
	}
	return "";
}


/** The worker, and the mailbox the UI thread reads it through. */
struct Loader {
	std::atomic<int> phase;
	std::atomic<float> progress;
	std::atomic<bool> cancel;
	/** Raised whenever `message` or `result` changed, so the UI can poll one
	    atomic instead of taking the mutex every frame. */
	std::atomic<bool> dirty;

	Loader() {
		phase.store(PHASE_IDLE);
		progress.store(0.f);
		cancel.store(false);
		dirty.store(false);
	}

	~Loader() { stop(); }

	/** UI thread. Cancels and joins any job in flight, then starts this one. */
	void start(const Request& req) {
		stop();
		cancel.store(false);
		phase.store(PHASE_FETCHING);
		progress.store(0.02f);
		setMessage("Filing notice...");
		Request copy = req;
		thread = std::thread(&Loader::work, this, copy);
	}

	/** UI thread. Safe to call when nothing is running. */
	void stop() {
		cancel.store(true);
		if (thread.joinable())
			thread.join();
		cancel.store(false);
	}

	bool busy() const {
		int p = phase.load();
		return p > PHASE_IDLE && p < PHASE_READY;
	}

	std::string message() {
		std::lock_guard<std::mutex> lock(mutex);
		return note;
	}

	/** UI thread. Hands over the finished Media exactly once. */
	std::shared_ptr<Media> take() {
		std::lock_guard<std::mutex> lock(mutex);
		std::shared_ptr<Media> m = result;
		result.reset();
		return m;
	}

private:
	std::mutex mutex;
	std::string note;
	std::shared_ptr<Media> result;
	std::thread thread;

	void setMessage(const std::string& s) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			note = s;
		}
		dirty.store(true);
	}

	void fail(const std::string& s) {
		setMessage(s);
		phase.store(PHASE_FAILED);
	}

	bool stopping() const { return cancel.load(std::memory_order_relaxed); }

	/** One line of a tool's stderr, for the panel. The whole thing is far too
	    long to read on a 100 mm strip, and the last line is the complaint. */
	static std::string lastLine(const std::string& s) {
		std::vector<std::string> lines = splitLines(s);
		if (lines.empty())
			return "";
		std::string l = lines.back();
		if (l.size() > 96)
			l = l.substr(0, 93) + "...";
		return l;
	}

	void work(Request req) {
		rack::system::setThreadName("Repossession");

		std::string cache = cacheDir();
		rack::system::createDirectories(cache);

		std::vector<std::string> dirs = defaultToolDirs();
		std::string globalDir = loadGlobalToolDir();
		if (!globalDir.empty())
			dirs.insert(dirs.begin(), globalDir);
		if (!req.toolDir.empty())
			dirs.insert(dirs.begin(), req.toolDir);
		std::string ffmpeg = which("ffmpeg", dirs);

		if (ffmpeg.empty()) {
			WARN("Repossession: ffmpeg not found; looked in %s", whereLooked(dirs).c_str());
			fail("ffmpeg not found. Install it (brew, apt, winget, scoop...), drop it "
			     "in Rack's MoonTechnologies/tools folder, or set the tools folder "
			     "in the menu. Rack's log lists everywhere it looked.");
			return;
		}

		std::string id = req.id;
		std::string title = req.source;
		std::string container;

		if (req.isPath) {
			if (!rack::system::isFile(req.source)) {
				fail("No such file: " + req.source);
				return;
			}
			container = req.source;
			id = pathKey(rack::system::getAbsolute(req.source));
			title = rack::system::getFilename(req.source);
		}

		const VideoPreset& preset = kVideoPresets[
			(req.videoPreset >= 0 && req.videoPreset < kNumVideoPresets)
				? req.videoPreset : kDefaultVideoPreset];

		std::string pcmPath = id.empty() ? "" : pcmPathFor(id, req.sampleRate);
		std::string rgbaPath = id.empty() ? "" : rgbaPathFor(id);
		std::string mjpgPath = id.empty() ? "" : mjpgPathFor(id, preset);
		std::string idxPath = id.empty() ? "" : idxPathFor(id, preset);

		bool haveAudio = !pcmPath.empty() && rack::system::isFile(pcmPath)
			&& rack::system::getFileSize(pcmPath) > 0;
		bool haveJpeg = !mjpgPath.empty() && rack::system::isFile(mjpgPath)
			&& rack::system::getFileSize(mjpgPath) > 0
			&& rack::system::isFile(idxPath);
		// A cache written before the JPEG format is still a usable thumbnail,
		// so a patch saved then does not go blank on upgrade -- but only for
		// the preset it was actually decoded at.
		bool haveLegacyRaw = req.videoPreset == kDefaultVideoPreset
			&& !rgbaPath.empty() && rack::system::isFile(rgbaPath)
			&& rack::system::getFileSize(rgbaPath) >= FRAME_BYTES;
		bool haveVideo = haveJpeg || haveLegacyRaw;

		// The container only has to exist if something still needs decoding.
		if (!(haveAudio && haveVideo) && container.empty() && !id.empty())
			container = findContainer(id);

		if (!(haveAudio && haveVideo) && container.empty()) {
			// Nothing cached: go and get it.
			std::string ytdlp = which("yt-dlp", dirs);
			if (ytdlp.empty()) {
				WARN("Repossession: yt-dlp not found; looked in %s", whereLooked(dirs).c_str());
				fail("yt-dlp not found. Install it (brew, pipx, winget, scoop...), drop it "
				     "in Rack's MoonTechnologies/tools folder, or set the tools folder "
				     "in the menu. Rack's log lists everywhere it looked.");
				return;
			}
			phase.store(PHASE_FETCHING);
			progress.store(0.05f);
			setMessage("Seizing " + req.source);

			// yt-dlp merges the video and audio streams by running ffmpeg
			// itself, and it looks for ffmpeg on PATH -- which, inside a GUI
			// launched Rack, does not include Homebrew. Hand it the one we found.
			INFO("Repossession: yt-dlp at %s, ffmpeg at %s", ytdlp.c_str(), ffmpeg.c_str());
			std::vector<std::string> argv;
			argv.push_back(ytdlp);
			argv.push_back("--ffmpeg-location");
			argv.push_back(rack::system::getDirectory(ffmpeg));
			argv.push_back("-f");
			// The download cap follows the quality setting. It used to be a
			// flat 360p, which was right when the only consumer was a 56 mm
			// screen and wrong the moment these frames started going to a
			// projector -- asking for 720p output from a 360p download is not a
			// bigger picture, it is the same picture upscaled.
			//
			// One container per id, at whatever cap was in force when it was
			// fetched, and it is reused rather than re-fetched afterwards. So
			// raising the quality on something already imported rescales what
			// is there; delete the container from the cache folder (or import
			// the link fresh) to actually go and get more pixels. That is the
			// honest trade against re-downloading a hundred megabytes every
			// time somebody opens a submenu.
			int cap = (preset.h > 360) ? 720 : 360;
			argv.push_back(rack::string::f(
				"bv*[height<=%d]+ba/b[height<=%d]", cap, cap));
			argv.push_back("--no-playlist");
			argv.push_back("--no-warnings");
			argv.push_back("-o");
			argv.push_back(cache + "%(id)s.%(ext)s");
			argv.push_back("--print");
			argv.push_back("after_move:filepath");
			argv.push_back("--print");
			argv.push_back("after_move:title");
			argv.push_back(req.source);

			ProcessResult r = run(argv, &cancel);
			if (stopping()) {
				phase.store(PHASE_CANCELLED);
				return;
			}
			if (!r.ok()) {
				if (!r.execError.empty()) {
					WARN("Repossession: %s", r.execError.c_str());
					fail(r.execError);
					return;
				}
				std::string why = lastLine(r.err);
				fail(why.empty() ? "yt-dlp failed (exit " + std::to_string(r.status) + ")"
				                 : why);
				return;
			}
			std::vector<std::string> lines = splitLines(r.out);
			if (lines.empty()) {
				fail("yt-dlp produced no file.");
				return;
			}
			container = lines[0];
			if (lines.size() > 1)
				title = lines[1];
			if (!rack::system::isFile(container)) {
				fail("yt-dlp reported a file that is not there.");
				return;
			}
			id = rack::system::getStem(container);
			pcmPath = pcmPathFor(id, req.sampleRate);
			rgbaPath = rgbaPathFor(id);
			mjpgPath = mjpgPathFor(id, preset);
			idxPath = idxPathFor(id, preset);
			haveAudio = rack::system::isFile(pcmPath)
				&& rack::system::getFileSize(pcmPath) > 0;
			haveJpeg = rack::system::isFile(mjpgPath)
				&& rack::system::getFileSize(mjpgPath) > 0
				&& rack::system::isFile(idxPath);
			haveLegacyRaw = req.videoPreset == kDefaultVideoPreset
				&& rack::system::isFile(rgbaPath)
				&& rack::system::getFileSize(rgbaPath) >= FRAME_BYTES;
			haveVideo = haveJpeg || haveLegacyRaw;
		}

		if (id.empty()) {
			fail("Nothing to open.");
			return;
		}
		if (stopping()) {
			phase.store(PHASE_CANCELLED);
			return;
		}

		std::string secs = std::to_string((int) (req.maxSeconds + 0.5));

		if (!haveAudio) {
			phase.store(PHASE_AUDIO);
			progress.store(0.45f);
			setMessage("Assessing audio...");
			std::vector<std::string> a;
			a.push_back(ffmpeg);
			a.push_back("-v");
			a.push_back("error");
			a.push_back("-nostdin");
			a.push_back("-y");
			a.push_back("-i");
			a.push_back(container);
			a.push_back("-t");
			a.push_back(secs);
			a.push_back("-vn");
			a.push_back("-ac");
			a.push_back("2");
			a.push_back("-ar");
			a.push_back(std::to_string(req.sampleRate));
			a.push_back("-f");
			a.push_back("f32le");
			a.push_back(pcmPath);
			ProcessResult r = run(a, &cancel);
			if (stopping()) {
				phase.store(PHASE_CANCELLED);
				return;
			}
			if (!r.ok() || rack::system::getFileSize(pcmPath) == 0) {
				if (!r.execError.empty()) {
					WARN("Repossession: %s", r.execError.c_str());
					fail(r.execError);
					return;
				}
				std::string why = lastLine(r.err);
				fail(why.empty() ? "ffmpeg could not decode the audio." : why);
				return;
			}
		}

		if (!haveVideo) {
			phase.store(PHASE_VIDEO);
			progress.store(0.75f);
			setMessage("Photographing the property...");
			std::vector<std::string> v;
			v.push_back(ffmpeg);
			v.push_back("-v");
			v.push_back("error");
			v.push_back("-nostdin");
			v.push_back("-y");
			v.push_back("-i");
			v.push_back(container);
			v.push_back("-t");
			v.push_back(secs);
			v.push_back("-an");
			v.push_back("-vf");
			// Scale to fit inside the preset without distorting, and pad the
			// rest -- a 4:3 source in a 16:9 frame should get bars, not be
			// stretched. The even-number rounding is for the JPEG encoder's
			// chroma subsampling, which cannot take an odd dimension.
			v.push_back(rack::string::f(
				"fps=%d,scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2,"
				"pad=%d:%d:(ow-iw)/2:(oh-ih)/2",
				preset.fps, preset.w, preset.h, preset.w, preset.h));
			v.push_back("-c:v");
			v.push_back("mjpeg");
			// 3 is visually clean at these sizes; the whole point of the format
			// is that it is a hundred-odd times smaller than raw, so there is no
			// reason to be stingy about the last few percent.
			v.push_back("-q:v");
			v.push_back("3");
			v.push_back("-pix_fmt");
			v.push_back("yuvj420p");
			v.push_back("-f");
			v.push_back("mjpeg");
			v.push_back(mjpgPath);
			ProcessResult r = run(v, &cancel);
			if (stopping()) {
				phase.store(PHASE_CANCELLED);
				return;
			}
			// A soundtrack-only source is still worth having, so a missing video
			// stream is not fatal -- the screen simply says so. An ffmpeg that
			// could not be launched at all is a different matter: the audio pass
			// already proved it can run, so this is worth a line in the log
			// rather than a silent blank screen.
			if (!r.ok()) {
				if (!r.execError.empty())
					WARN("Repossession: %s", r.execError.c_str());
				rack::system::remove(mjpgPath);
			}
			else {
				// Index once here rather than on every patch load: scanning a
				// couple of hundred megabytes for frame boundaries is cheap but
				// it is not free, and the answer never changes.
				std::vector<uint64_t> offs = buildJpegIndex(mjpgPath);
				if (offs.empty() || !writeIndex(idxPath, offs)) {
					WARN("Repossession: could not index %s", mjpgPath.c_str());
					rack::system::remove(mjpgPath);
					rack::system::remove(idxPath);
				}
			}
		}

		if (stopping()) {
			phase.store(PHASE_CANCELLED);
			return;
		}

		phase.store(PHASE_READING);
		progress.store(0.92f);
		setMessage("Reading the schedule...");

		std::shared_ptr<Media> m(new Media);
		m->id = id;
		m->source = req.source;
		m->title = title;
		m->pcmPath = pcmPath;
		m->rgbaPath = rgbaPath;
		m->mjpgPath = mjpgPath;
		m->sampleRate = req.sampleRate;
		m->videoW = preset.w;
		m->videoH = preset.h;
		m->videoFps = preset.fps;

		if (!scanPcm(*m)) {
			if (stopping()) {
				phase.store(PHASE_CANCELLED);
				return;
			}
			fail("Could not read the decoded audio.");
			return;
		}
		if (rack::system::isFile(mjpgPath) && rack::system::isFile(idxPath)) {
			m->frameOffsets = readIndex(idxPath);
			// An index that went missing or was truncated is recoverable: the
			// stream itself is the source of truth, so rebuild rather than
			// leave the module with a video it cannot address.
			if (m->frameOffsets.empty()) {
				m->frameOffsets = buildJpegIndex(mjpgPath);
				if (!m->frameOffsets.empty())
					writeIndex(idxPath, m->frameOffsets);
			}
			m->videoFrames = (int) m->frameOffsets.size();
		}
		else if (rack::system::isFile(rgbaPath)) {
			// The legacy raw thumbnail. No index: every frame is the same size,
			// so the offset is arithmetic.
			uint64_t sz = rack::system::getFileSize(rgbaPath);
			m->videoFrames = (int) (sz / FRAME_BYTES);
			m->mjpgPath.clear();
			m->videoW = FRAME_W;
			m->videoH = FRAME_H;
			m->videoFps = FRAME_FPS;
		}

		if (stopping()) {
			phase.store(PHASE_CANCELLED);
			return;
		}
		{
			std::lock_guard<std::mutex> lock(mutex);
			result = m;
			note = rack::string::f("%s  |  %.1f s  |  %d frames",
				m->title.c_str(), m->duration, m->videoFrames);
		}
		progress.store(1.f);
		phase.store(PHASE_READY);
		dirty.store(true);
	}

	/** Measures the .pcm and draws its waveform, without ever holding it.

	    The file is read once, forwards, in a fixed buffer, and every frame is
	    folded into the peak bucket it belongs to. Memory is the buffer -- half a
	    megabyte, whatever the clip -- and the result is the four kilobytes of
	    peaks the timeline draws from. The audio itself is left where it is; the
	    module reads windows out of it as it needs them. */
	bool scanPcm(Media& m) {
		uint64_t bytes = rack::system::getFileSize(m.pcmPath);
		int64_t frames = (int64_t) (bytes / (uint64_t) (2 * sizeof(float)));
		if (frames < 2)
			return false;
		m.frames = frames;
		m.duration = (double) frames / (double) m.sampleRate;
		m.peaks.assign(PEAK_BUCKETS, 0.f);

		FILE* f = std::fopen(m.pcmPath.c_str(), "rb");
		if (!f)
			return false;

		static const size_t CHUNK_FRAMES = 65536;
		std::vector<float> buf(CHUNK_FRAMES * 2);
		int64_t at = 0;
		while (at < frames) {
			if (stopping()) {
				std::fclose(f);
				return false;
			}
			size_t want = (size_t) std::min<int64_t>(
				(int64_t) CHUNK_FRAMES, frames - at);
			size_t got = std::fread(&buf[0], sizeof(float), want * 2, f);
			got -= got % 2;
			if (got == 0)
				break;
			size_t n = got / 2;
			for (size_t i = 0; i < n; i++) {
				int b = (int) (((at + (int64_t) i) * (int64_t) PEAK_BUCKETS)
					/ frames);
				if (b < 0)
					b = 0;
				if (b >= PEAK_BUCKETS)
					b = PEAK_BUCKETS - 1;
				float l = std::fabs(buf[i * 2]);
				float r = std::fabs(buf[i * 2 + 1]);
				float v = (l > r) ? l : r;
				if (v > m.peaks[(size_t) b])
					m.peaks[(size_t) b] = (v > 1.f) ? 1.f : v;
			}
			at += (int64_t) n;
			// A long clip is a visible amount of reading; the panel should not
			// sit at 92% through all of it.
			progress.store(0.92f + 0.07f * (float) at / (float) frames);
		}
		std::fclose(f);
		return m.frames > 1;
	}
};


} // namespace rp
