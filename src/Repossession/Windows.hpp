#pragma once
/** The windows: what is actually in RAM, and the budget that bounds it.

Repossession used to hold the whole decoded clip in memory -- ten minutes of
stereo float at 48 kHz is about 230 MB, which is why the import length was a
menu item rather than a number. But the module never plays the whole clip. It
plays eight windows cut out of it, and everything between them is memory spent
on audio nobody asked for.

So the decode still lands on disk as a .pcm, exactly as before, and only the
windows are read into RAM:

    <cache>/<id>-<rate>.pcm       the whole clip, on disk, as it always was
    Window[slot]                  one span of it, in RAM, stereo float

Reading a window is a seek and a read of a few megabytes, which is why the
disk-backed design was worth keeping: dragging a region's edge re-reads it in
milliseconds rather than re-running ffmpeg, so editing stays immediate.

Three rules this file exists to enforce:

  * **The audio thread never touches a file.** A window crosses to it as a plain
    atomic pointer, and the previous one is freed only once process() has
    demonstrably moved past the swap -- the same handover Media.hpp uses.
  * **RAM is bounded and the bound is visible.** The budget is a number the user
    sets, divided among the slots. Nothing can quietly exceed it.
  * **Giving a slot's share up is easy and taking it back may not be.** A
    disabled step returns its seconds to a common pool that the other steps can
    grow into. Re-arming it gets whatever is left, which can be nothing.

That last rule is a constraint the instrument is built on rather than an
accounting detail: eight steps sharing a fixed budget means lengthening one
costs another, and the panel says so. */
#include "Media.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rp {


/** Bytes per stereo frame of the .pcm: two float32 samples. */
static const size_t WINDOW_FRAME_BYTES = 2 * sizeof(float);

/** What the budget can be set to, in megabytes. The smallest is deliberately
    small enough to be felt -- a budget you never reach teaches nothing. */
static const int BUDGET_MB[] = {16, 32, 64, 128, 256, 512};
static const int NUM_BUDGETS = 6;


/** One slot's audio, in RAM. Immutable once published. */
struct Window {
	std::vector<float> pcm;         // stereo interleaved, as the .pcm is
	int64_t startFrame = 0;         // where in the clip pcm[0] sits
	int64_t frames = 0;             // stereo frames held
	/** The span this was cut for, so the UI can tell a stale window from a
	    current one without comparing megabytes. */
	float lo = 0.f, hi = 0.f;
	/** Which media it came from. A window outlives the clip it was cut from if
	    nobody checks, and playing one clip's audio at another's playhead is a
	    bug that sounds like a feature. */
	std::string mediaId;

	bool holds(int64_t frame) const {
		return frame >= startFrame && frame < startFrame + frames;
	}
	size_t bytes() const { return pcm.size() * sizeof(float); }
};


/** The seconds each slot is allowed, and the pool they come out of.

    Plain doubles on the UI thread: the audio thread never reads this, it only
    reads whatever window happens to be published. */
struct Budget {
	double pool = 0.0;              // total seconds the budget buys
	double grant[NUM_SLOTS];        // seconds each slot holds
	double free = 0.0;              // seconds nobody holds

	Budget() {
		for (int i = 0; i < NUM_SLOTS; i++)
			grant[i] = 0.0;
	}

	/** Seconds of stereo float at `rate` that `mb` megabytes buys. */
	static double secondsFor(int mb, int rate) {
		if (rate <= 0)
			rate = 48000;
		return (double) mb * 1024.0 * 1024.0
			/ ((double) rate * (double) WINDOW_FRAME_BYTES);
	}

	/** An even split, which is where every clip starts: eight steps, eight
	    equal shares, nothing held back. */
	void reset(double poolSeconds) {
		pool = poolSeconds;
		double each = pool / (double) NUM_SLOTS;
		for (int i = 0; i < NUM_SLOTS; i++)
			grant[i] = each;
		free = 0.0;
	}

	/** Re-scaling when the budget setting changes. Shares keep their proportions
	    rather than being thrown away, because a patch that has been shaped
	    should survive being given more room. */
	void rescale(double poolSeconds) {
		if (pool <= 0.0) {
			reset(poolSeconds);
			return;
		}
		double k = poolSeconds / pool;
		for (int i = 0; i < NUM_SLOTS; i++)
			grant[i] *= k;
		free *= k;
		pool = poolSeconds;
	}

	double held() const {
		double t = 0.0;
		for (int i = 0; i < NUM_SLOTS; i++)
			t += grant[i];
		return t;
	}

	/** Ask for `want` seconds for `slot`. Returns what it actually gets, which
	    is `want` if the pool can cover the difference and as much as it can
	    otherwise. Shrinking always succeeds and returns the difference to the
	    pool, so making room for one step is simply a matter of trimming
	    another. */
	double request(int slot, double want) {
		if (slot < 0 || slot >= NUM_SLOTS)
			return 0.0;
		if (want < 0.0)
			want = 0.0;
		double have = grant[slot];
		if (want <= have) {
			free += have - want;
			grant[slot] = want;
			return want;
		}
		double need = want - have;
		double take = (need <= free) ? need : free;
		grant[slot] = have + take;
		free -= take;
		return grant[slot];
	}

	/** A step being disabled hands its whole share back. */
	void release(int slot) {
		if (slot < 0 || slot >= NUM_SLOTS)
			return;
		free += grant[slot];
		grant[slot] = 0.0;
	}

	/** A step being re-armed takes back up to an even share -- but only out of
	    what is still there. Returns 0 when the pool is empty, which is the
	    caller's cue to refuse the arming and blink the step. */
	double reclaim(int slot) {
		if (slot < 0 || slot >= NUM_SLOTS)
			return 0.0;
		double want = pool / (double) NUM_SLOTS;
		double take = (want <= free) ? want : free;
		grant[slot] += take;
		free -= take;
		return grant[slot];
	}
};


/** The worker that reads windows off the .pcm.

    One thread, one pending request per slot with the newest winning: dragging an
    edge produces a request every frame and only the last one matters, so a queue
    would be a backlog of work nobody wants done. */
struct WindowLoader {
	struct Job {
		bool pending = false;
		std::string path;
		std::string mediaId;
		int64_t startFrame = 0;
		int64_t frames = 0;
		float lo = 0.f, hi = 0.f;
	};

	WindowLoader() {
		quit.store(false);
		thread = std::thread(&WindowLoader::run, this);
	}

	~WindowLoader() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			quit.store(true);
		}
		cv.notify_all();
		if (thread.joinable())
			thread.join();
	}

	/** UI thread. Replaces whatever was queued for this slot. */
	void request(int slot, const std::string& path, const std::string& mediaId,
	             int64_t startFrame, int64_t frames, float lo, float hi) {
		if (slot < 0 || slot >= NUM_SLOTS)
			return;
		{
			std::lock_guard<std::mutex> lock(mutex);
			Job& j = jobs[slot];
			j.pending = true;
			j.path = path;
			j.mediaId = mediaId;
			j.startFrame = startFrame;
			j.frames = frames;
			j.lo = lo;
			j.hi = hi;
		}
		cv.notify_one();
	}

	/** UI thread. Hands over whatever has finished, once each. */
	std::shared_ptr<Window> take(int slot) {
		if (slot < 0 || slot >= NUM_SLOTS)
			return std::shared_ptr<Window>();
		std::lock_guard<std::mutex> lock(mutex);
		std::shared_ptr<Window> w = done[slot];
		done[slot].reset();
		return w;
	}

	/** True while anything is queued or being read, for the panel's meter. */
	bool busy() {
		std::lock_guard<std::mutex> lock(mutex);
		for (int i = 0; i < NUM_SLOTS; i++)
			if (jobs[i].pending)
				return true;
		return working;
	}

private:
	std::mutex mutex;
	std::condition_variable cv;
	std::atomic<bool> quit;
	bool working = false;
	Job jobs[NUM_SLOTS];
	std::shared_ptr<Window> done[NUM_SLOTS];
	std::thread thread;

	void run() {
		rack::system::setThreadName("Repossession windows");
		while (true) {
			Job j;
			int slot = -1;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock, [this]() {
					if (quit.load())
						return true;
					for (int i = 0; i < NUM_SLOTS; i++)
						if (jobs[i].pending)
							return true;
					return false;
				});
				if (quit.load())
					return;
				for (int i = 0; i < NUM_SLOTS; i++) {
					if (jobs[i].pending) {
						slot = i;
						j = jobs[i];
						jobs[i].pending = false;
						break;
					}
				}
				working = true;
			}
			if (slot < 0) {
				std::lock_guard<std::mutex> lock(mutex);
				working = false;
				continue;
			}

			std::shared_ptr<Window> w = read(j);

			{
				std::lock_guard<std::mutex> lock(mutex);
				// A request that landed while this one was being read supersedes
				// it: publishing the stale one would make the last drag of an
				// edge the one that does not stick.
				if (!jobs[slot].pending)
					done[slot] = w;
				working = false;
			}
		}
	}

	static std::shared_ptr<Window> read(const Job& j) {
		std::shared_ptr<Window> w(new Window);
		w->startFrame = j.startFrame;
		w->lo = j.lo;
		w->hi = j.hi;
		w->mediaId = j.mediaId;
		if (j.frames <= 0)
			return w;

		FILE* f = std::fopen(j.path.c_str(), "rb");
		if (!f)
			return w;
		if (std::fseek(f, (long) (j.startFrame * (int64_t) WINDOW_FRAME_BYTES),
				SEEK_SET) != 0) {
			std::fclose(f);
			return w;
		}
		size_t floats = (size_t) j.frames * 2;
		w->pcm.resize(floats);
		size_t got = std::fread(&w->pcm[0], sizeof(float), floats, f);
		std::fclose(f);
		got -= got % 2;
		if (got < floats)
			w->pcm.resize(got);
		w->frames = (int64_t) (w->pcm.size() / 2);
		return w;
	}
};


} // namespace rp
