#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mfi_mqtt_client {
	bool valid_uuid(std::string_view value) noexcept;
	std::string random_uuid();
	std::optional<std::string> configured_device_id(std::string const& path);
	std::string initialize_device_id(std::string const& path);
	std::string select_device_id(std::optional<std::string> const& configured,
		std::string const& requested);
}
