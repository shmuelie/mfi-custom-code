#include "mfi_mqtt_client/functions/voltage.h"

using namespace std;
using namespace mfi_mqtt_client::functions;

voltage::voltage() noexcept : SensorFunction("voltage", {
		.device_class = "voltage",
		.state_class = "measurement",
		.unit_of_measurement = "V",
		.suggested_display_precision = 4
	}) {
}