#include "mfi/config.h"

#include <iostream>
#include <fstream>
#include <filesystem>

using namespace std;
using namespace mfi;

map<string, string> const config::read_all(const string& file) {
	ifstream stream{ file.c_str() };

	if (stream.bad()) {
		throw runtime_error{ "Unable to read file" };
	}

	map<string, string> config{};

	for (string line; getline(stream, line);) {
		auto equalityIndex = line.find('=');
		if (equalityIndex == -1)
		{
			continue;
		}

		auto key = line.substr(0, equalityIndex);
		auto value = line.substr(equalityIndex + 1);

		config.emplace(key, value);
	}

	return config;
}

string const config::read(const string& file, const string& prop) {
	ifstream stream{ file.c_str() };

	if (stream.bad()) {
		throw runtime_error{ "Unable to read file" };
	}

	for (string line; getline(stream, line);) {
		auto equalityIndex = line.find('=');
		if (equalityIndex == -1)
		{
			continue;
		}

		auto key = line.substr(0, equalityIndex);
		auto value = line.substr(equalityIndex + 1);

		if (key == prop) {
			return value;
		}
	}

	return string{};
}

string const config::read(const string& file, const string& prop, const string& defaultValue) {
	ifstream stream{ file.c_str() };

	if (stream.bad()) {
		return defaultValue;
	}

	for (string line; getline(stream, line);) {
		auto equalityIndex = line.find('=');
		if (equalityIndex == -1)
		{
			continue;
		}

		auto key = line.substr(0, equalityIndex);
		auto value = line.substr(equalityIndex + 1);

		if (key == prop) {
			return value;
		}
	}

	return defaultValue;
}