#pragma once

#include <stdint.h>

namespace mfi {
	enum class led_color {
		off,
		blue,
		yellow,
		both,
		alternate
	};

	class led {
	public:
		explicit led() noexcept;

		led_color color() const;
		void color(led_color value) const;
		uint32_t frequency() const;
		void frequency(uint32_t value) const;
	};
}