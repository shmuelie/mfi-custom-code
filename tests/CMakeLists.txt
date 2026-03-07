cmake_minimum_required(VERSION 3.21)

find_package(Catch2 REQUIRED)

add_executable(tests
	test_string_helpers.cpp
	test_config.cpp
	test_helper_functions.cpp
)
set_target_properties(tests PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
target_link_libraries(tests PRIVATE
	Catch2::Catch2WithMain
	${SHMUELIE_SHARED_TARGET_NAME}
	${MFI_API_TARGET_NAME}
)
target_include_directories(tests PRIVATE
	${CMAKE_SOURCE_DIR}/hass_mqtt_device/include
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
