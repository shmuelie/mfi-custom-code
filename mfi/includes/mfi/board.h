#pragma once

#include <string>
#include <vector>

namespace mfi {
	class sensor;

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