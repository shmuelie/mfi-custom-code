#pragma once

#include <string>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <vector>

namespace shmuelie {
	template<typename InputIt>
	std::string join(InputIt first,
		InputIt last,
		const std::string& separator = ", ",
		const std::string& concluder = "")
	{
		if (first == last)
		{
			return concluder;
		}

		std::stringstream ss{};
		ss << *first;
		++first;

		while (first != last)
		{
			ss << separator;
			ss << *first;
			++first;
		}

		ss << concluder;

		return ss.str();
	}

	template<typename NumberType>
	std::optional<NumberType> try_stoul(std::string const& __str, size_t* __idx = nullptr, int __base = 10) {
		try {
			return static_cast<NumberType>(std::stoul(__str, __idx, __base));
		}
		catch (std::invalid_argument) {
			return std::nullopt;
		}
		catch (std::out_of_range) {
			return std::nullopt;
		}
	}

	template<typename NumberType>
	std::optional<NumberType> try_stoi(std::string const& __str, size_t* __idx = nullptr, int __base = 10) {
		try {
			return static_cast<NumberType>(std::stoi(__str, __idx, __base));
		}
		catch (std::invalid_argument) {
			return std::nullopt;
		}
		catch (std::out_of_range) {
			return std::nullopt;
		}
	}

	bool is_number(std::string const& str);

	std::vector<std::string> split(std::string const& str, char delim);
}