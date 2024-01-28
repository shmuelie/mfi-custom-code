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

int main(int argc, char* argv[]) {
	std::string ip = defaultIp;
	std::uint16_t port = defaultPort;

	if (argc == 2) {
		port = string_helpers::try_stoul<uint16_t>(std::string{ argv[1] }, nullptr, 10).value_or(defaultPort);
	}
	else if (argc == 3) {
		std::string potentialIp{ argv[1] };
		if (validate_ip(potentialIp)) {
			ip = potentialIp;
		}
		port = string_helpers::try_stoul<uint16_t>(std::string{ argv[2] }, nullptr, 10).value_or(defaultPort);
	}

	mfi_server server{};
	server.listen("http://" + ip + ":" + std::to_string(port));
	for (;;) {
		server.poll(1000s);
	}
}