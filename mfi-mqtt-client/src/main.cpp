#include <iostream>
#include "mfi_mqtt_client/options.h"
#include "mfi_mqtt_client/mfi_device.h"

#define STR_(S) #S
#define STR(S) STR_(S)

static const char USAGE[] =
R"(mFi MQTT Client.

  Usage:
	mfi-mqtt-client --server SERVER --port PORT --username USER --password PASS
	mfi-mqtt-client (-h | --help)
	mfi-mqtt-client --version

  Options:
	-h --help        Show this screen.
	--version        Show version.
	--server SERVER  The MQTT server to connect to.
	--port PORT      The port to use when connecting to the MQTT server.
	--username USER  The username to use when connecting to the MQTT server.
	--password PASS  The password to use when connecting to the MQTT server.
)";
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

	mfi::board b{};
	mfi_mqtt_client::mfi_device device{ b, ops.server(), ops.port(), ops.username(), ops.password() };

	try {
		if (!device.connect()) {
			return -2;
		}
	}
	catch (std::exception& e) {
		std::cout << "Error connecting: " << e.what() << std::endl;
		return -2;
	}

	for (;;) {
		try {
			device.processMessages(1000);
		}
		catch (std::exception& e) {
			std::cout << "Error procssing message: " << e.what() << std::endl;
		}
		device.update();
	}

	return 0;
}