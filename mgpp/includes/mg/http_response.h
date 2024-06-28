#pragma once

#include <string>
#include <map>

namespace mg {
	class http_response {
	public:
		http_response(int status_code) noexcept;
		http_response(std::string const& body) noexcept;
		http_response(int status_code, std::string const& body) noexcept;
		http_response(int status_code, std::map<std::string, std::string> const& headers) noexcept;
		http_response(std::map<std::string, std::string> const& headers, std::string const& body) noexcept;
		http_response(int status_code, std::map<std::string, std::string> const& headers, std::string const& body) noexcept;

		int status_code() const noexcept;
		std::map<std::string, std::string> const& headers() const noexcept;
		std::string const& body() const noexcept;
	private:
		int _status_code;
		std::map<std::string, std::string> _headers;
		std::string _body;
	};
}