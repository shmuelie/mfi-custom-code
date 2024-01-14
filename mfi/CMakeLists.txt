cmake_minimum_required(VERSION 3.12)
project(mFiCustomCode)

set(MFI_API_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_API_HEADERS
	${MFI_API_INCLUDE}/FileWatch.hpp
	${MFI_API_INCLUDE}/mfi.h
	${MFI_API_INCLUDE}/mfi/board.h
	${MFI_API_INCLUDE}/mfi/led.h
	${MFI_API_INCLUDE}/mfi/sensor.h
)
set(MFI_API_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/board.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/led.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi/sensor.cpp
)

add_library(${MFI_API_TARGET_NAME} STATIC
	${MFI_API_HEADERS}
	${MFI_API_SOURCE_CODE}
)
set_target_properties(${MFI_API_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION 1.0.0.0
)
target_include_directories(${MFI_API_TARGET_NAME} PUBLIC ${MFI_API_INCLUDE})