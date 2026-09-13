#include <catch2/catch_all.hpp>
#include "mfi_update/background_updater.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace mfi_update;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
	class update_directory final {
	public:
		update_directory() {
			std::string pattern = (fs::temp_directory_path() / "mfi-background-XXXXXX").string();
			auto created = ::mkdtemp(pattern.data());
			if (!created) throw std::runtime_error("temporary updater directory failed");
			path = created;
		}
		~update_directory() { fs::remove_all(path); }
		fs::path path;
	};

	updater fake_update(fs::path const& target) {
		updater up{"mfi-cli", semver{1, 0, 0}, config{}};
		up.set_target_path(target.string());
		up.set_fetch([](std::string const& url) -> std::optional<std::string> {
			if (url.find("matching-refs") != std::string::npos)
				return R"([{"ref":"refs/tags/mfi-cli/v2.0.0"}])";
			return R"({"assets":[{"name":"mfi-cli","browser_download_url":"unused"}]})";
		});
		up.set_download([](std::string const&, std::string const& path) {
			std::ofstream out{path, std::ios::binary};
			out << "\x7f" "ELF" "test";
			return out.good();
		});
		return up;
	}

	std::optional<update_result> await_result(background_updater& up) {
		auto end = background_updater::clock::now() + 2s;
		do {
			auto result = up.tick();
			if (result) return result;
			std::this_thread::sleep_for(1ms);
		} while (background_updater::clock::now() < end);
		return std::nullopt;
	}

	void launch(background_updater& up) {
		auto now = background_updater::clock::now();
		REQUIRE_FALSE(up.tick(now));
		REQUIRE_FALSE(up.tick(now + 1s));
	}
}

TEST_CASE("background update: factory preserves periodic options", "[update][background]") {
	CHECK_FALSE(make_background_updater(false, 1, "", "", true, "mfi-cli", "1.0.0", {"mfi-cli"}));
	CHECK_FALSE(make_background_updater(true, 1, "", "", true, "mfi-cli", "bad", {"mfi-cli"}));
	CHECK(make_background_updater(true, 1, "owner/name", "", true, "mfi-cli", "mfi-cli 1.0.0", {"mfi-cli"}));
	CHECK(preparation_limits{}.total == 120s);
	CHECK(preparation_limits{}.cleanup == 5s);
}

TEST_CASE("background update: tick stays responsive with one task and main thread apply",
	"[update][background]") {
	update_directory dir;
	std::atomic_int checks{0};
	std::atomic_bool release{false};
	std::atomic_bool entered{false};
	std::atomic_bool wrong_thread{false};
	auto owner = std::this_thread::get_id();
	auto up = fake_update(dir.path / "target");
	up.set_cancellable_fetch([&](std::string const& url, preparation_context const& context)
		-> std::optional<std::string> {
		if (std::this_thread::get_id() == owner) wrong_thread = true;
		if (url.find("matching-refs") != std::string::npos) {
			++checks;
			entered = true;
			while (!release && !context.interrupted()) std::this_thread::sleep_for(1ms);
			return R"([{"ref":"refs/tags/mfi-cli/v2.0.0"}])";
		}
		return R"({"assets":[{"name":"mfi-cli","browser_download_url":"unused"}]})";
	});
	int applications = 0;
	up.set_apply([&](std::string const&, std::vector<std::string> const&) {
		CHECK(std::this_thread::get_id() == owner);
		++applications;
		return true;
	});
	background_updater background{std::move(up), 1, {"mfi-cli"}, {2s, 200ms}};
	launch(background);
	auto wait_end = background_updater::clock::now() + 1s;
	while (!entered && background_updater::clock::now() < wait_end) std::this_thread::sleep_for(1ms);
	REQUIRE(entered);
	CHECK(background.apply_ready([] { return false; }) == update_result::apply_failed);
	auto start = background_updater::clock::now();
	int polls = 0, relay_commands = 0;
	for (int index = 0; index < 1000; ++index) {
		REQUIRE_FALSE(background.tick(start + 100s));
		++polls;
		++relay_commands;
	}
	CHECK(background_updater::clock::now() - start < 250ms);
	CHECK(checks == 1);
	CHECK(polls == 1000);
	CHECK(relay_commands == 1000);
	CHECK(applications == 0);
	release = true;
	REQUIRE(await_result(background) == update_result::ready);
	CHECK_FALSE(background.tick(start + 100s));
	CHECK(background.apply_ready([] { return false; }) == update_result::updated);
	CHECK(applications == 1);
	CHECK_FALSE(wrong_thread);
	CHECK(fs::is_empty(dir.path));
}

TEST_CASE("background update: cancellation discards late result and staged artifact",
	"[update][background]") {
	update_directory dir;
	std::atomic_bool downloaded{false}, release{false};
	std::string staged;
	auto up = fake_update(dir.path / "target");
	up.set_cancellable_download([&](std::string const&, std::string const& path,
		preparation_context const& context) {
		{
			std::ofstream out{path};
			out << "\x7f" "ELF" "late";
		}
		staged = path;
		downloaded.store(true, std::memory_order_release);
		while (!release && !context.interrupted()) std::this_thread::sleep_for(1ms);
		return true;
	});
	bool applied = false;
	up.set_apply([&](std::string const&, std::vector<std::string> const&) { applied = true; return true; });
	background_updater background{std::move(up), 1, {"mfi-cli"}, {2s, 200ms}};
	launch(background);
	auto deadline = background_updater::clock::now() + 1s;
	while (!downloaded.load(std::memory_order_acquire) && background_updater::clock::now() < deadline)
		std::this_thread::sleep_for(1ms);
	REQUIRE(downloaded.load(std::memory_order_acquire));
	background.request_stop();
	release = true;
	CHECK(await_result(background) == update_result::cancelled);
	CHECK_FALSE(fs::exists(staged));
	CHECK(background.apply_ready([] { return false; }) == update_result::cancelled);
	CHECK_FALSE(applied);
	CHECK_FALSE(background.tick(background_updater::clock::now() + 100s));
}

TEST_CASE("background update: cancellation at ready and apply boundary prevents application",
	"[update][background]") {
	update_directory dir;
	bool applied = false;
	auto up = fake_update(dir.path / "target");
	up.set_apply([&](std::string const&, std::vector<std::string> const&) { applied = true; return true; });
	background_updater background{std::move(up), 1, {"mfi-cli"}};
	launch(background);
	REQUIRE(await_result(background) == update_result::ready);
	SECTION("signal flag translated at apply") {
		CHECK(background.apply_ready([] { return true; }) == update_result::cancelled);
	}
	SECTION("request racing completed result") {
		background.request_stop();
		CHECK(background.tick() == update_result::cancelled);
		CHECK(background.apply_ready([] { return false; }) == update_result::cancelled);
	}
	CHECK_FALSE(applied);
	CHECK(fs::is_empty(dir.path));
}

TEST_CASE("background update: exception and destructor own partial files", "[update][background]") {
	update_directory dir;
	auto up = fake_update(dir.path / "target");
	up.set_download([](std::string const&, std::string const& path) -> bool {
		std::ofstream{path} << "partial";
		throw std::runtime_error("not exposed in outcome");
	});
	{
		background_updater background{std::move(up), 1, {"mfi-cli"}};
		launch(background);
		CHECK(await_result(background) == update_result::preparation_failed);
	}
	CHECK(fs::is_empty(dir.path));
	{
		background_updater background{fake_update(dir.path / "target"), 1, {"mfi-cli"}};
		launch(background);
		REQUIRE(await_result(background) == update_result::ready);
		CHECK_FALSE(fs::is_empty(dir.path));
	}
	CHECK(fs::is_empty(dir.path));
}

TEST_CASE("background update: cancellation observed during apply disables future jobs",
	"[update][background]") {
	update_directory dir;
	bool cancelled = false;
	auto up = fake_update(dir.path / "target");
	up.set_apply([&](std::string const&, std::vector<std::string> const&) {
		cancelled = true;
		return true;
	});
	background_updater background{std::move(up), 1, {"mfi-cli"}};
	launch(background);
	REQUIRE(await_result(background) == update_result::ready);
	CHECK(background.apply_ready([&] { return cancelled; }) == update_result::replaced_not_restarted);
	CHECK_FALSE(background.tick(background_updater::clock::now() + 100s));
	CHECK(fs::is_empty(dir.path));
}

TEST_CASE("background update: ordinary outcomes never invoke application", "[update][background]") {
	update_directory dir;
	auto up = fake_update(dir.path / "target");
	auto expected = update_result::up_to_date;
	SECTION("up to date") {
		up.set_fetch([](std::string const&) -> std::optional<std::string> {
			return R"([{"ref":"refs/tags/mfi-cli/v1.0.0"}])";
		});
	}
	SECTION("metadata failure") {
		expected = update_result::check_failed;
		up.set_fetch([](std::string const&) -> std::optional<std::string> { return std::nullopt; });
	}
	SECTION("download failure") {
		expected = update_result::download_failed;
		up.set_download([](std::string const&, std::string const& path) {
			std::ofstream{path} << "partial";
			return false;
		});
	}
	bool applied = false;
	up.set_apply([&](std::string const&, std::vector<std::string> const&) { applied = true; return true; });
	background_updater background{std::move(up), 1, {"mfi-cli"}};
	launch(background);
	CHECK(await_result(background) == expected);
	CHECK_FALSE(applied);
	CHECK(fs::is_empty(dir.path));
}

TEST_CASE("background update: stop promptly interrupts cooperative preparation",
	"[update][background]") {
	update_directory dir;
	auto up = fake_update(dir.path / "target");
	std::atomic_bool entered{false};
	up.set_cancellable_fetch([&](std::string const&, preparation_context const& context)
		-> std::optional<std::string> {
		entered = true;
		while (!context.interrupted()) std::this_thread::sleep_for(1ms);
		return std::nullopt;
	});
	background_updater background{std::move(up), 1, {"mfi-cli"}};
	launch(background);
	auto deadline = background_updater::clock::now() + 1s;
	while (!entered && background_updater::clock::now() < deadline) std::this_thread::sleep_for(1ms);
	REQUIRE(entered);
	auto start = background_updater::clock::now();
	background.stop();
	CHECK(background_updater::clock::now() - start < 250ms);
	CHECK_FALSE(background.tick(background_updater::clock::now() + 100s));
}

TEST_CASE("background update: preparation budget includes delayed result handoff",
	"[update][background]") {
	update_directory dir;
	background_updater background{fake_update(dir.path / "target"), 1, {"mfi-cli"}, {50ms, 100ms}};
	launch(background);
	std::this_thread::sleep_for(100ms);
	CHECK(await_result(background) == update_result::timed_out);
	CHECK(fs::is_empty(dir.path));
}
