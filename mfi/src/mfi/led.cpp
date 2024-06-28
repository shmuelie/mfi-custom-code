#include "mfi/led.h"

#include <iostream>
#include <fstream>
#include <exception>

using namespace std;
using namespace mfi;

string const root{ "/proc/led/" };
string const status_path{ root + "status" };
string const frequency_path{ root + "freq" };

led::led() noexcept {
}

led_color led::color() const {
	ifstream stream{ status_path };
	string status;
	getline(stream, status);

	if (status.starts_with("0 ")) {
		return led_color::off;
	}
	else if (status.starts_with("1 ")) {
		return led_color::blue;
	}
	else if (status.starts_with("2 ")) {
		return led_color::yellow;
	}
	else if (status.starts_with("3 ")) {
		return led_color::both;
	}
	else if (status.starts_with("4 ")) {
		return led_color::alternate;
	}

	throw runtime_error{ "Unknown or supported LED color" };
}

void led::color(led_color value) const {
	ofstream stream{ status_path, ios::out };
	stream << static_cast<int>(value);
}

uint32_t led::frequency() const {
	ifstream stream{ frequency_path };
	uint32_t value;
	stream >> value;
	return value;
}

void led::frequency(uint32_t value) const {
	ofstream stream{ frequency_path, ios::out };
	stream << value;
}