/** Raw-DEFLATE decompression, for ZIP entries.

Rack links zlib but exports none of it, and the SDK ships no zlib header. It does
ship stb_image.h, whose PNG decoder contains a complete and well-tested inflate
implementation with an entry point for headerless (raw) DEFLATE streams -- which
is exactly what ZIP stores.

So this compiles that one piece of stb_image and nothing else:
  STBI_NO_* turns every image codec off,
  STBI_SUPPORT_ZLIB keeps the zlib decoder despite STBI_NO_PNG (stb_image.h:559),
  STB_IMAGE_STATIC makes every symbol static, so nothing is exported from this
  plugin's dylib and nothing can collide with Rack's own copy.
*/
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_SUPPORT_ZLIB

// Everything is static and we use one function of it, so the rest is
// legitimately unused; that is the point, not a mistake.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image.h>
#pragma GCC diagnostic pop

#include "Inflate.hpp"

#include <stdlib.h>

namespace ps {

bool inflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, size_t expectedSize) {
	out.clear();
	if (!in || inLen == 0)
		return false;

	// The uncompressed size is known from the ZIP directory, so decode straight
	// into a right-sized buffer rather than letting stb grow one.
	if (expectedSize > 0) {
		out.resize(expectedSize);
		int n = stbi_zlib_decode_noheader_buffer((char*) &out[0], (int) expectedSize,
			(const char*) in, (int) inLen);
		if (n < 0) {
			out.clear();
			return false;
		}
		out.resize((size_t) n);
		return true;
	}

	int outLen = 0;
	char* p = stbi_zlib_decode_noheader_malloc((const char*) in, (int) inLen, &outLen);
	if (!p)
		return false;
	out.assign((uint8_t*) p, (uint8_t*) p + outLen);
	free(p);
	return true;
}

} // namespace ps
