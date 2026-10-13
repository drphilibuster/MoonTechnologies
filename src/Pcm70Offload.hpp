// Pcm70Offload.hpp: run a sample-by-sample machine on a thread of its own, between two rings.
//
// Nordic Banking streams its machine ahead of the reader. An effect cannot: its output n needs input n, so the worker can only ever be as far ahead as the input has
// been pushed. What it can do is keep the audio thread from running the machine at all. The audio thread pushes one input sample and pops one output sample per
// call, and the machine runs on the worker as the input arrives.
//
// The latency is the number of samples in flight: the ring is started with `latency` zeros pushed ahead of the first real sample, so output n is the machine's answer
// to input n - latency. It has to be at least one engine block, because the engine calls the module `blockFrames` times back to back and then waits: the module has
// to be able to hand back that many outputs before the worker has seen the first of those inputs. Below that, a pop finds the ring empty (an underrun: the last
// output is held, which is a click) until the worker catches up.
//
// Audio thread: push(), pop(), setLatency(), start(), stop(). Worker: the Step callback only. Nothing on the audio thread waits on a lock.
// Header-only and free of Rack, so tests/Depreciation/test_offload.cpp covers it on its own.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <pthread/qos.h>
#elif defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
	#include <xmmintrin.h>
#endif

namespace pcm70 {

/** Single producer, single consumer, fixed capacity (a power of two). */
template <class T, size_t N>
class Spsc {
	static_assert((N & (N - 1)) == 0, "capacity must be a power of two");
public:
	bool push(const T& v) {
		const uint64_t h = head_.load(std::memory_order_relaxed);
		if (h - tail_.load(std::memory_order_acquire) >= N) return false;
		buf_[h & (N - 1)] = v;
		head_.store(h + 1, std::memory_order_release);
		return true;
	}
	bool pop(T& v) {
		const uint64_t t = tail_.load(std::memory_order_relaxed);
		if (t == head_.load(std::memory_order_acquire)) return false;
		v = buf_[t & (N - 1)];
		tail_.store(t + 1, std::memory_order_release);
		return true;
	}
	/** The oldest element without taking it (consumer thread). */
	bool peek(T& v) const {
		const uint64_t t = tail_.load(std::memory_order_relaxed);
		if (t == head_.load(std::memory_order_acquire)) return false;
		v = buf_[t & (N - 1)];
		return true;
	}
	/** Empty it. Only while neither thread is using it. */
	void clear() { tail_.store(head_.load()); }
	size_t size() const { return (size_t)(head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)); }
private:
	T buf_[N];
	std::atomic<uint64_t> head_{0}, tail_{0};
};

/** The latest of a stream of states, from one thread to another, without a lock and without tearing: the writer fills a private slot and publishes it, the reader
    takes the newest published one. */
template <class T>
class Latest {
public:
	T& writing() { return buf_[w_]; }
	void publish() { const int old = mid_.exchange(w_ | FRESH, std::memory_order_acq_rel); w_ = old & 3; }
	/** The newest published state (the one before it if nothing is new), or the zero state before the first publish. */
	const T& read() {
		if (mid_.load(std::memory_order_relaxed) & FRESH) { const int old = mid_.exchange(r_, std::memory_order_acq_rel); r_ = old & 3; }
		return buf_[r_];
	}
private:
	enum { FRESH = 4 };
	T buf_[3] = {};
	int w_ = 0, r_ = 2;
	std::atomic<int> mid_{1};
};

/** Thread settings a signal thread wants: the interactive QoS class on macOS (otherwise it lands on an efficiency core), a raised priority on Windows, and
    flush-to-zero as Rack gives its own engine threads. */
inline void prepareSignalThread() {
#if defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
#if defined(__x86_64__) || defined(_M_X64)
	_mm_setcsr(_mm_getcsr() | 0x8040);                     // FTZ | DAZ
#elif defined(__aarch64__)
	uint64_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr)); fpcr |= (1ull << 24); __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));   // FZ
#endif
}

class Offload {
public:
	static const int OUTS = 4;
	/** One machine sample, on the worker: `in` is the input sample, `index` counts input samples since start() (the zeros included). */
	typedef std::function<void(double in, int64_t index, double out[OUTS])> Step;

	Offload() : in_(RING), out_(RING * OUTS) {}
	~Offload() { stop(); }

	/** Audio thread. Starts the worker with `latency` samples in flight. */
	void start(const Step& step, size_t latency) {
		stop();
		step_ = step;
		inHead_.store(0); inTail_.store(0); outHead_.store(0); outTail_.store(0);
		underruns_.store(0);
		for (int c = 0; c < OUTS; c++) last_[c] = 0.0;
		latency_ = latency < MAXLAT ? latency : MAXLAT; dropNext_ = 0; sinceKick_ = 0;
		for (size_t i = 0; i < latency_; i++) put(0.0);
		run_.store(true);
		worker_ = std::thread([this]() { loop(); });
		// Let the worker take the zeros before the first real sample is asked for, or its start-up would be read as a run of underruns and each would
		// slip the latency up by a sample. Bounded: a worker that cannot be scheduled for 20 ms is the caller's problem, not a reason to hang.
		const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
		while (outHead_.load(std::memory_order_acquire) < latency_ && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
	}
	/** Audio thread. Stops the worker and waits for it; anything unread is dropped. */
	void stop() {
		if (!worker_.joinable()) return;
		run_.store(false);
		cv_.notify_all();
		worker_.join();
	}
	bool running() const { return worker_.joinable(); }

	/** Audio thread: one input sample in, then one output frame out (the last one again, and an underrun counted, if the worker had not finished it). */
	void exchange(double in, double out[OUTS]) {
		if (dropNext_ > 0) dropNext_--; else put(in);
		if (++sinceKick_ >= KICK) { sinceKick_ = 0; cv_.notify_one(); }
		const uint64_t t = outTail_.load(std::memory_order_relaxed);
		if (t != outHead_.load(std::memory_order_acquire)) {
			for (int c = 0; c < OUTS; c++) last_[c] = out_[(size_t)((t * OUTS + c) & (RING * OUTS - 1))];
			outTail_.store(t + 1, std::memory_order_release);
		} else {
			underruns_.fetch_add(1, std::memory_order_relaxed);
		}
		std::memcpy(out, last_, sizeof last_);
	}

	/** Audio thread: change the samples in flight, now. More: that many zeros go in. Fewer: that many of the next inputs are dropped. */
	void setLatency(size_t latency) {
		latency = latency < MAXLAT ? latency : MAXLAT;
		if (latency > latency_) { for (size_t i = latency_; i < latency; i++) put(0.0); dropNext_ = 0; }
		else dropNext_ += latency_ - latency;
		latency_ = latency;
	}
	size_t latency() const { return latency_; }
	/** Audio thread: the index the next input sample will have (the zeros of the latency included), in the Step callback's `index`. */
	int64_t position() const { return (int64_t)inHead_.load(std::memory_order_relaxed); }
	uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
	/** Output frames waiting for the audio thread: about `latency` when the worker is keeping up. */
	size_t ready() const { return (size_t)(outHead_.load(std::memory_order_acquire) - outTail_.load(std::memory_order_acquire)); }

private:
	static const size_t RING = 1 << 16;                    // input samples (and output frames) the rings hold
	static const size_t MAXLAT = RING / 2;
	static const int KICK = 32;                            // the worker is woken every this many input samples

	void put(double v) {
		const uint64_t h = inHead_.load(std::memory_order_relaxed);
		in_[(size_t)(h & (RING - 1))] = v;
		inHead_.store(h + 1, std::memory_order_release);
	}

	void loop() {
		prepareSignalThread();
		std::mutex m;
		int64_t index = 0;
		double o[OUTS];
		while (run_.load(std::memory_order_relaxed)) {
			const uint64_t h = inHead_.load(std::memory_order_acquire);
			uint64_t t = inTail_.load(std::memory_order_relaxed);
			if (t == h) {
				std::unique_lock<std::mutex> lock(m);
				cv_.wait_for(lock, std::chrono::milliseconds(2));
				continue;
			}
			while (t != h) {
				// the output ring is as long as the input ring and every output answers an input, so it cannot overrun a reader that keeps up; one that has
				// stopped reading is waited out rather than overwritten
				const uint64_t oh = outHead_.load(std::memory_order_relaxed);
				if (oh - outTail_.load(std::memory_order_acquire) >= RING) {
					if (!run_.load(std::memory_order_relaxed)) return;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				step_(in_[(size_t)(t & (RING - 1))], index++, o);
				for (int c = 0; c < OUTS; c++) out_[(size_t)((oh * OUTS + c) & (RING * OUTS - 1))] = o[c];
				outHead_.store(oh + 1, std::memory_order_release);
				t++;
				inTail_.store(t, std::memory_order_release);
			}
		}
	}

	Step step_;
	std::vector<double> in_, out_;
	std::atomic<uint64_t> inHead_{0}, inTail_{0}, outHead_{0}, outTail_{0};
	std::atomic<bool> run_{false};
	std::atomic<uint64_t> underruns_{0};
	std::condition_variable cv_;
	std::thread worker_;
	// audio-thread state
	size_t latency_ = 0, dropNext_ = 0; int sinceKick_ = 0;
	double last_[OUTS] = {};
};

}
