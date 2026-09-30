// Moon Technologies (GPL-3.0-or-later). Minimal stand-in for MAME's emu.h, just enough to compile es5510.cpp outside MAME.
// Not MAME code; part of the DP4Research harness.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <string>
#include <vector>
#include <ostream>
#include <utility>
#include <type_traits>

using u8 = uint8_t;   using s8 = int8_t;
using u16 = uint16_t; using s16 = int16_t;
using u32 = uint32_t; using s32 = int32_t;
using u64 = uint64_t; using s64 = int64_t;
using offs_t = uint32_t;

#define ATTR_COLD
constexpr int CLEAR_LINE = 0, ASSERT_LINE = 1;
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENFLAGS = -3 };

template <typename T> constexpr T BIT(T x, int n) { return (x >> n) & T(1); }

namespace util {
template <typename T> inline typename std::make_signed<T>::type sext(T value, unsigned width) {
	typedef typename std::make_signed<T>::type S;
	return S(value << (8 * sizeof(T) - width)) >> (8 * sizeof(T) - width);
}
class disasm_interface {
public:
	class data_buffer {};
	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &, offs_t, const data_buffer &, const data_buffer &) = 0;
};
}

inline std::string string_format(const char *fmt, ...) {
	char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap); return buf;
}

inline int64_t mul_32x32(int32_t a, int32_t b) { return int64_t(a) * int64_t(b); }
inline void logerror(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

struct machine_config {};
struct device_t {};
struct device_type_t {};
using device_type = const device_type_t &;
struct address_space {};

struct state_entry_stub {
	state_entry_stub &noshow() { return *this; }
	state_entry_stub &formatstr(const char *) { return *this; }
};

class device_memory_interface {
public:
	using space_config_vector = std::vector<std::pair<int, const void *>>;
};

class cpu_device : public device_t, public device_memory_interface {
public:
	cpu_device(const machine_config &, device_type, const char *, device_t *, uint32_t) {}
	virtual ~cpu_device() = default;
	void start() { device_start(); }
	void reset() { device_reset(); }
protected:
	virtual void device_start() {}
	virtual void device_reset() {}
	virtual device_memory_interface::space_config_vector memory_space_config() const { return {}; }
	virtual uint64_t execute_clocks_to_cycles(uint64_t c) const noexcept { return c; }
	virtual uint64_t execute_cycles_to_clocks(uint64_t c) const noexcept { return c; }
	virtual uint32_t execute_min_cycles() const noexcept { return 1; }
	virtual uint32_t execute_max_cycles() const noexcept { return 1; }
	virtual void execute_run() {}
	virtual void execute_set_input(int, int) {}
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return nullptr; }
	template <typename T> state_entry_stub state_add(int, const char *, T &) { return {}; }
	template <typename T> void save_item(T &&, const char * = nullptr) {}
	template <typename T> void save_pointer(T &&, const char *, size_t) {}
	void set_icountptr(int &) {}
};

#define NAME(x) x, #x
#define DEFINE_DEVICE_TYPE(Type, Class, Short, Full) const device_type_t Type##_stub{}; device_type Type = Type##_stub;
#define DECLARE_DEVICE_TYPE(Type, Class) extern device_type Type;
