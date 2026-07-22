cmake_minimum_required(VERSION 3.21)
project(mFiCustomCode)

set(MFI_UPDATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_UPDATE_HEADERS
	${MFI_UPDATE_INCLUDES}/mfi_update.h
	${MFI_UPDATE_INCLUDES}/mfi_update/config.h
	${MFI_UPDATE_INCLUDES}/mfi_update/downloader.h
	${MFI_UPDATE_INCLUDES}/mfi_update/periodic_updater.h
	${MFI_UPDATE_INCLUDES}/mfi_update/release.h
	${MFI_UPDATE_INCLUDES}/mfi_update/semver.h
	${MFI_UPDATE_INCLUDES}/mfi_update/updater.h
)
set(MFI_UPDATE_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/config.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/downloader.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/periodic_updater.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/release.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/semver.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_update/updater.cpp
)

find_package(nlohmann_json REQUIRED)

add_library(${MFI_UPDATE_TARGET_NAME} STATIC
	${MFI_UPDATE_HEADERS}
	${MFI_UPDATE_SOURCE_CODE}
)
set_target_properties(${MFI_UPDATE_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION 1.0.0
)
target_include_directories(${MFI_UPDATE_TARGET_NAME} PUBLIC ${MFI_UPDATE_INCLUDES})
target_link_libraries(${MFI_UPDATE_TARGET_NAME} PUBLIC nlohmann_json::nlohmann_json)
