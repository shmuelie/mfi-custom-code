#include "mfi_mqtt_client/functions/current.h"

using namespace std;
using namespace mfi_mqtt_client::functions;

current::current() noexcept : SensorFunction("current", {
		.device_class = "current",
		.state_class = "measurement",
		.unit_of_measurement = "A"
	}) {
}