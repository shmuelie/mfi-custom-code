"""Constants for the mFi MQTT energy companion."""

from datetime import timedelta

DOMAIN = "mfi"
CONF_STORAGE_ID = "storage_id"
CONF_SOURCE_DEVICE = "source_device_id"
CONF_SOURCE_CONFIG_ENTRY = "source_config_entry_id"
CONF_EXCLUDED = "excluded_sources"
CONF_FRESHNESS = "freshness_confirmed"
REPORT_INTERVAL = timedelta(seconds=60)
MODEL_PORT_COUNTS = {"58952": 8, "58993": 1}
MANUFACTURER = "Ubiquiti Networks"
STORAGE_VERSION = 1
