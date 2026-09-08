#pragma once
#include <stdint.h>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The video bus: how one module in this plugin hands frames to another.
//
// Rack's cables carry a float per sample, which is the wrong shape for a frame
// by several orders of magnitude, and there is no video cable to add -- the
// port type is Rack's, not ours. Expander messages would work but only between
// neighbours, and a video sink has no business being bolted to the side of its
// source. So sources publish here by module id and Transmittal reads by id,
// which lets the two sit anywhere in the rack.
//
// Frames are CPU-side RGBA rather than GL textures. That is deliberate for the
// first backend: ffmpeg wants bytes on a pipe, and a texture would have to be
// read back off the GPU to get them. A later Syphon/Spout backend wants the
// opposite -- a texture, never read back -- and will publish alongside this
// rather than through it, because converting either way is the one thing that
// would make both slow.
//
// Everything here is under one mutex. Publishing happens on whatever thread the
// source draws on and reading happens on the encoder's worker, so the copy is
// not optional; the frames are small enough (a 720p RGBA frame is 3.7 MB) that
// one memcpy per published frame is cheaper than any scheme for avoiding it.

namespace videobus {

/** What a source last published, and how to tell it apart from the frame
    before it. `seq` is the whole synchronisation story: a consumer keeps the
    last one it saw and does nothing until the number changes, so a source that
    publishes slower than the encoder pulls costs nothing but a comparison. */
struct Frame {
	int w = 0;
	int h = 0;
	uint64_t seq = 0;
	std::vector<uint8_t> rgba;
};

struct Bus {
	std::mutex mu;
	std::map<int64_t, Frame> frames;
	std::map<int64_t, std::string> names;

	/** Announce a source, so a sink can offer it by name before it has ever
	    published a frame. Re-registering under the same id renames it. */
	void add(int64_t id, const std::string& name) {
		std::lock_guard<std::mutex> lock(mu);
		names[id] = name;
		frames[id];                       // default-construct if new
	}

	/** A source going away. A sink reading it must cope with this happening
	    between any two of its own frames. */
	void remove(int64_t id) {
		std::lock_guard<std::mutex> lock(mu);
		names.erase(id);
		frames.erase(id);
	}

	/** `data` is w*h*4 bytes of RGBA. Ignored for an unregistered id rather
	    than registering one implicitly: a frame with no name cannot be offered
	    in a menu, and a sink that could select it could never say what it was. */
	void publish(int64_t id, int w, int h, const uint8_t* data) {
		if (w <= 0 || h <= 0 || !data)
			return;
		std::lock_guard<std::mutex> lock(mu);
		std::map<int64_t, Frame>::iterator it = frames.find(id);
		if (it == frames.end())
			return;
		Frame& f = it->second;
		size_t n = (size_t)w * (size_t)h * 4u;
		if (f.rgba.size() != n)
			f.rgba.resize(n);
		std::memcpy(&f.rgba[0], data, n);
		f.w = w;
		f.h = h;
		f.seq++;
	}

	/** Copy out the latest frame if `seq` has moved since `since`. Returns
	    false when the source is gone or has published nothing new, and leaves
	    `out` untouched -- so a sink keeps showing the last good frame rather
	    than a black one, which is what a stalled source should look like. */
	bool latest(int64_t id, uint64_t since, Frame& out) {
		std::lock_guard<std::mutex> lock(mu);
		std::map<int64_t, Frame>::iterator it = frames.find(id);
		if (it == frames.end())
			return false;
		Frame& f = it->second;
		if (f.seq == 0 || f.seq == since)
			return false;
		out = f;
		return true;
	}

	/** Every registered source, id and name, for a sink's menu. */
	std::vector<std::pair<int64_t, std::string> > list() {
		std::lock_guard<std::mutex> lock(mu);
		std::vector<std::pair<int64_t, std::string> > v;
		for (std::map<int64_t, std::string>::iterator it = names.begin();
		     it != names.end(); ++it)
			v.push_back(*it);
		return v;
	}
};

/** One bus for the plugin. A function-local static so it is constructed on
    first use and destroyed after every module that could touch it. */
inline Bus& bus() {
	static Bus b;
	return b;
}

} // namespace videobus
