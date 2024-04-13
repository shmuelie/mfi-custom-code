#include "ha/discoverable.h"

using namespace std;
using namespace ha;

discoverable::discoverable() noexcept : _domain(discoverable_domain::sensor), _name(), _topic_name(), _object_id(), _id(), _use_attributes(false) {
}

discoverable_domain discoverable::domain() const noexcept {
	return _domain;
}
void discoverable::domain(discoverable_domain value) noexcept {
	_domain = value;
}

const string discoverable::name() const noexcept {
	return _name;
}
void discoverable::name(const string& value) noexcept {
	_name = value;
}

const string discoverable::topic_name() const noexcept {
	return _topic_name;
}
void discoverable::topic_name(const string& value) noexcept {
	_topic_name = value;
}

const string discoverable::object_id() const noexcept {
	return _object_id;
}
void discoverable::object_id(const string& value) noexcept {
	_object_id = value;
}

const string discoverable::id() const noexcept {
	return _id;
}
void discoverable::id(const string& value) noexcept {
	_id = value;
}

bool discoverable::use_attributes() const noexcept {
	return _use_attributes;
}
void discoverable::use_attributes(bool value) noexcept {
	_use_attributes = value;
}