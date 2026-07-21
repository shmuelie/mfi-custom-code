#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include "CivetServer.h"
#include "mfi.h"

/**
 * @brief REST API server for mFi devices, built on civetweb.
 *
 * Exposes the mFi hardware over a small JSON REST API under /api/v2/.
 */
class mfi_http_server {
public:
	/**
	 * @brief Constructs the server.
	 */
	mfi_http_server() noexcept;

	/**
	 * @brief Destroys the server, stopping it and releasing handlers.
	 */
	~mfi_http_server();

	/**
	 * @brief Starts listening on the given host:port.
	 * @param listening_ports civetweb listening_ports value (e.g. "0.0.0.0:8000").
	 * @return true if the server started successfully.
	 */
	bool listen(std::string const& listening_ports);

	/**
	 * @brief A single response: status code, headers, and body.
	 */
	struct response {
		int status_code{ 200 };
		std::map<std::string, std::string> headers{};
		std::string body{};
	};

	response handle_info() noexcept;
	response handle_sensor_list() noexcept;
	response handle_sensor_item(std::string const& method, std::uint8_t sensor_id, std::string const& body) noexcept;
	response handle_led(std::string const& method, std::string const& body) noexcept;

	/**
	 * @brief Number of sensors on the board (used for id validation in handlers).
	 */
	std::size_t sensor_count() const noexcept;

private:
	class info_handler;
	class sensor_list_handler;
	class sensor_item_handler;
	class led_handler;

	mfi::board _board{};
	std::unique_ptr<CivetServer> _server{};
	std::unique_ptr<info_handler> _info_handler{};
	std::unique_ptr<sensor_list_handler> _sensor_list_handler{};
	std::unique_ptr<sensor_item_handler> _sensor_item_handler{};
	std::unique_ptr<led_handler> _led_handler{};
};
