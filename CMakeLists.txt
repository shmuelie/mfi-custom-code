cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_subdirectory(br2)

project(
	mfi-custom-code
	DESCRIPTION "Providing custom code to run on Ubiquiti's mFi Devices"
	HOMEPAGE_URL "https://github.com/shmuelie/mfi-custom-code"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AddProjectVersionData)

# Binary size reduction for release builds
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
	add_compile_options(-ffunction-sections -fdata-sections)
	add_link_options(-Wl,--gc-sections -Wl,-s)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# MIPS-specific optimizations for the target CPU (Atheros AR9331 = MIPS 24Kc)
if(MFI_CROSS_COMPILE)
	add_compile_options(-march=mips32r2 -mtune=mips32r2)
endif()

include(AddProjectVersionData)

set(MGPP_TARGET_NAME mgpp)
set(MFI_API_TARGET_NAME mfi)
set(MFI_SERVER_TARGET_NAME mfi-rest-server)
set(MFI_MQTT_TARGET_NAME mfi-mqtt-client)
set(SHMUELIE_SHARED_TARGET_NAME shmuelie-shared)
set(MFI_CLI_TARGET_NAME mfi-cli)

add_subdirectory(mfi)
add_subdirectory(mgpp)
add_subdirectory(hass_mqtt_device)
add_subdirectory(shmuelie-shared)
add_subdirectory(mfi-cli)
add_subdirectory(mfi-rest-server)
add_subdirectory(mfi-mqtt-client)

if(NOT MFI_CROSS_COMPILE)
	enable_testing()
	add_subdirectory(tests)
endif()
