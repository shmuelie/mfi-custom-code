#include "mfi_server.h"

using namespace std;
using namespace mg;
using namespace mfi;

mfi_server::mfi_server() noexcept : http_server() {
}

void mfi_server::http_handler(const connection& connection, const http_message& message) noexcept {
	if (message.match_uri("/api/v2/status")) {

	}
}