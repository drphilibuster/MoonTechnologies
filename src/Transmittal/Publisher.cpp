#include "Publisher.hpp"

// The stub, for every platform without a texture-sharing backend compiled in.
// macOS is handled in Syphon.mm and excluded here; Windows would get a Spout.mm
// equivalent and join the exclusion.
//
// The test is __APPLE__ and not Rack's own ARCH_MAC, because ARCH_MAC is a
// macro from <arch.hpp> rather than a -D on the command line -- so a file that
// does not include rack.hpp, as this one deliberately does not, never sees it.
// Testing ARCH_MAC here compiled this stub *and* emptied Syphon.mm, and the
// plugin still linked, because Rack builds with -undefined dynamic_lookup. The
// only symptom was the panel quietly reporting HLS on a Mac.

#if !defined __APPLE__

namespace transmittal {

bool publisherAvailable() { return false; }
const char* publisherName() { return "none"; }

bool Publisher::start(const std::string&, int, int) { return false; }
void Publisher::publish(const uint8_t*, int, int) {}
void Publisher::stop() {}
std::string Publisher::serverName() const { return std::string(); }
bool Publisher::hasClients() const { return false; }

} // namespace transmittal

#endif
