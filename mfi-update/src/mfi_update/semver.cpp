#include "mfi_update/semver.h"
#include <cctype>

namespace mfi_update {

	namespace {
		// Reads a run of ASCII digits starting at index i into out; advances i.
		// Returns false if there is no digit at i or the value overflows.
		bool read_uint(std::string_view text, size_t& i, std::uint32_t& out) noexcept {
			if (i >= text.size() || std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
				return false;
			}
			std::uint64_t value = 0;
			bool any = false;
			while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
				value = value * 10 + static_cast<std::uint32_t>(text[i] - '0');
				if (value > 0xFFFFFFFFULL) {
					return false;
				}
				++i;
				any = true;
			}
			out = static_cast<std::uint32_t>(value);
			return any;
		}
	}

	std::optional<semver> parse_semver(std::string_view text) noexcept {
		// Scan for the first position that parses as MAJOR.MINOR.PATCH.
		for (size_t start = 0; start < text.size(); ++start) {
			if (std::isdigit(static_cast<unsigned char>(text[start])) == 0) {
				continue;
			}
			// A component must not be a continuation of a longer number, e.g.
			// don't start mid-way through "1234"; require the char before to be
			// a non-digit (or start of string).
			if (start > 0 && std::isdigit(static_cast<unsigned char>(text[start - 1])) != 0) {
				continue;
			}
			size_t i = start;
			semver v{};
			if (!read_uint(text, i, v.major)) {
				continue;
			}
			if (i >= text.size() || text[i] != '.') {
				continue;
			}
			++i;
			if (!read_uint(text, i, v.minor)) {
				continue;
			}
			if (i >= text.size() || text[i] != '.') {
				continue;
			}
			++i;
			if (!read_uint(text, i, v.patch)) {
				continue;
			}
			return v;
		}
		return std::nullopt;
	}
}
