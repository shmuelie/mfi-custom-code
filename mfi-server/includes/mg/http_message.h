#pragma once

#include <string>
#include <vector>
#include "mongoose.h"
#include "mg/strings.h"

namespace mg {
	class http_message {
	public:
		http_message(mg_http_message* message) noexcept;

		bool match_uri(const std::string& glob) const noexcept;

		template<std::size_t _Nm>
		bool match_uri(const std::string& glob, std::vector<std::string>& captures) const noexcept {
			mg_str mgCaptures[_Nm + 1]{};
			bool result = mg_match(_message->uri, mg_str_n(glob.c_str(), glob.length()), mgCaptures);
			if (result) {
				for (std::size_t i = 0; i < _Nm; i++) {
					mg_str mgCapture = mgCaptures[i];
					if (mgCapture.len == 0) {
						break;
					}
					captures.push_back(std::to_string(mgCapture));
				}
			}
			return result;
		}

		const std::string method() const noexcept;
		const std::string body() const noexcept;
	private:
		mg_http_message* _message;
	};
}