#include "mfi_update/downloader.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace mfi_update {

	downloader::downloader(downloader_kind kind, config cfg) noexcept
		: _kind(kind), _config(std::move(cfg)) {
	}

	downloader::downloader(downloader_kind kind, config cfg, std::string executable)
		: _kind(kind), _config(std::move(cfg)), _executable(std::move(executable)) {
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
		using clock = preparation_context::clock;
		constexpr std::size_t max_metadata_bytes = 1024 * 1024;

		struct descriptor {
			int fd{-1};
			~descriptor() { if (fd >= 0) ::close(fd); }
			descriptor() = default;
			explicit descriptor(int value) : fd(value) {}
			descriptor(descriptor const&) = delete;
			descriptor& operator=(descriptor const&) = delete;
			void reset() noexcept {
				if (fd >= 0) ::close(fd);
				fd = -1;
			}
		};

		std::string executable_path(std::string const& name) {
			if (name.find('/') != std::string::npos)
				return ::access(name.c_str(), X_OK) == 0 ? name : std::string{};
			const char* env = std::getenv("PATH");
			std::string path = env ? env : "/bin:/usr/bin";
			std::size_t begin = 0;
			do {
				auto end = path.find(':', begin);
				auto dir = path.substr(begin, end == std::string::npos ? end : end - begin);
				auto candidate = (dir.empty() ? "." : dir) + "/" + name;
				if (::access(candidate.c_str(), X_OK) == 0) return candidate;
				if (end == std::string::npos) break;
				begin = end + 1;
			} while (begin <= path.size());
			return {};
		}

		int pause_ms(clock::time_point deadline) noexcept {
			auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
			return static_cast<int>(std::clamp(remaining.count(), std::int64_t{0}, std::int64_t{20}));
		}

		class owned_child final {
		public:
			owned_child(pid_t pid, preparation_context const& context) noexcept
				: _pid(pid), _context(context) {}
			~owned_child() { cleanup(); }

			// Keep the zombie until pipes are drained, preventing PID/group reuse.
			int exited() noexcept {
				siginfo_t info{};
				if (::waitid(P_PID, static_cast<id_t>(_pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0)
					return info.si_pid == _pid ? 1 : 0;
				if (errno == EINTR) return 0;
				if (errno == ECHILD) _pid = -1;
				return -1;
			}

			bool reap() noexcept {
				// A downloader must not leave detached descendants behind.
				signal_group(SIGKILL);
				int status{};
				pid_t result;
				do { result = ::waitpid(_pid, &status, WNOHANG); } while (result < 0 && errno == EINTR);
				if (result != _pid) return false;
				_pid = -1;
				return WIFEXITED(status) && WEXITSTATUS(status) == 0;
			}

		private:
			void signal_group(int signal) noexcept {
				if (::kill(-_pid, signal) < 0 && errno == ESRCH) ::kill(_pid, signal);
			}
			void cleanup() noexcept {
				if (_pid <= 0) return;
				auto allowance = _context.cleanup_allowance();
				auto deadline = clock::now() + allowance;
				auto grace = clock::now() + std::min(allowance / 2, std::chrono::milliseconds{250});
				signal_group(SIGTERM);
				while (clock::now() < grace && exited() == 0)
					::poll(nullptr, 0, pause_ms(grace));
				if (_pid <= 0) return;
				signal_group(SIGKILL);
				while (clock::now() < deadline) {
					int status{};
					auto result = ::waitpid(_pid, &status, WNOHANG);
					if (result == _pid || (result < 0 && errno == ECHILD)) {
						_pid = -1;
						return;
					}
					if (result < 0 && errno != EINTR) break;
					::poll(nullptr, 0, pause_ms(deadline));
				}
				// SIGKILL cannot bound a kernel uninterruptible sleep.
				_context.mark_cleanup_failed();
				std::fputs("update child could not be reaped within cleanup deadline\n", stderr);
			}
			pid_t _pid;
			preparation_context const& _context;
		};

		std::optional<std::string> run(std::vector<std::string> const& argv,
			std::string const& executable, bool capture, preparation_context const& context) {
			if (context.interrupted()) return std::nullopt;
			auto program = executable_path(executable.empty() ? argv.front() : executable);
			if (program.empty()) return std::nullopt;
			std::vector<char*> args;
			for (auto const& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
			args.push_back(nullptr);
			// Copy environment and prepare every C pointer before forking.
			std::vector<std::string> environment;
			for (auto entry = environ; entry && *entry; ++entry) environment.emplace_back(*entry);
			std::vector<char*> env;
			for (auto& entry : environment) env.push_back(entry.data());
			env.push_back(nullptr);
			descriptor input;
			descriptor output;
			if (capture) {
				int pipe_fds[2];
				if (::pipe2(pipe_fds, O_CLOEXEC) != 0) return std::nullopt;
				input.fd = pipe_fds[0];
				output.fd = pipe_fds[1];
				if (::fcntl(input.fd, F_SETFL, O_NONBLOCK) < 0) return std::nullopt;
			}
			descriptor null{::open("/dev/null", O_RDWR | O_CLOEXEC)};
			if (null.fd < 0) return std::nullopt;
			// Keep redirection sources away from stdio even when the embedding
			// process started with closed descriptors.
			for (auto* fd : {&input, &output, &null}) {
				if (fd->fd >= 0 && fd->fd <= STDERR_FILENO) {
					auto duplicate = ::fcntl(fd->fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
					if (duplicate < 0) return std::nullopt;
					fd->reset();
					fd->fd = duplicate;
				}
			}
			sigset_t child_mask;
			::sigemptyset(&child_mask);
			struct sigaction defaults{};
			defaults.sa_handler = SIG_DFL;
			::sigemptyset(&defaults.sa_mask);
			const char* program_ptr = program.c_str();
			char* const* args_ptr = args.data();
			char* const* env_ptr = env.data();
			const int out_fd = capture ? output.fd : null.fd;
			if (context.interrupted()) return std::nullopt;
			auto pid = ::fork();
			if (pid < 0) return std::nullopt;
			if (pid == 0) {
				// Only async-signal-safe C operations: no PATH lookup, allocation or logging.
				if (::setpgid(0, 0) != 0 ||
					::sigaction(SIGTERM, &defaults, nullptr) != 0 ||
					::sigaction(SIGINT, &defaults, nullptr) != 0 ||
					::sigprocmask(SIG_SETMASK, &child_mask, nullptr) != 0 ||
					::dup2(null.fd, STDIN_FILENO) < 0 ||
					::dup2(out_fd, STDOUT_FILENO) < 0 ||
					::dup2(null.fd, STDERR_FILENO) < 0) ::_exit(127);
				::execve(program_ptr, args_ptr, env_ptr);
				::_exit(127);
			}
			owned_child child{pid, context};
			output.reset();
			std::string body;
			bool eof = !capture;
			std::array<char, 4096> buffer{};
			while (!context.interrupted()) {
				if (!eof) {
					// Limit each drain pass; a continuous producer must not starve cancellation.
					for (int count = 0; count < 16 && !context.interrupted(); ++count) {
						auto n = ::read(input.fd, buffer.data(), buffer.size());
						if (n > 0) {
							if (body.size() + static_cast<std::size_t>(n) > max_metadata_bytes)
								return std::nullopt;
							body.append(buffer.data(), static_cast<std::size_t>(n));
						} else if (n == 0) {
							eof = true;
							input.reset();
							break;
						} else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
							break;
						} else return std::nullopt;
					}
				}
				auto exited = child.exited();
				if (exited < 0) return std::nullopt;
				if (exited && eof) return child.reap() ? std::optional{std::move(body)} : std::nullopt;
				pollfd fd{input.fd, POLLIN | POLLHUP, 0};
				if (::poll(eof ? nullptr : &fd, eof ? 0 : 1, pause_ms(context.deadline())) < 0 && errno != EINTR)
					return std::nullopt;
			}
			return std::nullopt;
		}
	}

	std::optional<std::string> downloader::fetch_to_string(std::string const& url) const {
		return fetch_to_string(url, preparation_context{});
	}

	std::optional<std::string> downloader::fetch_to_string(std::string const& url,
		preparation_context const& context) const {
		return run(build_argv(url, ""), _executable, true, context);
	}

	bool downloader::fetch_to_file(std::string const& url, std::string const& output_path) const {
		return fetch_to_file(url, output_path, preparation_context{});
	}

	bool downloader::fetch_to_file(std::string const& url, std::string const& output_path,
		preparation_context const& context) const {
		struct partial_file {
			std::string const& path;
			bool complete{false};
			~partial_file() {
				if (!complete && ::unlink(path.c_str()) != 0 && errno != ENOENT)
					std::fputs("update partial file cleanup failed\n", stderr);
			}
		} partial{output_path};
		partial.complete = run(build_argv(url, output_path), _executable, false, context).has_value();
		return partial.complete;
	}

	std::optional<downloader_kind> downloader::detect() noexcept {
		if (!executable_path("wget").empty()) return downloader_kind::wget;
		if (!executable_path("curl").empty()) return downloader_kind::curl;
		return std::nullopt;
	}
}
