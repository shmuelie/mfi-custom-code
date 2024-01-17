#pragma once

#include <stdint.h>

namespace mfi {
	/**
	 * @brief The color/style of the LED on the device.
	*/
	enum class led_color {
		/**
		 * @brief LED is off.
		*/
		off,
		/**
		 * @brief LED is solid blue.
		*/
		blue,
		/**
		 * @brief LED is solid yellow/orange.
		*/
		yellow,
		/**
		 * @brief LED is both blue and yellow/orange.
		*/
		both,
		/**
		 * @brief LED alternatives between blue and yellow/orange.
		*/
		alternate
	};

	/**
	 * @brief Represents the LED on an mFi device.
	*/
	class led {
	public:
		/**
		 * @brief Initializes a new instance of the mfi::led class.
		*/
		explicit led() noexcept;

		/**
		 * @brief Gets the color/style of the LED.
		*/
		led_color color() const;
		/**
		 * @brief Sets the color/style of the LED.
		 * @param value The color/style to set.
		*/
		void color(led_color value) const;
		/**
		 * @brief Gets the frequency the LED will alternate between blue and yellow/orange when led::color() is led_color::alternate.
		*/
		uint32_t frequency() const;
		/**
		 * @brief Sets the frequency the LED will alternate between blue and yellow/orange when led::color() is led_color::alternate.
		 * @param value The frequency the LED will alternate between blue and yellow/orange when led::color() is led_color::alternate.
		*/
		void frequency(uint32_t value) const;
	};
}