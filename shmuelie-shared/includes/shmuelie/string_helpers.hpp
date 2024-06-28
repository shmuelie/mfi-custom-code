#pragma once

#include <string>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <vector>

namespace shmuelie {
	template<typename InputIt>
	static std::string join(InputIt first,
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
	static std::optional<NumberType> try_stoul(std::string const& __str, size_t* __idx = nullptr, int __base = 10) {
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
	static std::optional<NumberType> try_stoi(std::string const& __str, size_t* __idx = nullptr, int __base = 10) {
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

	static bool is_number(std::string const& str)
	{
		// `std::find_first_not_of` searches the string for the first character
		// that does not match any of the characters specified in its arguments
		return !str.empty() && (str.find_first_not_of("[0123456789]") == std::string::npos);
	}

	static std::vector<std::string> split(std::string const& str, char delim)
	{
		auto i = 0;
		std::vector<std::string> list;

		auto pos = str.find(delim);

		while (pos != std::string::npos)
		{
			list.push_back(str.substr(i, pos - i));
			i = ++pos;
			pos = str.find(delim, pos);
		}

		list.push_back(str.substr(i, str.length()));

		return list;
	}
}