#pragma once
#include <stdint.h>

#include <vector>
#include <stddef.h>

namespace ps {

/** Decompresses a raw DEFLATE stream (no zlib or gzip header), as stored inside
    a ZIP entry. Pass the known uncompressed size when there is one. */
bool inflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out,
                size_t expectedSize = 0);

} // namespace ps
