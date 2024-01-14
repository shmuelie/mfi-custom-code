#pragma once

#include <string>
#include "FileWatch.hpp"

namespace mfi {
	class sensor {
	public:
		explicit sensor(uint8_t id);

		uint8_t id() const;
		double power() const;
		double current() const;
		double voltage() const;
		double power_factor() const;
		bool relay() const;
		void relay(bool value) const;

		filewatch::FileWatch<std::string> watch_relay(std::function<void(bool)> callback) const;
		filewatch::FileWatch<std::string> watch_power(std::function<void(double)> callback) const;
	private:
		uint8_t _id;
		double read(const std::string& path) const;
	};
}