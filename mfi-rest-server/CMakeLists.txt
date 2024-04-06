cmake_minimum_required(VERSION 3.12)
project(mFiCustomCode)

set(MFI_SERVER_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MFI_SERVER_HEADERS
	${MFI_SERVER_INCLUDE}/mfi_server/mfi_http_server.h
	${MFI_SERVER_INCLUDE}/mfi_server/options.h
	${MFI_SERVER_INCLUDE}/mg/connection.h
	${MFI_SERVER_INCLUDE}/mg/http_message.h
	${MFI_SERVER_INCLUDE}/mg/http_response.h
	${MFI_SERVER_INCLUDE}/mg/http_server.h
	${MFI_SERVER_INCLUDE}/mg/logger.h
	${MFI_SERVER_INCLUDE}/mg/server.h
	${MFI_SERVER_INCLUDE}/mg/strings.h
	${MFI_SERVER_INCLUDE}/mongoose.h
	${MFI_SERVER_INCLUDE}/string_helpers.h
)
set(MFI_SERVER_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_server/mfi_http_server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mfi_server/options.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/connection.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_message.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_response.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/logger.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mongoose.c
)
set(MFI_REST_SERVER_VERSION 1.1.0.0)
add_executable(${MFI_SERVER_TARGET_NAME}
	${MFI_SERVER_HEADERS}
	${MFI_SERVER_SOURCE_CODE}
)
set_target_properties(${MFI_SERVER_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION ${MFI_REST_SERVER_VERSION}
)
target_include_directories(${MFI_SERVER_TARGET_NAME} PUBLIC ${MFI_SERVER_INCLUDE})
target_link_libraries(${MFI_SERVER_TARGET_NAME} PUBLIC ${MFI_API_TARGET_NAME} docopt)
target_compile_definitions(${MFI_SERVER_TARGET_NAME} PRIVATE MFI_REST_SERVER_VERSION=${MFI_REST_SERVER_VERSION})