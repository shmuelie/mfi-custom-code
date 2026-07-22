#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mfi_update {
	/**
	 * @brief Configuration for the self-update mechanism.
	 *
	 * Values are resolved with the following precedence (highest first):
	 *   explicit override > environment (http_proxy/https_proxy) > built-in default.
	 * Callers apply their own CLI/config-file values as explicit overrides before
	 * calling resolve().
	 */
	struct config {
		/** GitHub repository owner. */
		std::string repo_owner{ "shmuelie" };
		/** GitHub repository name. */
		std::string repo_name{ "mfi-custom-code" };
		/** Proxy as host:port. Empty means direct connection. */
		std::string proxy{};
		/** Whether to route requests through the proxy. */
		bool use_proxy{ false };
		/** Pass --no-check-certificate (device CA store is limited). */
		bool insecure{ true };
		/** Directory the running binary lives in / is replaced in. */
		std::string bin_dir{ "/var/etc/persistent/bin" };
		/** Seconds between update checks for long-running tools. */
		std::uint32_t interval_seconds{ 86400 };
		/** Master enable switch. */
		bool enabled{ true };

		/**
		 * @brief Fills unset proxy settings from the environment.
		 *
		 * If proxy is still empty, reads https_proxy then http_proxy from the
		 * environment. When a proxy value is present (from any source) and
		 * use_proxy was not explicitly enabled, use_proxy is turned on.
		 *
		 * @param getenv_fn Environment lookup (defaults to std::getenv); injectable for tests.
		 * @return A resolved copy of this config.
		 */
		config resolve(const char* (*getenv_fn)(const char*) = nullptr) const;
	};
}
