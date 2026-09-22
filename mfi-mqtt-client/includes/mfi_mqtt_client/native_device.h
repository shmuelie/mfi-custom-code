#pragma once

#include "mfi_mqtt_client/device.h"
#include <map>

namespace mfi_mqtt_client {
	class native_device : public device {
	public:
		using clock = std::chrono::steady_clock;
		native_device(mfi::board const& board, std::string const& server, int port,
			std::string const& username, std::string const& password, sensor_policy policy,
			std::string const& device_id, std::string const& publisher_version);
		void init() override;
		void update() override;
		void update(clock::time_point now);
		json descriptor() const;
		std::vector<std::string> getSubscribeTopics() const override;
		bool acceptsMessage(std::string const& topic) const override;
		std::size_t messagePayloadLimit() const override { return 512; }
		void processMessage(std::string const& topic, std::string const& payload, bool retained) override;
		void beginConnection(std::uint64_t epoch) override;
		void sendDiscovery() override;
		void sendStatus() override {}
		void service() override;
		bool readyForOnline() const override;
	protected:
		virtual std::string session_id() const;
	private:
		struct confirmation {
			std::string request_id;
			bool desired;
			bool write_failed;
		};
		struct native_port {
			mfi::sensor sensor;
			json published_roles;
			json observed_roles;
			std::optional<clock::time_point> last_publish;
			std::optional<publication> pending;
			std::optional<confirmation> command;
			std::int64_t sequence = 0;
		};
		std::string _device_id;
		std::string _publisher_version;
		std::string _session;
		std::map<int, native_port> _native_ports;
		std::optional<publication> _discovery;
		std::optional<std::string> _command_fault;
		std::string base_topic() const;
		std::string port_topic(int id) const;
		void sample(native_port& port, clock::time_point now);
		void reject_command(std::string const& reason);
	};
}
