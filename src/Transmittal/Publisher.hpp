#pragma once
#include <stdint.h>
#include <string>

// ---------------------------------------------------------------------------
// The fast path: a GPU texture handed straight to a compositor, with no encoder
// and no player between us and it.
//
// Syphon on macOS shares an IOSurface, so a frame published here is the same
// memory TouchDesigner samples -- no encode, no segment, no buffering, and a
// latency of one Rack frame plus one TouchDesigner frame. That is the
// difference between a module you can capture with and a module you can perform
// with, which is the whole reason it exists: HLS is two to three seconds
// behind, and no amount of delaying the audio makes that a good idea.
//
// This header is the boundary. Everything above it is plain C++ and knows
// nothing about Objective-C or GL; Syphon.mm implements it on macOS and
// Publisher.cpp stubs it everywhere else, so the module compiles on all four
// targets and simply reports that the fast path is unavailable on three of
// them. Spout would slot in here for Windows the same way.
//
// Every call has to happen on the UI thread with Rack's GL context current.
// That is true in Widget::step(), which Rack calls once a frame before it
// begins its NanoVG frame (Rack/src/window/Window.cpp:496) -- so raw GL there
// touches nothing NanoVG is in the middle of.

namespace transmittal {

/** True on a platform with a texture-sharing backend compiled in. */
bool publisherAvailable();

/** What the backend is called, for the panel to say. */
const char* publisherName();

struct Publisher {
	void* impl = NULL;

	~Publisher() { stop(); }

	/** Bring up a server under `name`, sized w by h. Safe to call repeatedly;
	    a size change tears down and rebuilds, because the shared surface is
	    allocated once at that size. */
	bool start(const std::string& name, int w, int h);

	/** Upload RGBA and publish it. `data` is w*h*4 bytes at the size start()
	    was given. */
	void publish(const uint8_t* data, int w, int h);

	void stop();

	bool running() const { return impl != NULL; }

	/** Whether anything is actually looking. Publishing with no client is
	    cheap but not free, and the panel is more useful when it can say. */
	bool hasClients() const;
};

} // namespace transmittal
