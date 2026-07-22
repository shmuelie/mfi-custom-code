cmake_minimum_required(VERSION 3.21)
project(
	${MFI_CLI_TARGET_NAME}
	VERSION 1.1.0
	DESCRIPTION "CLI tool for Ubiquiti's mFi Devices"
	HOMEPAGE_URL "https://github.com/shmuelie/mfi-custom-code"
)

find_package(CLI11 REQUIRED)

set(MFI_CLI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_CLI_HEADERS
	${MFI_CLI_INCLUDE}/mfi_cli/info_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/sensor_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/relay_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/led_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/config_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/update_command.h
	${MFI_CLI_INCLUDE}/mfi_cli/helpers.h
)
set(MFI_CLI_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/info_command.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/sensor_command.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/relay_command.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/led_command.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/config_command.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_cli/update_command.cpp
)
add_executable(${MFI_CLI_TARGET_NAME}
	${MFI_CLI_HEADERS}
	${MFI_CLI_SOURCE_CODE}
)
set_target_properties(${MFI_CLI_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
ADD_VERSION_INFO(${MFI_CLI_TARGET_NAME})
target_include_directories(${MFI_CLI_TARGET_NAME} PUBLIC ${MFI_CLI_INCLUDE})
target_link_libraries(${MFI_CLI_TARGET_NAME} PUBLIC ${MFI_API_TARGET_NAME} ${SHMUELIE_SHARED_TARGET_NAME} ${MFI_UPDATE_TARGET_NAME} CLI11::CLI11)
mfi_upx_compress(${MFI_CLI_TARGET_NAME})
