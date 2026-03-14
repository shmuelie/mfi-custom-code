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
	add_compile_options(-ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-rtti -fmerge-all-constants -fvisibility=hidden -fvisibility-inlines-hidden)
	add_link_options(-Wl,--gc-sections -Wl,-s -Wl,--as-needed -Wl,--exclude-libs,ALL)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# UPX compression for on-disk size reduction (defaults to ON for release builds)
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
	set(_MFI_UPX_DEFAULT ON)
else()
	set(_MFI_UPX_DEFAULT OFF)
endif()
option(MFI_UPX "Compress executables with UPX after linking" ${_MFI_UPX_DEFAULT})
if(MFI_UPX)
	find_program(UPX_EXECUTABLE NAMES upx upx-ucl)
	if(UPX_EXECUTABLE)
		message(STATUS "UPX found: ${UPX_EXECUTABLE}")
		set(MFI_UPX_FLAGS "--best" "-q" CACHE STRING "Flags to pass to UPX")
	else()
		message(WARNING "MFI_UPX is ON but upx was not found — compression disabled")
		set(MFI_UPX OFF)
	endif()
endif()

# Helper to apply UPX to a target
function(mfi_upx_compress target)
	if(MFI_UPX AND UPX_EXECUTABLE)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}" ${UPX_EXECUTABLE} ${MFI_UPX_FLAGS} $<TARGET_FILE:${target}>
			COMMENT "UPX compressing ${target}"
		)
	endif()
endfunction()

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
