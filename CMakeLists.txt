cmake_minimum_required(VERSION 3.12)

option(MFI_CROSS_COMPILE "Cross Compile for MIPS" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(MIPS_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/br2/host/share/buildroot/toolchainfile.cmake)
if(MFI_CROSS_COMPILE AND ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Linux"))
	message(STATUS "Cross Compiling for MIPS")
	execute_process(
		COMMAND make O=${CMAKE_CURRENT_SOURCE_DIR}/br2
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/buildroot
	)
	set(CMAKE_TOOLCHAIN_FILE ${MIPS_TOOLCHAIN_FILE})
endif()

project(mFiCustomCode)

set(MGPP_TARGET_NAME mgpp)
set(MFI_API_TARGET_NAME mfi)
set(MFI_SERVER_TARGET_NAME mfi-rest-server)
set(MFI_MQTT_TARGET_NAME mfi-mqtt-client)
set(SHMUELIE_SHARED_TARGET_NAME shmuelie-shared)

set(HASS_MQTT_DEVICE_STATIC ON)
set(HASS_MQTT_DEVICE_SPDLOG OFF)

add_subdirectory(mfi)
add_subdirectory(docopt)
add_subdirectory(mgpp)
add_subdirectory(hass_mqtt_device)
add_subdirectory(shmuelie-shared)
add_subdirectory(mfi-rest-server)
add_subdirectory(mfi-mqtt-client)
