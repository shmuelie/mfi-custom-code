#include <iostream>
#include "mfi_mqtt_client/device.h"
#include <CLI/CLI.hpp>
#include "version_info.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#define log_info(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::info, __VA_ARGS__)
#define log_warn(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::warn, __VA_ARGS__)
#define log_error(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::err, __VA_ARGS__)
#define log_debug(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::debug, __VA_ARGS__)
#define log_trace(...) log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::trace, __VA_ARGS__)

std::shared_ptr<mfi_mqtt_client::device> create_device(std::string const& server, uint16_t port, std::string const& username, std::string const& password) {
	try {
		mfi::board b{};
		auto device = std::make_shared<mfi_mqtt_client::device>(b, server, port, username, password);
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
	app.add_option("--polling-rate", polling_rate, "The polling rate in milliseconds")->default_val(1000)->check(CLI::Range(0U, UINT32_MAX));
	spdlog::level::level_enum log_level;
	app.add_option("--log-level", log_level, "The log level to use")->transform(spdlog_level_transformer)->default_val(spdlog::level::info);

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}

	auto logger = spdlog::stdout_color_mt("main");
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-5l%$] [%s:%#] %v");
	logger->set_level(log_level);
	spdlog::set_default_logger(logger);

	logger->log_info("Starting MQTT client...");

	auto device = create_device(server, port, username, password);
	if (!device) {
		return -2;
	}

	logger->log_info("Connecting to client...");

	try {
		if (!device->connect()) {
			return -3;
		}
	}
	catch (std::exception& e) {
		logger->log_error("Error connecting: {}", e.what());
		return -3;
	}

	logger->log_info("Connected to client. Starting polling...");

	for (;;) {
		try {
			device->processMessages(polling_rate);
		}
		catch (std::exception& e) {
			logger->log_error("Error procssing message: {}", e.what());
		}
		device->update();
	}

	logger->log_info("Exiting");

	return 0;
}