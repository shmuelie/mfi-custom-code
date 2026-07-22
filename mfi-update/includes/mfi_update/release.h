#pragma once

#include <optional>
#include <string>
#include "mfi_update/semver.h"

namespace mfi_update {
	/**
	 * @brief A resolved release: its version and the download URL for the asset.
	 */
	struct release_asset {
		semver version{};
		std::string download_url{};
	};

	/**
	 * @brief Picks the highest semver tag for a tool from a matching-refs response.
	 *
	 * The GitHub git/matching-refs/tags/<tool>/ endpoint returns a JSON array of
	 * objects with a "ref" like "refs/tags/<tool>/vX.Y.Z". This selects the
	 * highest MAJOR.MINOR.PATCH (a true semver max, unlike a lexicographic last).
	 *
	 * @param matching_refs_json The raw JSON body.
	 * @param tool The tool name (tag prefix before "/v").
	 * @return The highest version and its full tag, or std::nullopt.
	 */
	std::optional<std::pair<semver, std::string>> pick_latest_tag(
		std::string const& matching_refs_json, std::string const& tool) noexcept;

	/**
	 * @brief Extracts the download URL for the asset named exactly @p tool.
	 *
	 * Parses a GitHub release JSON (from releases/tags/<tag>) and returns the
	 * browser_download_url of the asset whose name equals the tool name.
	 *
	 * @param release_json The raw JSON body.
	 * @param tool The asset name to match.
	 * @return The download URL, or std::nullopt if not present.
	 */
	std::optional<std::string> pick_asset_url(
		std::string const& release_json, std::string const& tool) noexcept;
}
