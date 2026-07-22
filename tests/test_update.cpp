#include <catch2/catch_all.hpp>
#include "mfi_update/semver.h"
#include "mfi_update/config.h"
#include "mfi_update/downloader.h"
#include "mfi_update/release.h"
#include "mfi_update/updater.h"
#include "mfi_update/periodic_updater.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace mfi_update;

// ========== semver ==========

TEST_CASE("update: parse_semver basic", "[update][semver]") {
	auto v = parse_semver("1.2.3");
	REQUIRE(v.has_value());
	CHECK(v->major == 1);
	CHECK(v->minor == 2);
	CHECK(v->patch == 3);
}

TEST_CASE("update: parse_semver skips tag and name prefixes", "[update][semver]") {
	CHECK(parse_semver("refs/tags/mfi-cli/v1.2.3") == semver{1, 2, 3});
	CHECK(parse_semver("mfi-cli 1.2.3") == semver{1, 2, 3});
	CHECK(parse_semver("v10.20.30") == semver{10, 20, 30});
}

TEST_CASE("update: parse_semver rejects non-versions", "[update][semver]") {
	CHECK_FALSE(parse_semver("").has_value());
	CHECK_FALSE(parse_semver("1.2").has_value());
	CHECK_FALSE(parse_semver("abc").has_value());
}

TEST_CASE("update: semver ordering is numeric not lexicographic", "[update][semver]") {
	// The mfi-env bug this fixes: 1.10.0 must be greater than 1.9.0.
	CHECK(semver{1, 10, 0} > semver{1, 9, 0});
	CHECK(max_semver(semver{1, 9, 0}, semver{1, 10, 0}) == semver{1, 10, 0});
	CHECK(semver{2, 0, 0} > semver{1, 99, 99});
}

// ========== config precedence ==========

namespace {
	// Fake environment for resolve().
	const char* fake_https_only(const char* name) {
		if (std::string{name} == "https_proxy") return "proxy.example:8888";
		return nullptr;
	}
	const char* fake_http_only(const char* name) {
		if (std::string{name} == "http_proxy") return "httpproxy.example:3128";
		return nullptr;
	}
	const char* fake_none(const char*) { return nullptr; }
}

TEST_CASE("update: config takes proxy from https_proxy env", "[update][config]") {
	config c;
	auto r = c.resolve(&fake_https_only);
	CHECK(r.proxy == "proxy.example:8888");
	CHECK(r.use_proxy);
}

TEST_CASE("update: config falls back to http_proxy env", "[update][config]") {
	config c;
	auto r = c.resolve(&fake_http_only);
	CHECK(r.proxy == "httpproxy.example:3128");
	CHECK(r.use_proxy);
}

TEST_CASE("update: explicit proxy overrides env", "[update][config]") {
	config c;
	c.proxy = "explicit:9999";
	auto r = c.resolve(&fake_https_only);
	CHECK(r.proxy == "explicit:9999");
	CHECK(r.use_proxy);
}

TEST_CASE("update: no proxy anywhere leaves use_proxy off", "[update][config]") {
	config c;
	auto r = c.resolve(&fake_none);
	CHECK(r.proxy.empty());
	CHECK_FALSE(r.use_proxy);
}

// ========== downloader command builder ==========

TEST_CASE("update: wget argv includes insecure and proxy", "[update][downloader]") {
	config c;
	c.insecure = true;
	c.proxy = "p:8888";
	c.use_proxy = true;
	downloader dl{downloader_kind::wget, c};
	auto argv = dl.build_argv("https://example/x", "/tmp/out");

	CHECK(argv.front() == "wget");
	auto has = [&](std::string const& s) {
		return std::find(argv.begin(), argv.end(), s) != argv.end();
	};
	CHECK(has("--no-check-certificate"));
	CHECK(has("http_proxy=p:8888"));
	CHECK(has("https_proxy=p:8888"));
	CHECK(has("-O"));
	CHECK(has("/tmp/out"));
	CHECK(argv.back() == "https://example/x");
}

TEST_CASE("update: wget argv to stdout uses dash", "[update][downloader]") {
	config c;
	c.insecure = false;
	downloader dl{downloader_kind::wget, c};
	auto argv = dl.build_argv("https://example/x", "");
	auto has = [&](std::string const& s) {
		return std::find(argv.begin(), argv.end(), s) != argv.end();
	};
	CHECK(has("-"));
	CHECK_FALSE(has("--no-check-certificate"));
}

TEST_CASE("update: curl argv includes -k and proxy", "[update][downloader]") {
	config c;
	c.insecure = true;
	c.proxy = "p:8888";
	c.use_proxy = true;
	downloader dl{downloader_kind::curl, c};
	auto argv = dl.build_argv("https://example/x", "/tmp/out");
	CHECK(argv.front() == "curl");
	auto has = [&](std::string const& s) {
		return std::find(argv.begin(), argv.end(), s) != argv.end();
	};
	CHECK(has("-k"));
	CHECK(has("-x"));
	CHECK(has("p:8888"));
	CHECK(has("-o"));
	CHECK(has("/tmp/out"));
}

// ========== release parsing ==========

TEST_CASE("update: pick_latest_tag chooses highest semver", "[update][release]") {
	std::string body = R"([
		{"ref":"refs/tags/mfi-cli/v1.0.0"},
		{"ref":"refs/tags/mfi-cli/v1.10.0"},
		{"ref":"refs/tags/mfi-cli/v1.9.0"},
		{"ref":"refs/tags/mfi-rest-server/v9.9.9"}
	])";
	auto r = pick_latest_tag(body, "mfi-cli");
	REQUIRE(r.has_value());
	CHECK(r->first == semver{1, 10, 0});
	CHECK(r->second == "mfi-cli/v1.10.0");
}

TEST_CASE("update: pick_latest_tag none for unknown tool", "[update][release]") {
	std::string body = R"([{"ref":"refs/tags/mfi-cli/v1.0.0"}])";
	CHECK_FALSE(pick_latest_tag(body, "nope").has_value());
}

TEST_CASE("update: pick_latest_tag handles bad json", "[update][release]") {
	CHECK_FALSE(pick_latest_tag("not json", "mfi-cli").has_value());
}

TEST_CASE("update: pick_asset_url matches asset by name", "[update][release]") {
	std::string body = R"({
		"assets":[
			{"name":"other","browser_download_url":"https://x/other"},
			{"name":"mfi-cli","browser_download_url":"https://x/mfi-cli"}
		]
	})";
	auto url = pick_asset_url(body, "mfi-cli");
	REQUIRE(url.has_value());
	CHECK(*url == "https://x/mfi-cli");
}

TEST_CASE("update: pick_asset_url none when missing", "[update][release]") {
	std::string body = R"({"assets":[{"name":"other","browser_download_url":"u"}]})";
	CHECK_FALSE(pick_asset_url(body, "mfi-cli").has_value());
}

// ========== is_valid_elf ==========

TEST_CASE("update: is_valid_elf detects ELF magic", "[update][updater]") {
	auto dir = fs::temp_directory_path() / "mfi_update_test";
	fs::create_directories(dir);
	auto good = dir / "good";
	auto bad = dir / "bad";
	{
		std::ofstream o{good, std::ios::binary};
		o << "\x7f" "ELF" "rest-of-binary";
	}
	{
		std::ofstream o{bad, std::ios::binary};
		o << "#!/bin/sh\n";
	}
	CHECK(is_valid_elf(good.string()));
	CHECK_FALSE(is_valid_elf(bad.string()));
	CHECK_FALSE(is_valid_elf((dir / "missing").string()));
	fs::remove_all(dir);
}

// ========== updater flow with injected seams ==========

namespace {
	struct FakeServer {
		std::string refs;
		std::string release;
		std::string binary_body{"\x7f" "ELF" "dummy-new-binary"};
		std::string requested_download_url;
		std::string downloaded_path;
		bool applied = false;
		std::string applied_new_path;
	};
}

TEST_CASE("update: check_and_apply updates when newer", "[update][updater]") {
	FakeServer srv;
	srv.refs = R"([{"ref":"refs/tags/mfi-cli/v2.0.0"}])";
	srv.release = R"({"assets":[{"name":"mfi-cli","browser_download_url":"https://dl/mfi-cli"}]})";

	config c;
	c.enabled = true;
	updater up{"mfi-cli", semver{1, 0, 0}, c};

	up.set_fetch([&](std::string const& url) -> std::optional<std::string> {
		if (url.find("matching-refs") != std::string::npos) return srv.refs;
		if (url.find("releases/tags/") != std::string::npos) return srv.release;
		return std::nullopt;
	});
	up.set_download([&](std::string const& url, std::string const& path) {
		srv.requested_download_url = url;
		srv.downloaded_path = path;
		std::ofstream o{path, std::ios::binary};
		o << srv.binary_body;
		return true;
	});
	up.set_apply([&](std::string const& new_path, std::vector<std::string> const&) {
		srv.applied = true;
		srv.applied_new_path = new_path;
		return true; // do NOT exec in tests
	});

	auto result = up.check_and_apply({"mfi-cli"});
	CHECK(result == update_result::updated);
	CHECK(srv.requested_download_url == "https://dl/mfi-cli");
	CHECK(srv.applied);
	CHECK(srv.applied_new_path == srv.downloaded_path);
	std::remove(srv.downloaded_path.c_str());
}

TEST_CASE("update: check_and_apply up_to_date when not newer", "[update][updater]") {
	config c;
	updater up{"mfi-cli", semver{2, 0, 0}, c};
	up.set_fetch([&](std::string const& url) -> std::optional<std::string> {
		if (url.find("matching-refs") != std::string::npos)
			return std::string{R"([{"ref":"refs/tags/mfi-cli/v2.0.0"}])"};
		return std::nullopt;
	});
	bool downloaded = false;
	up.set_download([&](std::string const&, std::string const&) { downloaded = true; return true; });
	up.set_apply([&](std::string const&, std::vector<std::string> const&) { return true; });

	CHECK(up.check_and_apply({"mfi-cli"}) == update_result::up_to_date);
	CHECK_FALSE(downloaded);
}

TEST_CASE("update: check_and_apply disabled short-circuits", "[update][updater]") {
	config c;
	c.enabled = false;
	updater up{"mfi-cli", semver{1, 0, 0}, c};
	up.set_fetch([&](std::string const&) -> std::optional<std::string> {
		FAIL("fetch should not be called when disabled");
		return std::nullopt;
	});
	CHECK(up.check_and_apply({"mfi-cli"}) == update_result::disabled);
}

TEST_CASE("update: check_and_apply download_failed on non-ELF", "[update][updater]") {
	config c;
	updater up{"mfi-cli", semver{1, 0, 0}, c};
	up.set_fetch([&](std::string const& url) -> std::optional<std::string> {
		if (url.find("matching-refs") != std::string::npos)
			return std::string{R"([{"ref":"refs/tags/mfi-cli/v2.0.0"}])"};
		return std::string{R"({"assets":[{"name":"mfi-cli","browser_download_url":"u"}]})"};
	});
	up.set_download([&](std::string const&, std::string const& path) {
		std::ofstream o{path};
		o << "#!/bin/sh\n"; // not ELF
		return true;
	});
	bool applied = false;
	up.set_apply([&](std::string const&, std::vector<std::string> const&) { applied = true; return true; });

	CHECK(up.check_and_apply({"mfi-cli"}) == update_result::download_failed);
	CHECK_FALSE(applied);
}

// ========== periodic_updater timing ==========

TEST_CASE("update: periodic_updater respects interval", "[update][periodic]") {
	config c;
	updater up{"mfi-cli", semver{1, 0, 0}, c};
	int checks = 0;
	// Stub the whole flow by intercepting fetch to count and return up_to_date path.
	up.set_fetch([&checks](std::string const& url) -> std::optional<std::string> {
		if (url.find("matching-refs") != std::string::npos) {
			++checks;
			return std::string{R"([{"ref":"refs/tags/mfi-cli/v1.0.0"}])"};
		}
		return std::nullopt;
	});

	using clock = periodic_updater::clock;
	auto t0 = clock::time_point{};
	periodic_updater pu{std::move(up), 100 /* seconds */, {"mfi-cli"}};

	// First tick establishes the baseline (no check yet).
	CHECK_FALSE(pu.tick(t0 + std::chrono::seconds(50)).has_value());
	CHECK(checks == 0);

	// Within one interval of the baseline: not due.
	CHECK_FALSE(pu.tick(t0 + std::chrono::seconds(100)).has_value());
	CHECK(checks == 0);

	// One interval after the baseline: due.
	auto r1 = pu.tick(t0 + std::chrono::seconds(151));
	REQUIRE(r1.has_value());
	CHECK(*r1 == update_result::up_to_date);
	CHECK(checks == 1);

	// Not due immediately again.
	CHECK_FALSE(pu.tick(t0 + std::chrono::seconds(160)).has_value());
	CHECK(checks == 1);

	// Due again after another interval.
	auto r2 = pu.tick(t0 + std::chrono::seconds(252));
	REQUIRE(r2.has_value());
	CHECK(checks == 2);
}

TEST_CASE("update: make_periodic_updater disabled returns nullopt", "[update][periodic]") {
	auto pu = make_periodic_updater(false, 100, "", "", true, "mfi-cli", "1.0.0", {"mfi-cli"});
	CHECK_FALSE(pu.has_value());
}

TEST_CASE("update: make_periodic_updater parses repo and version", "[update][periodic]") {
	auto pu = make_periodic_updater(true, 100, "owner/name", "", true, "mfi-cli", "mfi-cli 1.2.3", {"mfi-cli"});
	CHECK(pu.has_value());
}

TEST_CASE("update: make_periodic_updater bad version returns nullopt", "[update][periodic]") {
	auto pu = make_periodic_updater(true, 100, "", "", true, "mfi-cli", "not-a-version", {"mfi-cli"});
	CHECK_FALSE(pu.has_value());
}
