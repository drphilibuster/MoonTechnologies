#include "JobRunner.hpp"

using namespace rack;

namespace ps {

JobRunner& JobRunner::global() {
	static JobRunner instance;
	return instance;
}

void JobRunner::submit(Lane lane, std::function<void()> fn) {
	if (stopping())
		return;
	LaneState& L = lanes[(int) lane];
	std::unique_lock<std::mutex> lock(L.m);
	L.q.push_back(std::move(fn));
	if (!L.started) {
		L.started = true;
		L.thread = std::thread(&JobRunner::run, this, (int) lane);
	}
	lock.unlock();
	L.cv.notify_one();
}

void JobRunner::run(int lane) {
	system::setThreadName(lane == (int) Lane::Api ? "PatchAudit API" : "PatchAudit DL");
	LaneState& L = lanes[lane];
	while (true) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(L.m);
			L.cv.wait(lock, [&] { return stopping() || !L.q.empty(); });
			if (stopping())
				return;
			job = std::move(L.q.front());
			L.q.pop_front();
		}

		try {
			job();
		}
		catch (Exception& e) {
			WARN("PatchAudit job threw: %s", e.what());
		}
		catch (std::exception& e) {
			WARN("PatchAudit job threw: %s", e.what());
		}
	}
}

bool JobRunner::interruptibleSleep(double seconds) {
	double deadline = system::getTime() + seconds;
	while (!stopping()) {
		double left = deadline - system::getTime();
		if (left <= 0.0)
			return true;
		std::this_thread::sleep_for(std::chrono::duration<double>(std::min(left, 0.05)));
	}
	return false;
}

void JobRunner::shutdown() {
	if (stop.exchange(true))
		return;
	for (int i = 0; i < (int) Lane::NUM_LANES; i++) {
		LaneState& L = lanes[i];
		{
			std::lock_guard<std::mutex> lock(L.m);
			L.q.clear();
		}
		L.cv.notify_all();
	}
	// A worker blocked inside network::requestDownload can't be interrupted, so
	// this join can take as long as the transfer. Bounded in practice by curl's
	// 30 s connect timeout and by never having more than one bulk job in flight;
	// log it if it ever bites.
	for (int i = 0; i < (int) Lane::NUM_LANES; i++) {
		LaneState& L = lanes[i];
		if (!L.thread.joinable())
			continue;
		double t0 = system::getTime();
		L.thread.join();
		double dt = system::getTime() - t0;
		if (dt > 2.0)
			WARN("PatchAudit: lane %d took %.1f s to join (request still in flight?)", i, dt);
	}
}

} // namespace ps
