#include "mfi_server.h"
#include "string_helpers.h"

using namespace std::chrono_literals;

const std::string defaultIp = "0.0.0.0";
const std::uint16_t defaultPort = 8000;

static bool validate_ip(const std::string& ip)
{
	// split the string into tokens
	std::vector<std::string> list = string_helpers::split(ip, '.');

	// if the token size is not equal to four
	if (list.size() != 4) {
		return false;
	}

	// validate each token
	for (std::string str : list)
	{
		// verify that the string is a number or not, and the numbers
		// are in the valid range
		if (!string_helpers::is_number(str) || std::stoi(str) > 255 || std::stoi(str) < 0) {
			return false;
		}
	}

	return true;
}

class options {
public:
	explicit options(int argc, char* argv[]) noexcept {
		for (int i = 1; i < argc; i++) {
			std::string arg{ argv[i] };
			if (arg == "--help" || arg == "-h") {
				_help = true;
				return;
			}

			if (arg == "--ip" || arg == "-i") {
				if (++i >= argc) {
					_help = true;
					return;
				}
				std::string ip{ argv[i] };
				if (!validate_ip(ip)) {
					_help = true;
					return;
				}
				_ip = ip;
			}
			else if (arg == "--port" || arg == "-p") {
				if (++i >= argc) {
					_help = true;
					return;
				}
				auto potentialPort = string_helpers::try_stoul<uint16_t>(std::string{ argv[i] });
				if (!potentialPort) {
					_help = true;
					return;
				}
				_port = potentialPort.value();
			}
			else if (arg == "--log-level" || arg == "-l") {
				if (++i >= argc) {
					_help = true;
					return;
				}
				auto potentialLogLevel = string_helpers::try_stoul<uint8_t>(std::string{ argv[i] });
				if (!potentialLogLevel) {
					_help = true;
					return;
				}
				_logLevel = potentialLogLevel.value();
			}
		}
	}

	const std::string ip() const noexcept {
		return _ip;
	}

	std::uint16_t port() const noexcept {
		return _port;
	}

	std::uint8_t log_level() const noexcept {
		return _logLevel;
	}

	bool help() const noexcept {
		return _help;
	}

private:
	std::string _ip{ "0.0.0.0" };
	std::uint16_t _port = 8000;
	std::uint8_t _logLevel = MG_LL_NONE;
	bool _help = false;
};

int main(int argc, char* argv[]) {
	options o{ argc, argv };

	if (o.help()) {
		std::cout << "mfi-server [--ip|-i IP_ADDRESS] [--port|-p PORT] [--log-level|-l LOG_LEVEL]" << std::endl;
		return 0;
	}

	mg_log_set(o.log_level());

	mfi_server server{};
	server.listen("http://" + o.ip() + ":" + std::to_string(o.port()));
	for (;;) {
		server.poll(1000s);
	}
}