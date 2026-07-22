#include "mfi_update/release.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace mfi_update {

	std::optional<std::pair<semver, std::string>> pick_latest_tag(
		std::string const& matching_refs_json, std::string const& tool) noexcept {
		json parsed;
		try {
			parsed = json::parse(matching_refs_json);
		}
		catch (json::exception const&) {
			return std::nullopt;
		}
		if (!parsed.is_array()) {
			return std::nullopt;
		}

		std::string const prefix = "refs/tags/" + tool + "/";
		std::optional<std::pair<semver, std::string>> best;
		for (auto const& entry : parsed) {
			if (!entry.is_object() || !entry.contains("ref") || !entry["ref"].is_string()) {
				continue;
			}
			std::string ref = entry["ref"].get<std::string>();
			if (ref.compare(0, prefix.size(), prefix) != 0) {
				continue;
			}
			std::string tag = ref.substr(std::string("refs/tags/").size()); // "<tool>/vX.Y.Z"
			auto version = parse_semver(ref.substr(prefix.size())); // "vX.Y.Z"
			if (!version) {
				continue;
			}
			if (!best || *version > best->first) {
				best = std::make_pair(*version, tag);
			}
		}
		return best;
	}

	std::optional<std::string> pick_asset_url(
		std::string const& release_json, std::string const& tool) noexcept {
		json parsed;
		try {
			parsed = json::parse(release_json);
		}
		catch (json::exception const&) {
			return std::nullopt;
		}
		if (!parsed.is_object() || !parsed.contains("assets") || !parsed["assets"].is_array()) {
			return std::nullopt;
		}
		for (auto const& asset : parsed["assets"]) {
			if (!asset.is_object()) {
				continue;
			}
			if (!asset.contains("name") || !asset["name"].is_string()) {
				continue;
			}
			if (asset["name"].get<std::string>() != tool) {
				continue;
			}
			if (asset.contains("browser_download_url") && asset["browser_download_url"].is_string()) {
				return asset["browser_download_url"].get<std::string>();
			}
		}
		return std::nullopt;
	}
}
