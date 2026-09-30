// Minimal stand-in for MAME's logmacro.h: logging compiled out.
#ifndef VERBOSE
#define VERBOSE 0
#endif
#define LOGMASKED(mask, ...) do { if (VERBOSE & (mask)) logerror(__VA_ARGS__); } while (0)
#define LOG(...) LOGMASKED(1, __VA_ARGS__)
