#include <catch2/catch_all.hpp>
#include "mfi_update/background_updater.h"

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <signal.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace mfi_update;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
	class process_directory final {
	public:
		process_directory() {
			std::string pattern = (fs::temp_directory_path() / "mfi-process-XXXXXX").string();
			auto created = ::mkdtemp(pattern.data());
			if (!created) throw std::runtime_error("temporary process directory failed");
			path = created;
		}
		~process_directory() { fs::remove_all(path); }
		fs::path file(std::string const& name) const { return path / name; }
		std::string script(std::string const& body, std::string const& name = "fake-wget") const {
			auto destination = file(name);
			std::ofstream{destination} << "#!/bin/sh\n" << body;
			if (::chmod(destination.c_str(), 0700) != 0) throw std::runtime_error("chmod script failed");
			return destination.string();
		}
		fs::path path;
	};

	class test_child final {
	public:
		explicit test_child(std::function<int()> const& body) {
			pid = ::fork();
			if (pid < 0) throw std::runtime_error("test fork failed");
			if (pid == 0) ::_exit(body());
		}
		~test_child() {
			if (pid > 0) {
				::kill(pid, SIGKILL);
				int status{};
				while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			}
		}
		int wait() {
			auto deadline = preparation_context::clock::now() + 5s;
			int status{};
			while (preparation_context::clock::now() < deadline) {
				if (::waitpid(pid, &status, WNOHANG) == pid) {
					pid = -1;
					return status;
				}
				std::this_thread::sleep_for(1ms);
			}
			throw std::runtime_error("test child timed out");
		}
		pid_t pid{-1};
	};

	bool wait_file(fs::path const& path) {
		auto deadline = preparation_context::clock::now() + 2s;
		while (!fs::exists(path) && preparation_context::clock::now() < deadline)
			std::this_thread::sleep_for(1ms);
		return fs::exists(path);
	}

	std::size_t descriptor_count() {
		return static_cast<std::size_t>(std::distance(fs::directory_iterator{"/proc/self/fd"}, fs::directory_iterator{}));
	}

	void check_reaped(fs::path const& pid_path) {
		pid_t pid{};
		std::ifstream{pid_path} >> pid;
		REQUIRE(pid > 0);
		errno = 0;
		int status{};
		CHECK(::waitpid(pid, &status, WNOHANG) == -1);
		CHECK(errno == ECHILD);
		CHECK(::kill(pid, 0) == -1);
		CHECK(errno == ESRCH);
	}

	updater process_update(process_directory const& dir, std::string const& executable) {
		updater up{"mfi-cli", semver{1, 0, 0}, config{}};
		up.set_target_path(dir.file("target").string());
		up.set_downloader(downloader{downloader_kind::wget, config{}, executable});
		return up;
	}

	volatile sig_atomic_t terminated = 0;
	void mark_terminated(int) { terminated = 1; }

	void install_termination_handler() {
		terminated = 0;
		struct sigaction action{};
		action.sa_handler = mark_terminated;
		::sigemptyset(&action.sa_mask);
		if (::sigaction(SIGTERM, &action, nullptr) != 0) ::_exit(90);
	}
}

TEST_CASE("update process: actual downloader bounds silent and closed stdout",
	"[update][process]") {
	process_directory dir;
	auto body = "echo $$ > '" + dir.file("pid").string() + "'\n";
	SECTION("silent stdout stays open") { body += "sleep 30\n"; }
	SECTION("stdout closes but child stays alive") { body += "exec 1>&-\nsleep 30\n"; }
	SECTION("TERM is ignored") { body += "trap '' TERM\nsleep 30\n"; }
	downloader dl{downloader_kind::wget, config{}, dir.script(body)};
	auto descriptors = descriptor_count();
	auto start = preparation_context::clock::now();
	CHECK_FALSE(dl.fetch_to_string("unused", preparation_context{{100ms, 300ms}}));
	auto elapsed = preparation_context::clock::now() - start;
	CHECK(elapsed >= 80ms);
	CHECK(elapsed < 650ms);
	check_reaped(dir.file("pid"));
	CHECK(descriptor_count() == descriptors);
}

TEST_CASE("update process: cancellation interrupts reads and child waits without touching unrelated child",
	"[update][process]") {
	process_directory dir;
	test_child unrelated{[] { ::sleep(30); return 0; }};
	auto body = "echo $$ > '" + dir.file("pid").string() + "'\ntrap '' TERM\n";
	SECTION("read") { body += "sleep 30\n"; }
	SECTION("wait") { body += "exec 1>&-\nsleep 30\n"; }
	downloader dl{downloader_kind::wget, config{}, dir.script(body)};
	std::atomic_bool cancellation{false};
	std::thread canceller{[&] {
		if (wait_file(dir.file("pid"))) cancellation = true;
	}};
	auto start = preparation_context::clock::now();
	auto result = dl.fetch_to_string("unused", preparation_context{{120s, 300ms}, &cancellation});
	canceller.join();
	CHECK_FALSE(result);
	CHECK(preparation_context::clock::now() - start < 650ms);
	check_reaped(dir.file("pid"));
	CHECK(::kill(unrelated.pid, 0) == 0);
}

TEST_CASE("update process: all metadata download and validation share one deadline",
	"[update][process]") {
	process_directory dir;
	auto script = dir.script(
		"echo $$ >> '" + dir.file("pids").string() + "'\n"
		"out=''\n"
		"while [ \"$#\" -gt 0 ]; do\n"
		"  if [ \"$1\" = '-O' ]; then shift; out=\"$1\"; fi\n"
		"  url=\"$1\"\n"
		"  shift\n"
		"done\n"
		"case \"$url\" in\n"
		"  *matching-refs*) sleep 0.10; printf '%s' '[{\"ref\":\"refs/tags/mfi-cli/v2.0.0\"}]';;\n"
		"  *releases/tags*) sleep 0.10; printf '%s' '{\"assets\":[{\"name\":\"mfi-cli\",\"browser_download_url\":\"asset\"}]}';;\n"
		"  asset) printf '\\177ELFpartial' > \"$out\"; echo \"$out\" > '" + dir.file("partial").string() +
		"'; sleep 0.20;;\n"
		"esac\n");
	auto up = process_update(dir, script);
	auto start = preparation_context::clock::now();
	auto result = up.prepare(preparation_context{{330ms, 200ms}});
	CHECK(result.result == update_result::timed_out);
	CHECK_FALSE(result.artifact);
	CHECK(preparation_context::clock::now() - start < 700ms);
	REQUIRE(fs::exists(dir.file("partial")));
	std::string partial;
	std::getline(std::ifstream{dir.file("partial")}, partial);
	CHECK_FALSE(fs::exists(partial));
	std::ifstream pids{dir.file("pids")};
	pid_t pid{};
	int count = 0;
	while (pids >> pid) {
		++count;
		int status{};
		CHECK(::waitpid(pid, &status, WNOHANG) == -1);
		CHECK(errno == ECHILD);
	}
	CHECK(count == 3);
}

TEST_CASE("update process: actual failed download removes partial file", "[update][process]") {
	process_directory dir;
	auto script = dir.script(
		"while [ \"$#\" -gt 0 ]; do\n"
		"  if [ \"$1\" = '-O' ]; then shift; printf 'partial' > \"$1\"; fi\n"
		"  shift\n"
		"done\nexit 1\n");
	downloader dl{downloader_kind::wget, config{}, script};
	CHECK_FALSE(dl.fetch_to_file("unused", dir.file("partial").string()));
	CHECK_FALSE(fs::exists(dir.file("partial")));
}

TEST_CASE("update process: metadata memory and inherited closed descriptors are bounded",
	"[update][process]") {
	process_directory dir;
	SECTION("oversized metadata is rejected") {
		auto script = dir.script("echo $$ > '" + dir.file("pid").string() +
			"'\nhead -c 1048577 /dev/zero\nsleep 30\n");
		downloader dl{downloader_kind::wget, config{}, script};
		auto start = preparation_context::clock::now();
		CHECK_FALSE(dl.fetch_to_string("unused", preparation_context{{2s, 200ms}}));
		CHECK(preparation_context::clock::now() - start < 1s);
		check_reaped(dir.file("pid"));
	}
	SECTION("closed parent standard descriptors") {
		auto script = dir.script("printf bounded\n");
		test_child child{[&] {
			::close(STDIN_FILENO);
			::close(STDOUT_FILENO);
			::close(STDERR_FILENO);
			downloader dl{downloader_kind::wget, config{}, script};
			auto result = dl.fetch_to_string("unused", preparation_context{{1s, 200ms}});
			return result == "bounded" ? 0 : 99;
		}};
		auto status = child.wait();
		REQUIRE(WIFEXITED(status));
		CHECK(WEXITSTATUS(status) == 0);
	}
}

TEST_CASE("update process: SIGTERM during background downloader cancels and reaps",
	"[update][process][signal]") {
	process_directory dir;
	auto script = dir.script("echo $$ > '" + dir.file("pid").string() + "'\ntrap '' TERM\nsleep 30\n");
	test_child child{[&] {
		install_termination_handler();
		auto up = process_update(dir, script);
		up.set_apply([](std::string const&, std::vector<std::string> const&) { ::_exit(91); return false; });
		background_updater background{std::move(up), 0, {"unused"}, {120s, 300ms}};
		background.tick();
		background.tick();
		while (!terminated) {
			background.tick();
			std::this_thread::sleep_for(1ms);
		}
		background.request_stop();
		background.stop();
		pid_t downloader_pid{};
		std::ifstream{dir.file("pid")} >> downloader_pid;
		int status{};
		if (::waitpid(downloader_pid, &status, WNOHANG) != -1 || errno != ECHILD) return 92;
		return background.apply_ready([] { return terminated != 0; }) == update_result::cancelled ? 0 : 93;
	}};
	REQUIRE(wait_file(dir.file("pid")));
	auto start = preparation_context::clock::now();
	REQUIRE(::kill(child.pid, SIGTERM) == 0);
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
	CHECK(preparation_context::clock::now() - start < 700ms);
}

TEST_CASE("update process: real exec is restricted to disposable marker process", "[update][process][exec]") {
	process_directory dir;
	fs::copy_file("/bin/sh", dir.file("new"));
	std::ofstream{dir.file("target")} << "old";
	test_child child{[&] {
		install_termination_handler();
		auto target = dir.file("target").string();
		auto command = "printf executed > '" + dir.file("marker").string() + "'";
		auto result = replace_and_reexec(dir.file("new").string(), target, {target, "-c", command}, [] { return false; });
		return result == update_result::updated ? 94 : 95;
	}};
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
	CHECK(fs::exists(dir.file("marker")));
	CHECK_FALSE(fs::exists(dir.file("new")));
}

TEST_CASE("update process: exec does not inherit blocked termination signals",
	"[update][process][signal][exec]") {
	process_directory dir;
	fs::copy_file("/bin/sh", dir.file("new"));
	test_child child{[&] {
		install_termination_handler();
		sigset_t blocked;
		::sigemptyset(&blocked);
		::sigaddset(&blocked, SIGTERM);
		::sigprocmask(SIG_BLOCK, &blocked, nullptr);
		auto target = dir.file("target").string();
		auto command = "kill -TERM $$; printf survived > '" + dir.file("marker").string() + "'";
		replace_and_reexec(dir.file("new").string(), target, {target, "-c", command}, [] { return false; });
		return 95;
	}};
	auto status = child.wait();
	REQUIRE(WIFSIGNALED(status));
	CHECK(WTERMSIG(status) == SIGTERM);
	CHECK_FALSE(fs::exists(dir.file("marker")));
}

TEST_CASE("update process: validation rejects FIFOs without blocking", "[update][process]") {
	process_directory dir;
	REQUIRE(::mkfifo(dir.file("fifo").c_str(), 0600) == 0);
	auto start = preparation_context::clock::now();
	CHECK_FALSE(is_valid_elf(dir.file("fifo").string()));
	CHECK(preparation_context::clock::now() - start < 100ms);
}

TEST_CASE("update process: SIGTERM racing a prepared result discards it without exec",
	"[update][process][signal]") {
	process_directory dir;
	auto script = dir.script(
		"while [ \"$#\" -gt 0 ]; do\n"
		"  if [ \"$1\" = '-O' ]; then shift; out=\"$1\"; fi\n"
		"  url=\"$1\"; shift\n"
		"done\n"
		"case \"$url\" in\n"
		"  *matching-refs*) printf '%s' '[{\"ref\":\"refs/tags/mfi-cli/v2.0.0\"}]';;\n"
		"  *releases/tags*) printf '%s' '{\"assets\":[{\"name\":\"mfi-cli\",\"browser_download_url\":\"asset\"}]}';;\n"
		"  asset) printf '\\177ELFready' > \"$out\"; echo \"$out\" > '" + dir.file("partial").string() + "';;\n"
		"esac\n");
	test_child child{[&] {
		install_termination_handler();
		auto up = process_update(dir, script);
		up.set_apply([](std::string const&, std::vector<std::string> const&) { ::_exit(91); return false; });
		background_updater background{std::move(up), 0, {"unused"}, {2s, 200ms}};
		background.tick();
		while (!terminated) {
			auto result = background.tick();
			if (result == update_result::ready) std::ofstream{dir.file("ready")} << "ready";
			std::this_thread::sleep_for(1ms);
		}
		background.request_stop();
		auto result = background.apply_ready([] { return terminated != 0; });
		background.stop();
		return result == update_result::cancelled ? 0 : 93;
	}};
	REQUIRE(wait_file(dir.file("ready")));
	REQUIRE(::kill(child.pid, SIGTERM) == 0);
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
	std::string partial;
	std::getline(std::ifstream{dir.file("partial")}, partial);
	CHECK_FALSE(fs::exists(partial));
	CHECK_FALSE(fs::exists(dir.file("target")));
}

TEST_CASE("update process: SIGTERM at pre and post replacement boundaries suppresses exec",
	"[update][process][signal][exec]") {
	process_directory dir;
	fs::copy_file("/bin/sh", dir.file("new"));
	std::ofstream{dir.file("target")} << "old";
	bool after_replacement = false;
	SECTION("before replacement") {}
	SECTION("after replacement") { after_replacement = true; }
	test_child child{[&] {
		install_termination_handler();
		int checks = 0;
		auto target = dir.file("target").string();
		auto command = "printf executed > '" + dir.file("marker").string() + "'";
		auto result = replace_and_reexec(dir.file("new").string(), target, {target, "-c", command}, [&] {
			++checks;
			if (checks == (after_replacement ? 3 : 2)) ::kill(::getpid(), SIGTERM);
			return terminated != 0;
		});
		if (!terminated) return 96;
		auto expected = after_replacement ? update_result::replaced_not_restarted : update_result::cancelled;
		return result == expected ? 0 : 97;
	}};
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
	CHECK_FALSE(fs::exists(dir.file("marker")));
	CHECK(fs::exists(dir.file("new")) == !after_replacement);
	CHECK(is_valid_elf(dir.file("target").string()) == after_replacement);
}

TEST_CASE("update process: failed exec restores signal handling and reports replacement",
	"[update][process][exec]") {
	process_directory dir;
	std::ofstream{dir.file("new")} << "\x7f" "ELF" "invalid";
	::chmod(dir.file("new").c_str(), 0700);
	test_child child{[&] {
		install_termination_handler();
		auto target = dir.file("target").string();
		auto result = replace_and_reexec(dir.file("new").string(), target, {target}, [] { return false; });
		::raise(SIGTERM);
		return result == update_result::replaced_not_restarted && terminated ? 0 : 98;
	}};
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("update process: explicit cancellation distinguishes pre and post rename",
	"[update][process][exec]") {
	process_directory dir;
	fs::copy_file("/bin/sh", dir.file("new"));
	std::ofstream{dir.file("target")} << "old";
	bool after_replacement = false;
	SECTION("before replacement") {}
	SECTION("after replacement") { after_replacement = true; }
	test_child child{[&] {
		int checks = 0;
		auto target = dir.file("target").string();
		auto command = "printf executed > '" + dir.file("marker").string() + "'";
		auto result = replace_and_reexec(dir.file("new").string(), target, {target, "-c", command}, [&] {
			return ++checks == (after_replacement ? 3 : 2);
		});
		auto expected = after_replacement ? update_result::replaced_not_restarted : update_result::cancelled;
		return result == expected ? 0 : 97;
	}};
	auto status = child.wait();
	REQUIRE(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);
	CHECK_FALSE(fs::exists(dir.file("marker")));
	CHECK(is_valid_elf(dir.file("target").string()) == after_replacement);
}
