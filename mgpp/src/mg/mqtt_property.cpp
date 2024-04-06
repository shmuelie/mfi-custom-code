#include "mg/mqtt_property.h"
#include "mg/strings.h"

using namespace std;
using namespace mg;

mqtt_property::mqtt_property() noexcept : _prop() {
}

mqtt_property::mqtt_property(mg_mqtt_prop prop) noexcept : _prop(prop) {
};

uint8_t mqtt_property::id() const noexcept {
	return _prop.id;
}

void mqtt_property::id(uint8_t value) noexcept {
	_prop.id = value;
}

uint32_t mqtt_property::integer_value() const noexcept {
	return _prop.iv;
}

void mqtt_property::integer_value(uint32_t value) noexcept {
	_prop.iv = value;
}

const string mqtt_property::key() const noexcept {
	mg_str key = _prop.key;
	return to_string(key);
}

void mqtt_property::key(const string& value) noexcept {
	_prop.key = to_mg_str(value);
}

const string mqtt_property::value() const noexcept {
	mg_str value = _prop.val;
	return to_string(value);
}

void mqtt_property::value(const string& value) noexcept {
	_prop.val = to_mg_str(value);
}