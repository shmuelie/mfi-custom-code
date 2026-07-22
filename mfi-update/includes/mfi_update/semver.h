#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mfi_update {
	/**
	 * @brief A parsed semantic version (MAJOR.MINOR.PATCH).
	 */
	struct semver {
		std::uint32_t major{ 0 };
		std::uint32_t minor{ 0 };
		std::uint32_t patch{ 0 };

		/**
		 * @brief Orders versions by major, then minor, then patch.
		 */
		constexpr auto operator<=>(semver const&) const = default;
	};

	/**
	 * @brief Parses the first MAJOR.MINOR.PATCH found in a string.
	 *
	 * Leading text (such as a "name/v" tag prefix or a "name " program-name
	 * prefix) is skipped. Parsing stops at the third numeric component; any
	 * trailing text (pre-release/build metadata) is ignored.
	 *
	 * @param text The text to parse.
	 * @return The parsed version, or std::nullopt if no MAJOR.MINOR.PATCH exists.
	 */
	std::optional<semver> parse_semver(std::string_view text) noexcept;

	/**
	 * @brief Returns the greater of two versions.
	 */
	constexpr semver const& max_semver(semver const& a, semver const& b) noexcept {
		return (b > a) ? b : a;
	}
}
