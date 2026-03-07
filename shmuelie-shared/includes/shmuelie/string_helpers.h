#pragma once

#include <string>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <vector>

namespace shmuelie {
	/**
	 * @brief Joins a range of strings with a separator and optional concluder.
	 * @tparam InputIt Iterator type for the string range.
	 * @param first Iterator to the first element.
	 * @param last Iterator past the last element.
	 * @param separator String inserted between elements.
	 * @param concluder String appended after the last element (returned alone if range is empty).
	 * @return The joined string.
	 */
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

	/**
	 * @brief Non-throwing conversion of a string to an unsigned integer.
	 * @tparam NumberType The target unsigned integer type.
	 * @param __str The string to convert.
	 * @param __idx If non-null, receives the index of the first unconverted character.
	 * @param __base The numeric base (default 10).
	 * @return The converted value, or std::nullopt on failure.
	 */
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

	/**
	 * @brief Non-throwing conversion of a string to a signed integer.
	 * @tparam NumberType The target signed integer type.
	 * @param __str The string to convert.
	 * @param __idx If non-null, receives the index of the first unconverted character.
	 * @param __base The numeric base (default 10).
	 * @return The converted value, or std::nullopt on failure.
	 */
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

	/**
	 * @brief Checks whether a string contains only digit characters.
	 * @param str The string to check.
	 * @return true if the string is non-empty and contains only digits.
	 */
	bool is_number(std::string const& str);

	/**
	 * @brief Splits a string by a single-character delimiter.
	 * @param str The string to split.
	 * @param delim The delimiter character.
	 * @return A vector of substrings. Empty segments are preserved.
	 */
	std::vector<std::string> split(std::string const& str, char delim);
}