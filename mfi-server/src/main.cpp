#include "mfi_server.h"

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
	mfi_server server{};
	server.listen("http://0.0.0.0:8000");
	for (;;) {
		server.poll(1000s);
	}
}