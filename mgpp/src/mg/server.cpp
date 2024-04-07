#include "mg/server.h"
#include "mg/manager.h"

using namespace std;
using namespace mg;

server::server() noexcept : eventing_handler() {
}

optional<connection> server::listen(const string& url) noexcept {
	return manager()->listen(this, url);
}