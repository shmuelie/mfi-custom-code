#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "mfi/board.h"
#include "mfi/sensor.h"
#include "mfi/led.h"

namespace mfi_cli {
	/**
	 * @brief Finds a sensor on the board by its ID.
	 * @param board The board to search.
	 * @param id The sensor ID to look for.
	 * @return The matching sensor, or std::nullopt if no sensor has that ID.
	 * @remarks CLI11-free so it can be unit tested without argument parsing.
	 */
	inline std::optional<mfi::sensor> find_sensor(mfi::board const& board, uint8_t id) {
		for (auto const& s : board.sensors()) {
			if (s.id() == id) {
				return s;
			}
		}
		return std::nullopt;
	}

	/**
	 * @brief Converts an mfi::led_color to its lowercase string name.
	 * @param color The color to format.
	 * @return The string name (e.g. "alternate").
	 */
	inline std::string_view format_led_color(mfi::led_color color) noexcept {
		switch (color) {
		case mfi::led_color::off:
			return "off";
		case mfi::led_color::blue:
			return "blue";
		case mfi::led_color::yellow:
			return "yellow";
		case mfi::led_color::both:
			return "both";
		case mfi::led_color::alternate:
			return "alternate";
		}
		return "off";
	}

	/**
	 * @brief Parses a string into an mfi::led_color.
	 * @param value The string to parse (case-sensitive lowercase).
	 * @return The parsed color, or std::nullopt if the string is not a valid color.
	 */
	inline std::optional<mfi::led_color> parse_led_color(std::string_view value) noexcept {
		if (value == "off") {
			return mfi::led_color::off;
		}
		if (value == "blue") {
			return mfi::led_color::blue;
		}
		if (value == "yellow") {
			return mfi::led_color::yellow;
		}
		if (value == "both") {
			return mfi::led_color::both;
		}
		if (value == "alternate") {
			return mfi::led_color::alternate;
		}
		return std::nullopt;
	}
}
