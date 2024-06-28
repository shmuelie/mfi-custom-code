#include "mg/logger.h"

using namespace std;
using namespace mg;

log_level logger::level() noexcept {
	return static_cast<log_level>(mg_log_level);
}

void logger::level(log_level value) noexcept {
	mg_log_level = static_cast<int>(value);
}

void logger::hex_dump(void const* buf, size_t len) noexcept {
	mg_hexdump(buf, len);
}