/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/functions/sensor.h"

/**
 * @brief Returns attributes for a temperature sensor
 */

inline SensorAttributes getTemperatureSensorAttributes()
{
    SensorAttributes attributes;
    attributes.unit_of_measurement = "°C";
    attributes.device_class = "temperature";
    attributes.state_class = "measurement";
    attributes.suggested_display_precision = 1;
    return attributes;
}
