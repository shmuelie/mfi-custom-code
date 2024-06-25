#include "mfi_mqtt_client/functions/power.h"

using namespace std;
using namespace mfi_mqtt_client::functions;

power::power() noexcept : SensorFunction("power", {
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 4
	}) {
}