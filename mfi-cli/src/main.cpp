#include <iostream>
#include <unistd.h>
#include <string>
#include "mfi.h"

static void print_help()
{
	std::cout <<
		"Help:" << std::endl;
}

static int status(int argc, char* argv[]) {
	if (argc == 0) {
		mfi::board mfiBoard{};
		std::cout << "Model: " << mfiBoard.name() << std::endl;
		for (auto& sensor : mfiBoard.sensors()) {

			if (sensor.relay()) {
				std::cout <<
					"\tSensor " << std::to_string(sensor.id()) << "\n" <<
					"\t\tPower:\t" << sensor.power() << "\n" <<
					"\t\tCurrent:\t" << sensor.current() << "\n" <<
					"\t\tVoltage:\t" << sensor.voltage() << "\b" << std::endl;
			}
			else {
				std::cout << "\tSensor " << std::to_string(sensor.id()) << " off" << std::endl;
			}

		}
	}

	return 0;
}

static int sensor(int argc, char* argv[]) {
	if (argc == 1) {
		uint8_t sensorId = static_cast<uint8_t>(strtoul(argv[0], nullptr, 10));
		mfi::board mfiBoard{};
		if (sensorId < mfiBoard.sensors().size())
		{
			std::cout << (mfiBoard.sensors().at(sensorId).relay() ? 1 : 0) << std::endl;
		}
		else
		{
			std::cout << "Invalid sensor ID " << std::to_string(sensorId) << std::endl;
			print_help();
		}
	}
	else if (argc == 2) {
		uint8_t sensorId = static_cast<uint8_t>(strtoul(argv[0], nullptr, 10));
		std::string onOff{ argv[1] };
		mfi::board mfiBoard{};
		if (sensorId < mfiBoard.sensors().size()) {
			mfi::sensor sensor = mfiBoard.sensors().at(sensorId);
			if (onOff == "on") {
				sensor.relay(true);
			}
			else if (onOff == "off") {
				sensor.relay(false);
			}
			else {
				std::cout << "Valid input is on/off, not " << onOff << std::endl;
				print_help();
			}
		}
		else {
			std::cout << "Invalid sensor ID " << std::to_string(sensorId) << std::endl;
			print_help();
		}
	}
	else {
		std::cout << "Missing parameter" << std::endl;
		print_help();
	}

	return 0;
}

int main(int argc, char* argv[]) {
	if (argc == 1) {
		print_help();
	}
	else
	{
		std::string command{ argv[1] };
		if (command == "help") {
			print_help();
		}
		else if (command == "status") {
			return status(argc - 2, argv + 2);
		}
		else if (command == "sensor") {
			return sensor(argc - 2, argv + 2);
		}
		else {
			std::cout << "Unknown command " << command << std::endl;
		}
	}

	return 0;
}