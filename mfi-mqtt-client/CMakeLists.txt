cmake_minimum_required(VERSION 3.21)
project(
	${MFI_MQTT_TARGET_NAME}
	VERSION 1.0.1.0
	DESCRIPTION "MQTT Client for Ubiquiti's mFi Devices"
	HOMEPAGE_URL "https://github.com/shmuelie/mfi-custom-code"
)

set(MFI_MQTT_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_MQTT_HEADERS
	${MFI_MQTT_INCLUDE}/mfi_mqtt_client/device.h
	${MFI_MQTT_INCLUDE}/mfi_mqtt_client/port.h
)
set(MFI_MQTT_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_mqtt_client/device.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_mqtt_client/port.cpp
)

find_package(CLI11 REQUIRED)

add_executable(${MFI_MQTT_TARGET_NAME}
	${MFI_MQTT_HEADERS}
	${MFI_MQTT_SOURCE_CODE}
)
set_target_properties(${MFI_MQTT_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
ADD_VERSION_INFO(${MFI_MQTT_TARGET_NAME})
target_include_directories(${MFI_MQTT_TARGET_NAME} PUBLIC ${MFI_MQTT_INCLUDE})
target_link_libraries(${MFI_MQTT_TARGET_NAME} PUBLIC ${MFI_API_TARGET_NAME} ${SHMUELIE_SHARED_TARGET_NAME} hass_mqtt_device CLI11::CLI11)
