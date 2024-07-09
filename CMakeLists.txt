cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_subdirectory(br2)

project(mFiCustomCode)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(MGPP_TARGET_NAME mgpp)
set(MFI_API_TARGET_NAME mfi)
set(MFI_SERVER_TARGET_NAME mfi-rest-server)
set(MFI_MQTT_TARGET_NAME mfi-mqtt-client)
set(SHMUELIE_SHARED_TARGET_NAME shmuelie-shared)
set(MFI_CLI_TARGET_NAME mfi-cli)

set(HASS_MQTT_DEVICE_STATIC ON)
set(HASS_MQTT_DEVICE_SPDLOG OFF)

add_subdirectory(mfi)
add_subdirectory(mgpp)
add_subdirectory(hass_mqtt_device)
add_subdirectory(shmuelie-shared)
add_subdirectory(mfi-cli)
add_subdirectory(mfi-rest-server)
add_subdirectory(mfi-mqtt-client)
