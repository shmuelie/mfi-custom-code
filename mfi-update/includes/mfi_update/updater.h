#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "mfi_update/config.h"
#include "mfi_update/release.h"
#include "mfi_update/semver.h"
#include "mfi_update/downloader.h"

namespace mfi_update {
	/**
	 * @brief Outcome of an update check/apply cycle.
	 */
	enum class update_result {
		disabled,       ///< Updates are disabled in config.
		no_downloader,  ///< No usable wget/curl was found.
		up_to_date,     ///< Already running the latest release.
		check_failed,   ///< Could not resolve the latest release.
		download_failed,///< Download or validation of the new binary failed.
		updated,        ///< Installed; a successful real exec does not return.
		ready,          ///< Preparation completed; application has not started.
		cancelled,      ///< Cancelled before replacement; no exec was attempted.
		timed_out,      ///< The shared preparation deadline expired.
		apply_failed,   ///< Replacement failed; the target is unchanged.
		replaced_not_restarted, ///< Replacement succeeded, but restart failed/was cancelled.
		preparation_failed, ///< Unexpected exception or worker creation failure.
		cleanup_failed  ///< SIGKILL sent, but the kernel did not permit timely child reaping.
	};

	/** Owns one unique, same-directory staged file until applied or discarded. */
	class prepared_update final {
	public:
		prepared_update(std::string path, std::string target) noexcept;
		~prepared_update();
		prepared_update(prepared_update&& other) noexcept;
		prepared_update& operator=(prepared_update&& other) noexcept;
		prepared_update(prepared_update const&) = delete;
		prepared_update& operator=(prepared_update const&) = delete;
		std::string const& path() const noexcept { return _path; }
		std::string const& target() const noexcept { return _target; }

	private:
		std::string _path;
		std::string _target;
	};

	struct preparation_result {
		update_result result;
		std::optional<prepared_update> artifact;
	};

	/**
	 * @brief Self-updates a tool from this repo's GitHub Releases.
	 *
	 * Wraps the downloader + release parsing + atomic file replacement. The
	 * network fetch and the "replace+re-exec" steps are injectable so the flow
	 * can be unit tested without touching the network or exec()ing.
	 */
	class updater {
	public:
		/** Fetches a URL and returns the body, or nullopt on failure. */
		using fetch_fn = std::function<std::optional<std::string>(std::string const& url)>;
		/** Downloads a URL to a path; returns true on success. */
		using download_fn = std::function<bool(std::string const& url, std::string const& path)>;
		/** Replaces the running binary with @p new_path and re-execs @p argv. */
		using apply_fn = std::function<bool(std::string const& new_path, std::vector<std::string> const& argv)>;

		/**
		 * @brief Constructs an updater.
		 * @param tool_name The tool/asset name (e.g. "mfi-cli").
		 * @param current The currently-running version.
		 * @param cfg Resolved configuration.
		 */
		updater(std::string tool_name, semver current, config cfg);

		/** Overrides the network fetch (defaults to a real downloader). */
		void set_fetch(fetch_fn fn) { _fetch = std::move(fn); }
		/** Overrides the file download (defaults to a real downloader). */
		void set_download(download_fn fn) { _download = std::move(fn); }
		/** Overrides the apply step (defaults to atomic rename + execv). */
		void set_apply(apply_fn fn) { _apply = std::move(fn); }
		using cancellable_fetch_fn = std::function<std::optional<std::string>(
			std::string const&, preparation_context const&)>;
		using cancellable_download_fn = std::function<bool(
			std::string const&, std::string const&, preparation_context const&)>;
		/** Hooks must return promptly when context.interrupted() becomes true. */
		void set_cancellable_fetch(cancellable_fetch_fn fn) { _cancellable_fetch = std::move(fn); }
		void set_cancellable_download(cancellable_download_fn fn) { _cancellable_download = std::move(fn); }
		void set_downloader(downloader dl) { _downloader = std::move(dl); }
		/** Test/embedding seam; defaults to resolving /proc/self/exe. */
		void set_target_path(std::string path) { _target_path = std::move(path); }

		preparation_result prepare(preparation_context const& context) const;
		/** Caller owns the application thread and must finish offline shutdown first. */
		update_result apply(prepared_update const& artifact,
			std::vector<std::string> const& argv,
			std::function<bool()> const& should_cancel = {}) const;

		/**
		 * @brief Resolves the latest available release for this tool.
		 * @return The latest version + asset URL, or nullopt on failure.
		 */
		std::optional<release_asset> check() const;

		/**
		 * @brief Checks for a newer release and, if found, downloads and applies it.
		 *
		 * On success the process image is replaced (via the apply step) and does
		 * not return; the return value covers the non-applied outcomes.
		 *
		 * @param argv The argv to re-exec with (typically the process's own argv).
		 * @return The update_result.
		 */
		update_result check_and_apply(std::vector<std::string> const& argv) const;

	private:
		std::string api_matching_refs_url() const;
		std::string api_release_url(std::string const& tag) const;

		std::string _tool_name;
		semver _current;
		config _config;
		fetch_fn _fetch;
		download_fn _download;
		apply_fn _apply;
		cancellable_fetch_fn _cancellable_fetch;
		cancellable_download_fn _cancellable_download;
		std::optional<downloader> _downloader;
		std::string _target_path;
	};

	/**
	 * @brief Validates that a file exists, is non-empty, and starts with the ELF magic.
	 * @param path The file to check.
	 * @return true if the file looks like an executable ELF image.
	 */
	bool is_valid_elf(std::string const& path) noexcept;

	/**
	 * @brief Resolves the absolute path of the running executable.
	 * @return /proc/self/exe target if available, else the fallback.
	 * @param fallback Used when /proc/self/exe is unavailable (e.g. argv[0]).
	 */
	std::string self_path(std::string const& fallback);

	/**
	 * @brief Atomically replaces @p target_path with @p new_path and re-execs.
	 *
	 * Renames within the same directory (atomic; works while the target is the
	 * running executable), then execv()s the target with @p argv. Returns only
	 * on failure.
	 *
	 * @return false if the rename or exec failed.
	 */
	bool replace_and_reexec(std::string const& new_path, std::string const& target_path,
		std::vector<std::string> const& argv) noexcept;
	update_result replace_and_reexec(std::string const& new_path, std::string const& target_path,
		std::vector<std::string> const& argv, std::function<bool()> const& should_cancel);

	/**
	 * @brief A short human-readable description of an update_result.
	 */
	std::string describe(update_result result) noexcept;
}
