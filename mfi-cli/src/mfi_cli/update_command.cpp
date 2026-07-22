#include "mfi_cli/update_command.h"
#include "mfi_update.h"
#include "shmuelie/string_helpers.h"
#include "version_info.h"
#include <iostream>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi_update;

namespace {
	// Applies the shared repo/proxy/bin-dir options onto a config and resolves it.
	config make_config(std::string const& repo, std::string const& proxy,
		std::string const& bin_dir, bool insecure) {
		config c;
		if (!repo.empty()) {
			auto slash = repo.find('/');
			if (slash != std::string::npos) {
				c.repo_owner = repo.substr(0, slash);
				c.repo_name = repo.substr(slash + 1);
			}
		}
		if (!proxy.empty()) {
			c.proxy = proxy;
		}
		if (!bin_dir.empty()) {
			c.bin_dir = bin_dir;
		}
		c.insecure = insecure;
		return c.resolve();
	}
}

update_command::update_command(App& app)
	: _repo{}, _proxy{}, _bin_dir{}, _insecure{ true }, _check_only{ false } {
	auto command = app.add_subcommand("update", "Update this tool from GitHub Releases");
	command->add_option("--repo", _repo, "GitHub owner/name (default shmuelie/mfi-custom-code)");
	command->add_option("--proxy", _proxy, "Proxy host:port for the downloader");
	command->add_option("--bin-dir", _bin_dir, "Directory holding the installed binary");
	command->add_flag("!--no-check-cert", _insecure,
		"Verify TLS certificates (default: skip, matching device firmware)");
	command->add_flag("--check", _check_only, "Only check for updates; do not download or apply");
	command->callback([this]() { run(); });
}

void update_command::run() {
	config cfg = make_config(_repo, _proxy, _bin_dir, _insecure);
	auto current = parse_semver(PROJECT_VERSION);
	if (!current) {
		std::cerr << "Could not determine current version" << std::endl;
		throw RuntimeError(1);
	}

	updater up{ PROJECT_NAME, *current, cfg };

	if (_check_only) {
		auto latest = up.check();
		if (!latest) {
			std::cerr << "Update check failed (no downloader, network error, or no release)" << std::endl;
			throw RuntimeError(2);
		}
		if (latest->version > *current) {
			std::cout << "Update available: " << PROJECT_VERSION << " -> "
				<< latest->version.major << "." << latest->version.minor << "." << latest->version.patch
				<< std::endl;
		}
		else {
			std::cout << "Up to date (" << PROJECT_VERSION << ")" << std::endl;
		}
		return;
	}

	// One-shot check + apply. On success this re-execs and does not return.
	auto result = up.check_and_apply({ std::string(PROJECT_NAME), "update" });
	switch (result) {
	case update_result::updated:
		std::cout << "Updated; restarting..." << std::endl;
		break;
	case update_result::up_to_date:
		std::cout << "Up to date (" << PROJECT_VERSION << ")" << std::endl;
		break;
	case update_result::disabled:
		std::cout << "Updates are disabled" << std::endl;
		break;
	case update_result::no_downloader:
		std::cerr << "No usable downloader (wget/curl) found" << std::endl;
		throw RuntimeError(2);
	case update_result::check_failed:
		std::cerr << "Update check failed" << std::endl;
		throw RuntimeError(2);
	case update_result::download_failed:
		std::cerr << "Update download failed" << std::endl;
		throw RuntimeError(3);
	}
}
