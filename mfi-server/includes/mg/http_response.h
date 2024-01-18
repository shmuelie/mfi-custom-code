#pragma once

#include <string>
#include <map>

namespace mg {
	class http_response {
	public:
		http_response(int status_code) noexcept;
		http_response(const std::string& body) noexcept;
		http_response(int status_code, const std::string& body) noexcept;
		http_response(int status_code, const std::map<std::string, std::string>& headers) noexcept;
		http_response(const std::map<std::string, std::string>& headers, const std::string& body) noexcept;
		http_response(int status_code, const std::map<std::string, std::string>& headers, const std::string& body) noexcept;

		int status_code() const noexcept;
		const std::map<std::string, std::string>& headers() const noexcept;
		const std::string& body() const noexcept;
	private:
		int _status_code;
		std::map<std::string, std::string> _headers;
		std::string _body;
	};
}