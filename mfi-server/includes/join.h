#pragma once

#include <string>
#include <sstream>

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