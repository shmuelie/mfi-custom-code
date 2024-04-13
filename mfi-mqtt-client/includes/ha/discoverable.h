#pragma once

#include <string>
#include "ha/discoverable_domain.h"

namespace ha {
	class discoverable {
	public:
		discoverable_domain domain() const noexcept;
		const std::string name() const noexcept;
		const std::string topic_name() const noexcept;
		const std::string object_id() noexcept;
		const std::string id() const noexcept;
		bool use_attributes() const noexcept;

		virtual const std::string auto_discovery_config_json() const noexcept = 0;
	protected:
		explicit discoverable() noexcept;

		void domain(discoverable_domain value) noexcept;
		void name(const std::string& value) noexcept;
		void topic_name(const std::string& value) noexcept;
		void object_id(const std::string& value) noexcept;
		void id(const std::string& value) noexcept;
		void use_attributes(bool value) noexcept;
	private:
		discoverable_domain _domain;
		std::string _name;
		std::string _topic_name;
		std::string _object_id;
		std::string _id;
		bool _use_attributes;
	};
}