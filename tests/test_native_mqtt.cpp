#include <catch2/catch_all.hpp>
#include "mfi_mqtt_client/identity.h"
#include "mfi_mqtt_client/native_device.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace mfi_mqtt_client;

namespace {
std::string const device_id = "0123456789abcdef0123456789abcdef";
std::string const first_session = "11111111111141118111111111111111";
std::string const next_session = "22222222222242228222222222222222";
std::string const request_id = "33333333333343338333333333333333";
std::string const base_topic = "mfi/" + device_id;
auto const start = native_device::clock::time_point{};

struct hardware_tree {
	std::filesystem::path previous = std::filesystem::current_path();
	std::filesystem::path root;
	explicit hardware_tree(bool eight = false) {
		auto pattern = (std::filesystem::temp_directory_path() / "mfi-native-XXXXXX").string();
		auto created = ::mkdtemp(pattern.data());
		if (!created) {
			throw std::runtime_error("Cannot create native hardware fixture");
		}
		root = created;
		std::filesystem::current_path(root);
		write("etc/board.info", std::string("board.name=mPower\nboard.shortname=mpower\nboard.sysid=")
			+ (eight ? "e648\n" : "e671\n"));
		write("etc/version", "MF.test\n");
		write("etc/persistent/cfg/config_file", "port.0.label=Desk\nport.1.sensorId=Second\n");
		for (int id = 1; id <= (eight ? 8 : 1); ++id) {
			write("proc/power/active_pwr" + std::to_string(id), "100\n");
			write("proc/power/i_rms" + std::to_string(id), "0.8333\n");
			write("proc/power/v_rms" + std::to_string(id), "120\n");
			write("proc/power/relay" + std::to_string(id), "0\n");
		}
	}
	~hardware_tree() {
		std::filesystem::current_path(previous);
		std::filesystem::remove_all(root);
	}
	void write(std::filesystem::path const& path, std::string const& text) {
		if (path.has_parent_path()) {
			std::filesystem::create_directories(path.parent_path());
		}
		std::ofstream out(path);
		out.exceptions(std::ios::badbit | std::ios::failbit);
		out << text;
	}
	std::string read(std::filesystem::path const& path) const {
		std::ifstream in(path);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}
};

struct message {
	std::string topic;
	json payload;
	int qos;
	bool retained;
	publication ticket;
};

class capture_native final : public native_device {
public:
	explicit capture_native(mfi::board const& board, sensor_policy policy = sensor_policy::power()) :
		native_device(board, "localhost", 1883, "", "", policy, device_id, "2.0.0") {}
	bool connected = true;
	bool auto_complete = true;
	bool reject = false;
	std::uint64_t epoch = 1;
	std::uint64_t serial = 0;
	std::string session = first_session;
	std::vector<message> messages;
	std::set<std::uint64_t> completed;

	bool isConnected() const override { return connected; }
	std::string session_id() const override { return session; }
	publication publishMessage(std::string const& topic, json const& payload, int qos, bool retained) override {
		if (!connected || reject) {
			return {};
		}
		publication ticket{MOSQ_ERR_SUCCESS, epoch, ++serial};
		messages.push_back({topic, payload, qos, retained, ticket});
		if (auto_complete) {
			completed.insert(ticket.sequence);
		}
		return ticket;
	}
	publication_state publicationState(publication const& ticket) const override {
		if (!connected || !ticket.accepted() || ticket.epoch != epoch) {
			return publication_state::failed;
		}
		return completed.contains(ticket.sequence) ? publication_state::complete : publication_state::pending;
	}
	void establish() {
		beginConnection(epoch);
		sendDiscovery();
	}
	void command(std::string value, bool retained = false, std::string id = request_id) {
		processMessage(::base_topic + "/port/1/set",
			json{{"session_id", session}, {"request_id", id}, {"value", value}}.dump(), retained);
	}
};
}

TEST_CASE("native MQTT: descriptors use physical ports and native topics only", "[mqtt][native]") {
	bool eight = GENERATE(false, true);
	hardware_tree tree(eight);
	auto device = std::make_shared<capture_native>(mfi::board{});
	device->init();
	CHECK(device->getFunctions().empty());
	auto descriptor = device->descriptor();
	CHECK(descriptor["schema_version"] == 1);
	CHECK(descriptor["device_id"] == device_id);
	CHECK(descriptor["model_id"] == (eight ? "58952" : "58993"));
	CHECK(descriptor["firmware_version"] == "MF.test");
	CHECK(descriptor["publisher_version"] == "2.0.0");
	CHECK(descriptor["refresh_interval"] == 60);
	CHECK(descriptor["expire_after"] == 180);
	REQUIRE(descriptor["ports"].size() == (eight ? 8 : 1));
	for (int id = 1; id <= (eight ? 8 : 1); ++id) {
		CHECK(descriptor["ports"][id - 1]["id"] == id);
		CHECK(descriptor["ports"][id - 1]["capabilities"] == json::array({"power", "current", "voltage", "relay"}));
		CHECK(device->getSubscribeTopics()[id - 1] == base_topic + "/port/" + std::to_string(id) + "/set");
	}
	CHECK(descriptor["ports"][0]["name"] == "Desk");
	if (eight) {
		CHECK(descriptor["ports"][1]["name"] == "Second");
		CHECK(descriptor["ports"][2]["name"] == "Port 3");
	}
	device->establish();
	CHECK(device->messages.back().topic == base_topic + "/config");
	CHECK(device->messages.back().qos == 1);
	CHECK(device->messages.back().retained);
	CHECK_FALSE(device->acceptsMessage("home/legacy/relay/set"));
	CHECK_FALSE(device->acceptsMessage("mfi/" + next_session + "/port/1/set"));
}

TEST_CASE("native MQTT: invalid hardware metadata and policy fail before connection", "[mqtt][native]") {
	hardware_tree tree;
	SECTION("unknown board") {
		tree.write("etc/board.info", "board.name=Unknown\nboard.shortname=unknown\nboard.sysid=0001\n");
		CHECK_THROWS(capture_native(mfi::board{}));
		auto legacy = std::make_shared<mfi_mqtt_client::device>(mfi::board{}, "", 1883, "", "");
		legacy->init();
		CHECK_THROWS(legacy->migration_map(device_id));
	}
	SECTION("missing version") {
		tree.write("etc/version", "");
		CHECK_THROWS(capture_native(mfi::board{}));
	}
	SECTION("invalid UTF-8") {
		tree.write("etc/version", "\xff");
		CHECK_THROWS(capture_native(mfi::board{}));
	}
	SECTION("long label") {
		tree.write("etc/persistent/cfg/config_file", "port.0.label=" + std::string(129, 'x'));
		CHECK_THROWS(capture_native(mfi::board{}));
	}
	SECTION("bad intervals") {
		CHECK_THROWS(capture_native(mfi::board{}, sensor_policy::power(0s, 180s)));
		CHECK_THROWS(capture_native(mfi::board{}, sensor_policy::power(60s, 179s)));
		CHECK_THROWS(capture_native(mfi::board{}, sensor_policy::power(86401s, 259203s)));
	}
}

TEST_CASE("native MQTT: new reads gate online and constant reports refresh all roles", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.auto_complete = false;
	device.establish();
	CHECK_FALSE(device.readyForOnline());
	device.sendStatus();
	CHECK(device.messages.size() == 1);
	device.update(start);
	REQUIRE(device.messages.size() == 2);
	CHECK_FALSE(device.readyForOnline());
	device.completed.insert(device.messages[0].ticket.sequence);
	CHECK_FALSE(device.readyForOnline());
	device.completed.insert(device.messages[1].ticket.sequence);
	CHECK(device.readyForOnline());
	device.auto_complete = true;
	for (int second = 1; second <= 3600; ++second) {
		device.update(start + std::chrono::seconds(second));
	}
	REQUIRE(device.messages.size() == 62);
	for (std::size_t index = 1; index < device.messages.size(); ++index) {
		auto const& message = device.messages[index];
		CHECK(message.topic == base_topic + "/port/1/state");
		CHECK(message.qos == 0);
		CHECK_FALSE(message.retained);
		CHECK(message.payload.size() == 6);
		CHECK(message.payload["session_id"] == first_session);
		CHECK(message.payload["sequence"] == index);
		CHECK(message.payload["power"] == json{{"status", "ok"}, {"value", 100}});
		CHECK(message.payload["relay"] == json{{"status", "ok"}, {"value", "OFF"}});
	}
	tree.write("proc/power/active_pwr1", "0\n");
	device.update(start + 3601s);
	CHECK(device.messages.back().payload["power"]["value"] == 0);
	CHECK(device.messages.back().payload["sequence"] == 62);
}

TEST_CASE("native MQTT: checked errors isolate roles and physical ports", "[mqtt][native]") {
	std::string invalid = GENERATE("", "broken", "1 trailing", "NaN", "inf", "-1", "1e305", "1000000001");
	hardware_tree tree(true);
	capture_native device(mfi::board{});
	device.establish();
	tree.write("proc/power/active_pwr1", invalid);
	tree.write("proc/power/i_rms1", "-1");
	std::filesystem::remove("proc/power/v_rms1");
	tree.write("proc/power/relay1", "2");
	device.update(start);
	auto const& failed = device.messages[1].payload;
	CHECK(failed["power"] == json{{"status", "error"}, {"reason", "invalid_data"}});
	CHECK(failed["current"] == json{{"status", "error"}, {"reason", "invalid_data"}});
	CHECK(failed["voltage"] == json{{"status", "error"}, {"reason", "open_failed"}});
	CHECK(failed["relay"] == json{{"status", "error"}, {"reason", "invalid_data"}});
	CHECK(device.messages[2].payload["power"]["status"] == "ok");
	tree.write("proc/power/active_pwr1", "42.123456");
	device.update(start + 1s);
	CHECK(device.messages.back().payload["sequence"] == 2);
	CHECK(device.messages.back().payload["power"] == json{{"status", "ok"}, {"value", 42.1235}});
	std::filesystem::remove("proc/power/active_pwr1");
	std::filesystem::create_directory("proc/power/active_pwr1");
	device.update(start + 2s);
	CHECK(device.messages.back().payload["power"] == json{{"status", "error"}, {"reason", "read_failed"}});
}

TEST_CASE("native MQTT: reconnect resets session sequence and discards queued confirmations", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	device.auto_complete = false;
	device.update(start);
	device.command("ON");
	REQUIRE(device.messages.size() == 2);
	CHECK(tree.read("proc/power/relay1") == "1");
	device.connected = false;
	device.update(start + 1s);
	device.command("OFF");
	CHECK(tree.read("proc/power/relay1") == "1");
	device.connected = true;
	device.epoch++;
	device.session = next_session;
	device.auto_complete = true;
	device.establish();
	device.sendStatus();
	CHECK(device.messages.size() == 3);
	tree.write("proc/power/active_pwr1", "bad");
	device.update(start + 2s);
	auto const& payload = device.messages.back().payload;
	CHECK(payload["session_id"] == next_session);
	CHECK(payload["sequence"] == 1);
	CHECK_FALSE(payload.contains("request_id"));
	CHECK(payload["power"]["status"] == "error");
	device.session = "invalid";
	CHECK_THROWS(device.beginConnection(++device.epoch));
}

TEST_CASE("native MQTT: invalid commands never turn a relay off", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	tree.write("proc/power/relay1", "1\n");
	json valid{{"session_id", first_session}, {"request_id", request_id}, {"value", "OFF"}};
	std::vector<std::string> invalid{
		"", "not-json", "[]", "null", "false", R"({"value":"OFF"})", std::string(513, 'x'),
		R"({"session_id":")" + first_session + R"(","request_id":")" + request_id + R"(","value":"ON","value":"OFF"})"
	};
	for (auto const& value : {json("off"), json(""), json(false), json(0), json(nullptr), json::array()}) {
		auto command = valid;
		command["value"] = value;
		invalid.push_back(command.dump());
	}
	for (auto const& key : {"session_id", "request_id"}) {
		for (auto const& value : {json("wrong"), json(42), json(nullptr)}) {
			auto command = valid;
			command[key] = value;
			invalid.push_back(command.dump());
		}
	}
	auto old_session = valid;
	old_session["session_id"] = next_session;
	invalid.push_back(old_session.dump());
	auto extra = valid;
	extra["extra"] = true;
	invalid.push_back(extra.dump());
	for (auto const& payload : invalid) {
		CAPTURE(payload);
		device.processMessage(base_topic + "/port/1/set", payload, false);
		CHECK(tree.read("proc/power/relay1") == "1\n");
	}
	device.processMessage(base_topic + "/port/1/set", valid.dump(), true);
	for (auto const& topic : {base_topic + "/port/0/set", base_topic + "/port/01/set",
		base_topic + "/port/2/set", base_topic + "/port/1/state", std::string("home/legacy/set")}) {
		device.processMessage(topic, valid.dump(), false);
	}
	CHECK(tree.read("proc/power/relay1") == "1\n");
	CHECK(device.messages.size() == 1);
}

TEST_CASE("native MQTT: relay commands confirm fresh reads including unchanged state", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	device.update(start);
	device.command("OFF");
	CHECK(tree.read("proc/power/relay1") == "0\n");
	CHECK(device.messages.back().payload["request_id"] == request_id);
	CHECK(device.messages.back().payload["sequence"] == 2);
	CHECK(device.messages.back().payload["relay"]["value"] == "OFF");
	device.command("ON", false, next_session);
	CHECK(tree.read("proc/power/relay1") == "1");
	CHECK(device.messages.back().payload["request_id"] == next_session);
	CHECK(device.messages.back().payload["relay"]["value"] == "ON");
	CHECK(device.messages.back().qos == 0);
	CHECK_FALSE(device.messages.back().retained);
	std::filesystem::remove("proc/power/relay1");
	device.command("OFF");
	CHECK_FALSE(std::filesystem::exists("proc/power/relay1"));
	CHECK(device.messages.back().payload["relay"] == json{{"status", "error"}, {"reason", "write_failed"}});
	device.update(native_device::clock::now() + 1s);
	CHECK_FALSE(device.messages.back().payload.contains("request_id"));
	CHECK(device.messages.back().payload["relay"]["reason"] == "open_failed");
}

TEST_CASE("native MQTT: checked relay API distinguishes read and write failures", "[mqtt][native]") {
	hardware_tree tree;
	mfi::sensor sensor(1);
	CHECK(std::get<bool>(sensor.relay_checked()) == false);
	CHECK_FALSE(sensor.relay_checked(true).has_value());
	CHECK(std::get<bool>(sensor.relay_checked()) == true);
	tree.write("proc/power/relay1", "not OFF");
	CHECK(std::get<mfi::sensor_read_error>(sensor.relay_checked()) == mfi::sensor_read_error::invalid_number);
	std::filesystem::remove("proc/power/relay1");
	CHECK(std::get<mfi::sensor_read_error>(sensor.relay_checked()) == mfi::sensor_read_error::open_failed);
	CHECK(sensor.relay_checked(false) == mfi::sensor_write_error::open_failed);
	CHECK_FALSE(std::filesystem::exists("proc/power/relay1"));
	std::filesystem::create_symlink("/dev/full", "proc/power/relay1");
	CHECK(sensor.relay_checked(false) == mfi::sensor_write_error::write_failed);
}

TEST_CASE("native MQTT: pending confirmation coalesces fresh readback without another write", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	device.auto_complete = false;
	device.update(start);
	device.command("ON");
	CHECK(tree.read("proc/power/relay1") == "1");
	device.command("OFF");
	CHECK(tree.read("proc/power/relay1") == "1");
	tree.write("proc/power/relay1", "0\n");
	device.completed.insert(device.messages.back().ticket.sequence);
	device.service();
	CHECK(tree.read("proc/power/relay1") == "0\n");
	CHECK(device.messages.back().payload["request_id"] == request_id);
	CHECK(device.messages.back().payload["relay"] == json{{"status", "error"}, {"reason", "state_mismatch"}});
}

TEST_CASE("native MQTT: rejected publication never replays a hardware write", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	device.reject = true;
	CHECK_THROWS(device.command("ON"));
	CHECK(tree.read("proc/power/relay1") == "1");
	tree.write("proc/power/relay1", "0\n");
	device.reject = false;
	device.service();
	CHECK(tree.read("proc/power/relay1") == "0\n");
	CHECK(device.messages.back().payload["relay"]["reason"] == "state_mismatch");
}

TEST_CASE("native MQTT: rejected state retries only a fresh hardware sample", "[mqtt][native]") {
	hardware_tree tree;
	capture_native device(mfi::board{});
	device.establish();
	device.reject = true;
	CHECK_THROWS(device.update(start));
	CHECK_FALSE(device.readyForOnline());
	tree.write("proc/power/active_pwr1", "invalid");
	device.reject = false;
	device.sendStatus();
	CHECK(device.messages.size() == 1);
	device.update(start + 1s);
	CHECK(device.messages.back().payload["power"]["status"] == "error");
	CHECK(device.messages.back().payload["sequence"] == 2);
	CHECK(device.readyForOnline());
}

TEST_CASE("native MQTT: UUIDs are OS-random lowercase version four identifiers", "[mqtt][native][identity]") {
	std::set<std::string> ids;
	for (int i = 0; i < 128; ++i) {
		auto id = random_uuid();
		CHECK(valid_uuid(id));
		CHECK(id[12] == '4');
		CHECK(std::string("89ab").find(id[16]) != std::string::npos);
		CHECK(ids.insert(id).second);
	}
	for (auto const& invalid : {"", "01234567-89ab-cdef-0123-456789abcdef",
		"0123456789ABCDEF0123456789ABCDEF", "0123456789abcdef0123456789abcdeg"}) {
		CHECK_FALSE(valid_uuid(invalid));
	}
}

TEST_CASE("native MQTT: provisioning persists once and preserves configuration", "[mqtt][native][identity]") {
	hardware_tree tree;
	auto path = tree.root / "publisher.conf";
	std::string original = "# keep comments\nserver = \"example.invalid\"\npassword = \"private-fixture\"\n[section]\nkey = 2\n";
	tree.write(path, original);
	REQUIRE(::chmod(path.c_str(), 0640) == 0);
	auto id = initialize_device_id(path);
	CHECK(valid_uuid(id));
	CHECK(tree.read(path) == "device_id = \"" + id + "\"\n" + original);
	CHECK(configured_device_id(path) == id);
	struct stat before{}, after{};
	REQUIRE(::stat(path.c_str(), &before) == 0);
	CHECK((before.st_mode & 0777) == 0640);
	CHECK(initialize_device_id(path) == id);
	REQUIRE(::stat(path.c_str(), &after) == 0);
	CHECK(before.st_ino == after.st_ino);
	CHECK(before.st_mtim.tv_sec == after.st_mtim.tv_sec);
	CHECK(before.st_mtim.tv_nsec == after.st_mtim.tv_nsec);
	CHECK(select_device_id(id, "") == id);
	CHECK(select_device_id(id, id) == id);
	CHECK_THROWS(select_device_id(id, next_session));
	CHECK_THROWS(select_device_id({}, id));
	CHECK_THROWS(select_device_id(id, "bad"));
	auto fresh = tree.root / "new.conf";
	CHECK(valid_uuid(initialize_device_id(fresh)));
	REQUIRE(::stat(fresh.c_str(), &after) == 0);
	CHECK((after.st_mode & 0777) == 0600);
}

TEST_CASE("native MQTT: corrupt or duplicate identities cannot be replaced", "[mqtt][native][identity]") {
	std::string contents = GENERATE(
		"device_id = \"bad\"\n",
		"device_id = \"\"\n",
		"device_id = [1,2]\n",
		"device_id = \"0123456789abcdef0123456789abcdef\"\ndevice-id = \"0123456789abcdef0123456789abcdef\"\n",
		"[nested]\ndevice_id = \"0123456789abcdef0123456789abcdef\"\n",
		"device_id = \"0123456789abcdef0123456789abcdef\"\ndevice_id = \"11111111111141118111111111111111\"\n"
	);
	hardware_tree tree;
	auto path = tree.root / "invalid.conf";
	tree.write(path, contents);
	CHECK_THROWS(configured_device_id(path));
	CHECK_THROWS(initialize_device_id(path));
	CHECK(tree.read(path) == contents);
}

TEST_CASE("native MQTT: provisioning refuses unsafe paths and locked or oversized files", "[mqtt][native][identity]") {
	hardware_tree tree;
	auto path = tree.root / "publisher.conf";
	tree.write(path, "server = \"fixture\"\n");
	SECTION("relative path") { CHECK_THROWS(initialize_device_id("publisher.conf")); }
	SECTION("missing directory") { CHECK_THROWS(initialize_device_id(tree.root / "absent/file")); }
	SECTION("symlink") {
		auto link = tree.root / "link.conf";
		std::filesystem::create_symlink(path, link);
		CHECK_THROWS(initialize_device_id(link));
		CHECK_THROWS(configured_device_id(link));
	}
	SECTION("hardlink") {
		std::filesystem::create_hard_link(path, tree.root / "link.conf");
		CHECK_THROWS(initialize_device_id(path));
	}
	SECTION("directory") { CHECK_THROWS(initialize_device_id(tree.root)); }
	SECTION("FIFO") {
		auto fifo = tree.root / "fifo";
		REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
		CHECK_THROWS(initialize_device_id(fifo));
		CHECK_THROWS(configured_device_id(fifo));
	}
	SECTION("size limit") {
		tree.write(path, std::string(65537, '#'));
		CHECK_THROWS(initialize_device_id(path));
		CHECK_THROWS(configured_device_id(path));
	}
	SECTION("unwritable directory") {
		if (::geteuid() == 0) {
			SKIP("Directory permissions do not restrict root");
		}
		REQUIRE(::chmod(tree.root.c_str(), 0500) == 0);
		CHECK_THROWS(initialize_device_id(path));
		REQUIRE(::chmod(tree.root.c_str(), 0700) == 0);
		CHECK(tree.read(path) == "server = \"fixture\"\n");
	}
	SECTION("concurrent provisioner") {
		int fd = ::open(tree.root.c_str(), O_RDONLY | O_DIRECTORY);
		REQUIRE(fd >= 0);
		REQUIRE(::flock(fd, LOCK_EX | LOCK_NB) == 0);
		CHECK_THROWS(initialize_device_id(path));
		REQUIRE(::close(fd) == 0);
		CHECK(valid_uuid(initialize_device_id(path)));
	}
}

TEST_CASE("native MQTT: migration map matches legacy discovery without connecting", "[mqtt][native][identity]") {
	hardware_tree tree(true);
	auto device = std::make_shared<mfi_mqtt_client::device>(mfi::board{}, "", 1883, "", "");
	CHECK_THROWS(device->migration_map(device_id));
	device->init();
	auto map = device->migration_map(device_id);
	CHECK(map["device_id"] == device_id);
	CHECK(map["schema_version"] == 1);
	REQUIRE(map["ports"].size() == 8);
	auto functions = device->getFunctions();
	REQUIRE(functions.size() == 32);
	for (int port = 1; port <= 8; ++port) {
		CHECK(map["ports"][port - 1]["id"] == port);
		int index = (port - 1) * 4;
		for (auto const& role : {"power", "current", "voltage", "relay"}) {
			auto const& function = functions[index++];
			auto expected = function->getDiscoveryJson();
			auto const& actual = map["ports"][port - 1]["roles"][role];
			CHECK(actual["unique_id"] == expected["unique_id"]);
			CHECK(actual["state_topic"] == expected["state_topic"]);
			CHECK(actual["discovery_topic"] == function->getDiscoveryTopic());
			if (expected.contains("command_topic")) {
				CHECK(actual["command_topic"] == expected["command_topic"]);
			}
		}
	}
	CHECK_FALSE(device->isConnected());
	CHECK_THROWS(device->migration_map("bad"));
}
