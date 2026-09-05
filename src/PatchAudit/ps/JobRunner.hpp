#pragma once
#include <rack.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace ps {

/** Two single-threaded worker lanes, joined at plugin unload.

Rack calls a plugin's `extern "C" void destroy()` before dlclose()
(Rack/src/plugin.cpp:317-333), which is what makes joinable threads safe here:
no code from this dylib can still be running when the image is unmapped. That is
why nothing in this plugin ever calls std::thread::detach().

One thread per lane, deliberately:
  Api  - every request to patchstorage.com and api.vcvrack.com. Serialising them
         through one thread is half the rate limiting; the other half is the
         250 ms floor between outbound requests, which lives in ps/Api.cpp
         because only that function knows whether a job will hit the network or
         be served from disk.
  Bulk - .vcv downloads, so a slow multi-megabyte transfer can't stall
         search-as-you-type.
*/
enum class Lane {
	Api = 0,
	Bulk = 1,
	NUM_LANES
};

struct JobRunner {
	static JobRunner& global();

	/** Queues a job. Threads are spawned on first use. No-op once stopping. */
	void submit(Lane lane, std::function<void()> fn);
	/** Drains queues, wakes and joins both threads. Called from destroy(). */
	void shutdown();
	bool stopping() const { return stop.load(std::memory_order_relaxed); }

	/** Sleeps in short slices so shutdown doesn't have to wait out the whole
	    interval. Returns false if we were asked to stop partway through. */
	bool interruptibleSleep(double seconds);

private:
	JobRunner() {}
	~JobRunner() { shutdown(); }

	struct LaneState {
		std::mutex m;
		std::condition_variable cv;
		std::deque<std::function<void()>> q;
		std::thread thread;
		bool started = false;
	};

	void run(int lane);

	LaneState lanes[(int) Lane::NUM_LANES];
	std::atomic<bool> stop{false};
};

} // namespace ps
