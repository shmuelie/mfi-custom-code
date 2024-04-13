cmake_minimum_required(VERSION 3.12)
project(mFiCustomCode)

set(MFI_MQTT_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_MQTT_HEADERS
	${MFI_MQTT_INCLUDE}/mfi_mqtt/mfi_mqtt_client.h
	${MFI_MQTT_INCLUDE}/ha/command.h
	${MFI_MQTT_INCLUDE}/ha/discoverable.h
	${MFI_MQTT_INCLUDE}/ha/discoverable_domain.h
	${MFI_MQTT_INCLUDE}/ha/sensor.h
	${MFI_MQTT_INCLUDE}/ha/multi_value_sensor.h
	${MFI_MQTT_INCLUDE}/ha/single_value_sensor.h
)
set(MFI_MQTT_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_mqtt/mfi_mqtt_client.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/ha/discoverable.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/ha/sensor.cpp
)
set(MFI_MQTT_CLIENT_VERSION 1.1.0.0)
add_executable(${MFI_MQTT_TARGET_NAME}
	${MFI_MQTT_HEADERS}
	${MFI_MQTT_SOURCE_CODE}
)
set_target_properties(${MFI_MQTT_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION ${MFI_MQTT_CLIENT_VERSION}
)
target_include_directories(${MFI_MQTT_TARGET_NAME} PUBLIC ${MFI_MQTT_INCLUDE})
target_link_libraries(${MFI_MQTT_TARGET_NAME} PUBLIC ${MFI_API_TARGET_NAME} docopt ${MGPP_TARGET_NAME})
target_compile_definitions(${MFI_MQTT_TARGET_NAME} PRIVATE MFI_MQTT_CLIENT_VERSION=${MFI_MQTT_CLIENT_VERSION})