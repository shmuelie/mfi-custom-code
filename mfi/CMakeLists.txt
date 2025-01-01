cmake_minimum_required(VERSION 3.21)
project(
	${MFI_API_TARGET_NAME}
	VERSION 1.0.0.0
	DESCRIPTION "API for Ubiquiti's mFi Devices"
	HOMEPAGE_URL "https://github.com/shmuelie/mfi-custom-code"
)

set(MFI_API_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_API_HEADERS
	${MFI_API_INCLUDE}/mfi.h
	${MFI_API_INCLUDE}/mfi/board.h
	${MFI_API_INCLUDE}/mfi/led.h
	${MFI_API_INCLUDE}/mfi/sensor.h
	${MFI_API_INCLUDE}/mfi/config.h
)
set(MFI_API_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/board.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/led.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/sensor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/config.cpp
)

add_library(${MFI_API_TARGET_NAME} STATIC
	${MFI_API_HEADERS}
	${MFI_API_SOURCE_CODE}
)
set_target_properties(${MFI_API_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
)
ADD_VERSION_INFO(${MFI_API_TARGET_NAME})
target_include_directories(${MFI_API_TARGET_NAME} PUBLIC ${MFI_API_INCLUDE})
