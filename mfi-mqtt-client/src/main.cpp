#include <iostream>
#include "mfi_mqtt_client/options.h"
#include "mfi_mqtt_client/mfi_device.h"

#define STR_(S) #S
#define STR(S) STR_(S)

static const char USAGE[] =
R"(mFi MQTT Client.

  Usage:
	mfi-mqtt-client --server SERVER --port PORT --username USER --password PASS [--rate RATE]
	mfi-mqtt-client (-h | --help)
	mfi-mqtt-client --version

  Options:
	-h --help        Show this screen.
	--version        Show version.
	--server SERVER  The MQTT server to connect to.
	--port PORT      The port to use when connecting to the MQTT server.
	--username USER  The username to use when connecting to the MQTT server.
	--password PASS  The password to use when connecting to the MQTT server.
	--rate RATE      The polling rate in milliseconds [default: 1000].
)";

std::shared_ptr<mfi_mqtt_client::mfi_device> create_device(const mfi_mqtt_client::options& ops) {
	try {
		mfi::board b{};
		auto device = std::make_shared<mfi_mqtt_client::mfi_device>(b, ops.server(), ops.port(), ops.username(), ops.password());
		device->init();
		return device;
	}
	catch (std::exception& e) {
		std::cout << "Error creating devices: " << e.what() << std::endl;
		return nullptr;
	}
}

int main(int argc, char* argv[]) {
	mfi_mqtt_client::options ops{ USAGE, { argv + 1, argv + argc } };

	if (ops.help()) {
		std::cout << USAGE << std::endl;
		return 0;
	}

	if (ops.version()) {
		std::cout << "mFI MQTT Client " << STR(MFI_MQTT_CLIENT_VERSION) << std::endl;
		return 0;
	}

	if (ops.errors().size() > 0) {
		for (auto const& error : ops.errors()) {
			std::cout << error << std::endl;
		}
		std::cout << USAGE << std::endl;
		return -1;
	}

	std::cout << "Starting MQTT client..." << std::endl;

	auto device = create_device(ops);
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
			device->processMessages(ops.polling_rate());
		}
		catch (std::exception& e) {
			std::cout << "Error procssing message: " << e.what() << std::endl;
		}
		device->update();
	}

	std::cout << "Exiting" << std::endl;

	return 0;
}