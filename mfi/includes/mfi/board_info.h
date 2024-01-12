#pragma once

#include <string>
#include <vector>

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

	class board {
	public:
		explicit board();

		const std::string& name() const;
		const std::string& short_name() const;
		uint16_t id() const;
		const std::vector<sensor>& sensors() const;
	private:
		std::string _name;
		std::string _shortName;
		uint16_t _id;
		std::vector<sensor> _sensors{};
	};
}