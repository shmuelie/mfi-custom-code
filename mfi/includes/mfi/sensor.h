#pragma once

#include <string>

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
	private:
		uint8_t _id;
		double read(const std::string& path) const;
	};
}