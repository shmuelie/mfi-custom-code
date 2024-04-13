#pragma once

#include <string>

namespace ha {
	enum class discoverable_domain {
		sensor,
		button,
		light,
		lock,
		siren,
		_switch
	};
}

namespace std {
	inline std::string to_string(ha::discoverable_domain domain) {
		switch (domain) {
		case ha::discoverable_domain::button:
			return "button";
		case ha::discoverable_domain::light:
			return "light";
		case ha::discoverable_domain::lock:
			return "lock";
		case ha::discoverable_domain::siren:
			return "siren";
		case ha::discoverable_domain::_switch:
			return "switch";
		}
		return "sensor";
	}
}