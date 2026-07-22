#pragma once

#include <optional>
#include <string>
#include <vector>
#include "mfi_update/config.h"

namespace mfi_update {
	/**
	 * @brief The device downloader to shell out to.
	 */
	enum class downloader_kind {
		wget,
		curl
	};

	/**
	 * @brief Builds and runs the fetch command, mirroring the mfi-env scripts.
	 *
	 * Uses the device firmware's wget (preferred) or curl over a fork/exec, so
	 * TLS, proxying, and certificate handling are the firmware's, not ours.
	 */
	class downloader {
	public:
		/**
		 * @brief Constructs a downloader for a specific tool.
		 * @param kind Which downloader binary to invoke.
		 * @param cfg Resolved update configuration (proxy/insecure).
		 */
		downloader(downloader_kind kind, config cfg) noexcept;

		/**
		 * @brief Builds the argv for fetching a URL to a file.
		 *
		 * Pure and deterministic so it can be unit tested without executing.
		 *
		 * @param url The URL to fetch.
		 * @param output_path Destination file, or empty to fetch to stdout.
		 * @return The argv vector (program name first).
		 */
		std::vector<std::string> build_argv(std::string const& url, std::string const& output_path) const;

		/**
		 * @brief Fetches a URL and returns its body as a string.
		 * @param url The URL to fetch.
		 * @return The response body, or std::nullopt on failure.
		 */
		std::optional<std::string> fetch_to_string(std::string const& url) const;

		/**
		 * @brief Fetches a URL to a file on disk.
		 * @param url The URL to fetch.
		 * @param output_path Destination file path.
		 * @return true if the download succeeded.
		 */
		bool fetch_to_file(std::string const& url, std::string const& output_path) const;

		/**
		 * @brief Detects an available downloader on PATH.
		 * @return The detected downloader, or std::nullopt if none is usable.
		 */
		static std::optional<downloader_kind> detect() noexcept;

	private:
		downloader_kind _kind;
		config _config;
	};
}
