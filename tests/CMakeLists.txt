cmake_minimum_required(VERSION 3.21)

include(FetchContent)
FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

add_executable(tests
	test_string_helpers.cpp
	test_config.cpp
	test_helper_functions.cpp
	test_hass_functions.cpp
	test_device_base.cpp
	test_mfi_hardware.cpp
	test_cli_helpers.cpp
	test_update.cpp
	test_background_update.cpp
	test_update_process.cpp
	test_mqtt_freshness.cpp
	test_native_mqtt.cpp
)
set_target_properties(tests PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
target_link_libraries(tests PRIVATE
	Catch2::Catch2WithMain
	${SHMUELIE_SHARED_TARGET_NAME}
	${MFI_API_TARGET_NAME}
	${MFI_UPDATE_TARGET_NAME}
	${MFI_MQTT_CORE_TARGET_NAME}
	hass_mqtt_device
)
target_include_directories(tests PRIVATE
	${CMAKE_SOURCE_DIR}/hass_mqtt_device/include
	${CMAKE_SOURCE_DIR}/mfi-cli/includes
	${CMAKE_SOURCE_DIR}/mfi-update/includes
)

include(CTest)
include(Catch)
catch_discover_tests(tests)

# Fake-root smoke test for the mfi-cli write paths (relay/led/config)
add_test(
	NAME cli_smoke
	COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.sh $<TARGET_FILE:${MFI_CLI_TARGET_NAME}>
)

# Integration smoke test for the mfi-rest-server REST API (civetweb)
add_test(
	NAME rest_smoke
	COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/rest_smoke.sh $<TARGET_FILE:${MFI_SERVER_TARGET_NAME}>
)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(
	NAME mqtt_wire_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mqtt_wire_smoke.py $<TARGET_FILE:${MFI_MQTT_TARGET_NAME}>
)
set_tests_properties(mqtt_wire_smoke PROPERTIES TIMEOUT 90)

add_test(
	NAME native_mqtt_wire_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_mqtt_wire_smoke.py $<TARGET_FILE:${MFI_MQTT_TARGET_NAME}>
)
set_tests_properties(native_mqtt_wire_smoke PROPERTIES TIMEOUT 90)
