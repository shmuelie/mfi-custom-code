#include "mfi_update/config.h"
#include <cstdlib>

namespace mfi_update {

	namespace {
		const char* default_getenv(const char* name) {
			return std::getenv(name);
		}
	}

	config config::resolve(const char* (*getenv_fn)(const char*)) const {
		if (getenv_fn == nullptr) {
			getenv_fn = &default_getenv;
		}
		config result = *this;

		if (result.proxy.empty()) {
			const char* https = getenv_fn("https_proxy");
			if (https == nullptr || https[0] == '\0') {
				https = getenv_fn("http_proxy");
			}
			if (https != nullptr && https[0] != '\0') {
				result.proxy = https;
			}
		}

		if (!result.proxy.empty()) {
			result.use_proxy = true;
		}

		return result;
	}
}
