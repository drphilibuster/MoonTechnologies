#include "Publisher.hpp"

// __APPLE__, not Rack's ARCH_MAC: that one comes from <arch.hpp>, which this
// file does not include, so testing it silently compiled this whole file away.
#if defined __APPLE__

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <Syphon/SyphonOpenGLServer.h>

// ---------------------------------------------------------------------------
// Syphon, on Rack's own GL context.
//
// Rack builds NANOVG_GL2, so the context is legacy OpenGL 2.0 -- which is what
// Syphon's OpenGL server wants, and its publishFrameTexture: explicitly
// restores the GL state it touches on a legacy context, so it cannot disturb
// what NanoVG draws next.
//
// The frames arrive as CPU-side RGBA, because that is the shape the video bus
// carries (see src/VideoBus.hpp) and the shape Repossession decodes into. So
// there is exactly one upload per frame and nothing is ever read back off the
// GPU. A source that one day produces a texture directly can skip even that by
// handing its GLuint straight to publish -- the wrapper is deliberately thin
// enough for that to be a small change.

namespace transmittal {

struct Impl {
	SyphonOpenGLServer* server = nil;
	GLuint tex = 0;
	int w = 0, h = 0;
};

bool publisherAvailable() { return true; }
const char* publisherName() { return "Syphon"; }

bool Publisher::start(const std::string& name, int w, int h) {
	if (w <= 0 || h <= 0)
		return false;
	Impl* p = (Impl*)impl;
	if (p && p->w == w && p->h == h && p->server)
		return true;                       // already up at this size
	stop();

	CGLContextObj ctx = CGLGetCurrentContext();
	if (!ctx)
		return false;                      // not on the UI thread, or no GL yet

	p = new Impl;
	p->w = w;
	p->h = h;

	glGenTextures(1, &p->tex);
	glBindTexture(GL_TEXTURE_2D, p->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Allocated once at full size, so every frame is a glTexSubImage2D into
	// storage that already exists rather than a fresh allocation.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
	p->server = [[SyphonOpenGLServer alloc] initWithName:nsName context:ctx options:nil];
	if (!p->server) {
		glDeleteTextures(1, &p->tex);
		delete p;
		return false;
	}
	impl = p;
	return true;
}

void Publisher::publish(const uint8_t* data, int w, int h) {
	Impl* p = (Impl*)impl;
	if (!p || !p->server || !data || w != p->w || h != p->h)
		return;

	// Save and restore the binding: NanoVG keeps its own idea of what is bound
	// and does not expect anyone else to have changed it between frames.
	GLint prev = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
	glBindTexture(GL_TEXTURE_2D, p->tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev);

	// Flipped: the bus carries frames top-row-first, the way an image file and
	// a decoder produce them, whereas GL's origin is the bottom left.
	[p->server publishFrameTexture:p->tex
	                 textureTarget:GL_TEXTURE_2D
	                   imageRegion:NSMakeRect(0, 0, w, h)
	             textureDimensions:NSMakeSize(w, h)
	                       flipped:YES];
}

bool Publisher::hasClients() const {
	Impl* p = (Impl*)impl;
	return p && p->server && [p->server hasClients];
}

void Publisher::stop() {
	Impl* p = (Impl*)impl;
	if (!p)
		return;
	if (p->server) {
		[p->server stop];
		p->server = nil;
	}
	if (p->tex && CGLGetCurrentContext())
		glDeleteTextures(1, &p->tex);
	delete p;
	impl = NULL;
}

} // namespace transmittal

#endif
