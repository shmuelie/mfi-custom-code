#include <iostream>
#include "mfi_server.h"
#include "options.h"

using namespace std::chrono_literals;

static const char USAGE[] =
R"(mFi HTTP REST SERVER.

  Usage:
	mfi-server [options]
	mfi-server (-h | --help)
	mfi-server --version

  Options:
	-h --help                  Show this screen.
	--version                  Show version.
	-i, --ip IP_ADDRESS        Use IP_ADDRESS to listen on [default: 0.0.0.0].
	-p, --port PORT            Use PORT to listen on [default: 8000].
	-l, --log-level LOG_LEVEL  Logging level 0-4 [default: 0].
)";

int main(int argc, char* argv[]) {
	options ops{ USAGE, { argv + 1, argv + argc } };

	if (ops.help())
	{
		std::cout << USAGE << std::endl;
		return 0;
	}

	if (ops.version()) {
		std::cout << "mFi HTTP REST SERVER 1.0.0.0" << std::endl;
		return 0;
	}

	if (ops.errors().size() > 0) {
		for (auto const& error : ops.errors()) {
			std::cout << error << std::endl;
		}
		std::cout << USAGE << std::endl;
		return -1;
	}

	mg_log_set(ops.log_level());

	mfi_server server{};
	server.listen("http://" + ops.ip() + ":" + std::to_string(ops.port()));
	for (;;) {
		server.poll(1000s);
	}
}