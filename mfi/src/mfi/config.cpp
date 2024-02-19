#include "mfi/config.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace mfi;

const map<string, string> config::read_all(const string& file) {
	ifstream stream{ file.c_str() };
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

const string config::read(const string& file, const string& prop) {
	ifstream stream{ file.c_str() };

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