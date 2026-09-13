#include <iostream>
#include <vector>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstring>
#include "mfi_mqtt_client/device.h"
#include <CLI/CLI.hpp>
#include "mfi_update.h"
#include "mfi_update/background_updater.h"
#include "version_info.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#define log_info(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::info, __VA_ARGS__)
#define log_warn(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::warn, __VA_ARGS__)
#define log_error(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::err, __VA_ARGS__)
#define log_debug(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::debug, __VA_ARGS__)
#define log_trace(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::trace, __VA_ARGS__)

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
}

std::shared_ptr<mfi_mqtt_client::device> create_device(std::string const& server, uint16_t port,
	std::string const& username, std::string const& password, sensor_policy policy) {
	try {
		mfi::board b{};
		auto device = std::make_shared<mfi_mqtt_client::device>(b, server, port, username, password, policy);
		device->init();
		return device;
	}
	catch (std::exception& e) {
		spdlog::default_logger()->log_error("Error creating device: {}", e.what());
		return nullptr;
	}
}

CLI::CheckedTransformer spdlog_level_transformer{
	CLI::TransformPairs<spdlog::level::level_enum>{
		{ "trace", spdlog::level::trace },
		{ "debug", spdlog::level::debug },
		{ "info", spdlog::level::info },
		{ "warn", spdlog::level::warn },
		{ "error", spdlog::level::err },
		{ "critical", spdlog::level::critical },
		{ "off", spdlog::level::off }
	}
};

int main(int argc, char* argv[]) {
	CLI::App app{ PROJECT_DESCRIPTION };
	app.set_version_flag("--version", PROJECT_NAME " " PROJECT_VERSION);
	app.set_config("--config", "", "Configuration file to load options from", false)->check(CLI::ExistingFile);

	std::string server;
	app.add_option("--server", server, "The MQTT server to connect to")->required();
	uint16_t port;
	app.add_option("--port", port, "The port to use when connecting to the MQTT server")->default_val(1883);
	std::string username;
	app.add_option("--username", username, "The username to use when connecting to the MQTT server")->required();
	std::string password;
	app.add_option("--password", password, "The password to use when connecting to the MQTT server")->required();
	uint32_t polling_rate;
	app.add_option("--polling-rate", polling_rate, "The polling rate in milliseconds")->default_val(1000)->check(CLI::Range(1U, UINT32_MAX));
	uint32_t power_refresh;
	app.add_option("--power-refresh-interval", power_refresh, "Successful power refresh interval in seconds")
		->default_val(60)->check(CLI::Range(1U, 86400U));
	uint32_t power_expiry;
	app.add_option("--power-expire-after", power_expiry, "Power expiration advertised in discovery, in seconds")
		->default_val(180)->check(CLI::Range(1U, 259200U));
	spdlog::level::level_enum log_level;
	app.add_option("--log-level", log_level, "The log level to use")->transform(spdlog_level_transformer)->default_val(spdlog::level::info);

	bool update_enabled;
	app.add_flag("--update,!--no-update", update_enabled, "Enable self-update from GitHub Releases")->default_val(true);
	uint32_t update_interval;
	app.add_option("--update-interval", update_interval, "Seconds between update checks")->default_val(86400);
	std::string update_repo;
	app.add_option("--update-repo", update_repo, "GitHub owner/name for updates")->default_val("shmuelie/mfi-custom-code");
	std::string update_proxy;
	app.add_option("--update-proxy", update_proxy, "Proxy host:port for the update downloader");
	bool update_insecure;
	app.add_flag("--update-insecure,!--update-check-cert", update_insecure, "Skip TLS cert verification when updating")->default_val(true);

	try {
		app.parse(argc, argv);
		if (power_expiry / power_refresh < 3
			|| static_cast<uint64_t>(polling_rate) > static_cast<uint64_t>(power_refresh) * 1000) {
			throw CLI::ValidationError("Power freshness", "expiry must allow three refresh intervals, and polling must not exceed refresh");
		}
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}

	auto logger = spdlog::stdout_color_mt("main");
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-5l%$] [%s:%#] %v");
	logger->set_level(log_level);
	spdlog::set_default_logger(logger);
	struct sigaction action{};
	action.sa_handler = request_stop;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, nullptr) != 0 || sigaction(SIGTERM, &action, nullptr) != 0) {
		logger->log_error("Cannot install termination handlers: {}", std::strerror(errno));
		return -4;
	}

	logger->log_info("Starting MQTT client...");

	std::vector<std::string> args;
	for (int i = 0; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}
	auto updater = mfi_update::make_background_updater(
		update_enabled, update_interval, update_repo, update_proxy, update_insecure,
		PROJECT_NAME, PROJECT_VERSION, args);
	if (!updater) {
		logger->log_info("Self-update disabled");
	}

	auto device = create_device(server, port, username, password,
		sensor_policy::power(std::chrono::seconds(power_refresh), std::chrono::seconds(power_expiry)));
	if (!device) {
		return -2;
	}

	logger->log_info("Connecting to client...");

	try {
		if (!device->connect()) {
			logger->log_warn("Initial MQTT connection failed; retrying in the main loop");
		}
	}
	catch (std::exception& e) {
		logger->log_error("Error connecting: {}", e.what());
		return -3;
	}

	logger->log_info("Starting polling...");

	auto next_poll = std::chrono::steady_clock::now();
	while (!stop_requested) {
		try {
			device->processMessages(100);
		}
		catch (std::exception& e) {
			logger->log_error("Error processing message: {}", e.what());
		}
		if (stop_requested) {
			break;
		}
		auto now = std::chrono::steady_clock::now();
		if (now >= next_poll) {
			try {
				device->update();
			}
			catch (std::exception& e) {
				logger->log_error("Error updating device: {}", e.what());
			}
			next_poll = now + std::chrono::milliseconds(polling_rate);
		}
		if (updater && !stop_requested) {
			bool interrupted = false;
			try {
				auto result = updater->tick();
				if (result && *result == mfi_update::update_result::ready) {
					interrupted = true;
					device->shutdown();
					if (stop_requested) {
						break;
					}
					result = updater->apply_ready([] { return stop_requested != 0; });
					if (!stop_requested && !device->connect()) {
						logger->log_warn("MQTT reconnect after update failed; retrying");
					}
					interrupted = false;
				}
				if (result && *result != mfi_update::update_result::up_to_date) {
					logger->log_info("Self-update: {}", mfi_update::describe(*result));
				}
			}
			catch (std::exception& e) {
				logger->log_error("Error during update check: {}", e.what());
				if (interrupted && !stop_requested) {
					try {
						if (!device->connect()) {
							logger->log_warn("MQTT reconnect after failed update failed; retrying");
						}
					}
					catch (std::exception const& reconnect_error) {
						logger->log_error("MQTT reconnect failed: {}", reconnect_error.what());
					}
				}
			}
		}
	}

	if (updater) {
		updater->request_stop();
	}
	bool offline = device->shutdown();
	if (updater) {
		updater->stop();
	}
	logger->log_info("Exiting");

	return offline ? 0 : 1;
}