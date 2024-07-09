#include <iostream>
#include "mfi_mqtt_client/mfi_device.h"
#include <CLI/CLI.hpp>

#define STR_(S) #S
#define STR(S) STR_(S)

std::shared_ptr<mfi_mqtt_client::mfi_device> create_device(std::string const& server, uint16_t port, std::string const& username, std::string const& password) {
	try {
		mfi::board b{};
		auto device = std::make_shared<mfi_mqtt_client::mfi_device>(b, server, port, username, password);
		device->init();
		return device;
	}
	catch (std::exception& e) {
		std::cout << "Error creating devices: " << e.what() << std::endl;
		return nullptr;
	}
}

int main(int argc, char* argv[]) {
	CLI::App app{ "mFi MQTT Client" };
	app.set_version_flag("--version", "mFi MQTT Client " STR(MFI_MQTT_CLIENT_VERSION));

	std::string server;
	app.add_option("--server", server, "The MQTT server to connect to")->required();
	uint16_t port;
	app.add_option("--port", port, "The port to use when connecting to the MQTT server")->required();
	std::string username;
	app.add_option("--username", username, "The username to use when connecting to the MQTT server")->required();
	std::string password;
	app.add_option("--password", password, "The password to use when connecting to the MQTT server")->required();
	uint32_t polling_rate;
	app.add_option("--polling-rate", polling_rate, "The polling rate in milliseconds")->default_val(1000);

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}

	std::cout << "Starting MQTT client..." << std::endl;

	auto device = create_device(server, port, username, password);
	if (!device) {
		return -2;
	}

	std::cout << "Connecting to client..." << std::endl;

	try {
		if (!device->connect()) {
			return -3;
		}
	}
	catch (std::exception& e) {
		std::cout << "Error connecting: " << e.what() << std::endl;
		return -3;
	}

	std::cout << "Connected to client. Starting polling..." << std::endl;

	for (;;) {
		try {
			device->processMessages(polling_rate);
		}
		catch (std::exception& e) {
			std::cout << "Error procssing message: " << e.what() << std::endl;
		}
		device->update();
	}

	std::cout << "Exiting" << std::endl;

	return 0;
}