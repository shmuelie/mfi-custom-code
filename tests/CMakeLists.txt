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
)
set_target_properties(tests PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
target_link_libraries(tests PRIVATE
	Catch2::Catch2WithMain
	${SHMUELIE_SHARED_TARGET_NAME}
	${MFI_API_TARGET_NAME}
	hass_mqtt_device
)
target_include_directories(tests PRIVATE
	${CMAKE_SOURCE_DIR}/hass_mqtt_device/include
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
