#pragma once
#include <stdint.h>

#include <functional>
#include <string>

namespace ps {
namespace http {

/** A GET whose body can be abandoned as soon as the caller has what it needs.
 *
 * WHY THIS EXISTS. The audit reads a patch's module list, which for a zipped
 * upload lives in a .vcv of a few tens of KB -- but the upload also carries the
 * samples the patch loads, and those can be tens of megabytes. Patchstorage's
 * download endpoint is a WordPress PHP handler on Apache that ignores `Range`
 * outright: it answers 200, with no Accept-Ranges and no Content-Range, and
 * streams the whole file however narrow a range you ask for. Verified against
 * the live endpoint -- so a ranged fetch is not on the table.
 *
 * What IS on the table is hanging up. A zip's local headers are self-describing
 * and come before their data, so a reader walking the stream from the front
 * knows every entry's name and compressed size as it reaches it, with no central
 * directory. Once the patch entry has gone past, the rest of the transfer is
 * dead weight and the connection can simply be dropped. On a 31 MB upload whose
 * .vcv is the first entry, that is 64 KB read instead of 31 MB.
 *
 * Rack's network::requestDownload cannot express this: it writes to a FILE* and
 * has no way to say "stop". So this goes to libcurl directly.
 */

/** True when the streaming fetcher is usable in this process.
 *
 * The curl symbols are resolved with dlsym rather than linked, so that a Rack
 * build which doesn't re-export them costs this plugin a feature rather than the
 * ability to load at all. Callers must have a fallback for false. */
bool available();

/** Receives each chunk as it arrives. Return false to hang up immediately;
    that is a success, not an error. */
typedef std::function<bool(const uint8_t* data, size_t len)> Sink;

/** GETs `url`, feeding the body to `sink` as it arrives.
 *
 * Returns true if the transfer finished OR the sink asked to stop. Returns false
 * on a transport, TLS or HTTP error, with `error` set when non-NULL. */
bool get(const std::string& url, const Sink& sink, std::string* error = NULL);

} // namespace http
} // namespace ps
