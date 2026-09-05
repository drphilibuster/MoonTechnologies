#pragma once
#include <string>

namespace ps {

/** Decodes the HTML entities WordPress emits in titles and excerpts. */
std::string decodeEntities(const std::string& s);
/** Removes tags and collapses whitespace. Excerpts arrive wrapped in <p>. */
std::string stripTags(const std::string& s);
/** stripTags + decodeEntities + trim. Applied on the worker, so no widget ever
    sees raw markup. */
std::string plainText(const std::string& s);

} // namespace ps
