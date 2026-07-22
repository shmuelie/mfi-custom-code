#include "mfi_update/downloader.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace mfi_update {

	downloader::downloader(downloader_kind kind, config cfg) noexcept
		: _kind(kind), _config(std::move(cfg)) {
	}

	std::vector<std::string> downloader::build_argv(std::string const& url, std::string const& output_path) const {
		std::vector<std::string> argv;
		if (_kind == downloader_kind::wget) {
			argv.push_back("wget");
			argv.push_back("-q");
			if (_config.insecure) {
				argv.push_back("--no-check-certificate");
			}
			if (_config.use_proxy && !_config.proxy.empty()) {
				argv.push_back("--execute");
				argv.push_back("use_proxy=yes");
				argv.push_back("--execute");
				argv.push_back("http_proxy=" + _config.proxy);
				argv.push_back("--execute");
				argv.push_back("https_proxy=" + _config.proxy);
			}
			argv.push_back("-O");
			argv.push_back(output_path.empty() ? std::string("-") : output_path);
			argv.push_back(url);
		}
		else { // curl
			argv.push_back("curl");
			argv.push_back("-sSL"); // silent, show errors, follow redirects
			if (_config.insecure) {
				argv.push_back("-k");
			}
			if (_config.use_proxy && !_config.proxy.empty()) {
				argv.push_back("-x");
				argv.push_back(_config.proxy);
			}
			if (!output_path.empty()) {
				argv.push_back("-o");
				argv.push_back(output_path);
			}
			argv.push_back(url);
		}
		return argv;
	}

	namespace {
		// Runs argv via fork/exec, redirecting child stdout to out_fd (or leaving
		// it if out_fd < 0). Returns the child exit code, or -1 on spawn failure.
		int run(std::vector<std::string> const& argv, int out_fd) {
			std::vector<char*> c_argv;
			c_argv.reserve(argv.size() + 1);
			for (auto const& a : argv) {
				c_argv.push_back(const_cast<char*>(a.c_str()));
			}
			c_argv.push_back(nullptr);

			pid_t pid = fork();
			if (pid < 0) {
				return -1;
			}
			if (pid == 0) {
				if (out_fd >= 0) {
					dup2(out_fd, STDOUT_FILENO);
				}
				execvp(c_argv[0], c_argv.data());
				_exit(127); // exec failed
			}
			int status = 0;
			while (waitpid(pid, &status, 0) < 0) {
				// retry on EINTR
			}
			if (WIFEXITED(status)) {
				return WEXITSTATUS(status);
			}
			return -1;
		}
	}

	std::optional<std::string> downloader::fetch_to_string(std::string const& url) const {
		auto argv = build_argv(url, "");
		int fds[2];
		if (pipe(fds) != 0) {
			return std::nullopt;
		}

		std::vector<char*> c_argv;
		c_argv.reserve(argv.size() + 1);
		for (auto const& a : argv) {
			c_argv.push_back(const_cast<char*>(a.c_str()));
		}
		c_argv.push_back(nullptr);

		pid_t pid = fork();
		if (pid < 0) {
			close(fds[0]);
			close(fds[1]);
			return std::nullopt;
		}
		if (pid == 0) {
			close(fds[0]);
			dup2(fds[1], STDOUT_FILENO);
			close(fds[1]);
			execvp(c_argv[0], c_argv.data());
			_exit(127);
		}
		close(fds[1]);

		std::string body;
		std::array<char, 4096> buf{};
		ssize_t n;
		while ((n = read(fds[0], buf.data(), buf.size())) > 0) {
			body.append(buf.data(), static_cast<size_t>(n));
		}
		close(fds[0]);

		int status = 0;
		while (waitpid(pid, &status, 0) < 0) {
			// retry on EINTR
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			return std::nullopt;
		}
		return body;
	}

	bool downloader::fetch_to_file(std::string const& url, std::string const& output_path) const {
		auto argv = build_argv(url, output_path);
		return run(argv, -1) == 0;
	}

	std::optional<downloader_kind> downloader::detect() noexcept {
		// Prefer wget (matches mfi-env); fall back to curl.
		if (::access("/usr/bin/wget", X_OK) == 0 || ::access("/bin/wget", X_OK) == 0) {
			return downloader_kind::wget;
		}
		if (::access("/usr/bin/curl", X_OK) == 0 || ::access("/bin/curl", X_OK) == 0) {
			return downloader_kind::curl;
		}
		// Fall back to a PATH search via the shell's command resolution.
		if (std::system("command -v wget >/dev/null 2>&1") == 0) {
			return downloader_kind::wget;
		}
		if (std::system("command -v curl >/dev/null 2>&1") == 0) {
			return downloader_kind::curl;
		}
		return std::nullopt;
	}
}
