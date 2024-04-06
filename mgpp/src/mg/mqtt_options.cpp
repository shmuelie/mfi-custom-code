#include "mg/mqtt_options.h"
#include "mg/strings.h"

using namespace std;
using namespace std::chrono;
using namespace mg;

#define prop(prop_name, field_name, type) type mqtt_options::prop_name() const noexcept {\
	return _opts.field_name;\
}\
void mqtt_options::prop_name(type value) noexcept {\
	_opts.field_name = value;\
}

#define str_prop(prop_name, field_name) const string mqtt_options::prop_name() const noexcept {\
	mg_str field_name = _opts.field_name;\
	return to_string(field_name);\
}\
void mqtt_options::prop_name(const string& value) noexcept {\
	_opts.field_name = to_mg_str(value);\
}

mqtt_options::mqtt_options() noexcept : _opts(), _props(), _will_props() {
}

mqtt_options::mqtt_options(mg_mqtt_opts opts) noexcept : _opts(opts), _props(), _will_props() {
	for (size_t prop_index = 0; prop_index < _opts.num_props; prop_index++) {
		_props.push_back(_opts.props[prop_index]);
	}
	for (size_t will_prop_index = 0; will_prop_index < _opts.num_will_props; will_prop_index++) {
		_will_props.push_back(_opts.will_props[will_prop_index]);
	}
}

mqtt_options::operator mg_mqtt_opts () const noexcept {
	mg_mqtt_opts opts = _opts;
	if (opts.num_props != _props.size() && opts.props != nullptr) {
		delete[] opts.props;
	}

	if (_props.size() > 0) {
		opts.num_props = _props.size();
		opts.props = new mg_mqtt_prop[opts.num_props];
		for (size_t prop_index = 0; prop_index < _props.size(); prop_index++) {
			opts.props[prop_index] = _props.at(prop_index)._prop;
		}
	}

	if (opts.num_will_props != _will_props.size() && opts.will_props != nullptr) {
		delete[] opts.will_props;
	}

	if (_will_props.size() > 0) {
		opts.num_will_props = _will_props.size();
		opts.will_props = new mg_mqtt_prop[opts.num_will_props];
		for (size_t will_prop_index = 0; will_prop_index < _will_props.size(); will_prop_index++) {
			opts.will_props[will_prop_index] = _will_props.at(will_prop_index)._prop;
		}
	}

	return opts;
}

str_prop(username, user)
str_prop(password, pass)
str_prop(client_id, client_id)
str_prop(topic, topic)
str_prop(message, message)
prop(quality_of_service, qos, uint8_t)
prop(version, version, uint8_t)
prop(retain, retain, bool)
prop(clean, clean, bool)

seconds mqtt_options::keep_alive() const noexcept {
	return seconds{ _opts.keepalive };
}

void mqtt_options::keep_alive(seconds value) noexcept {
	_opts.keepalive = value.count();
}

const vector<mqtt_property>& mqtt_options::properties() const noexcept {
	return _props;
}

const vector<mqtt_property>& mqtt_options::will_properties() const noexcept {
	return _will_props;
}