#include "Publisher.hpp"

// The stub, for every platform without a texture-sharing backend compiled in.
// macOS is handled in Syphon.mm and excluded here; Windows would get a Spout.mm
// equivalent and join the exclusion.

#if !defined ARCH_MAC

namespace transmittal {

bool publisherAvailable() { return false; }
const char* publisherName() { return "none"; }

bool Publisher::start(const std::string&, int, int) { return false; }
void Publisher::publish(const uint8_t*, int, int) {}
void Publisher::stop() {}
bool Publisher::hasClients() const { return false; }

} // namespace transmittal

#endif
