cmake_minimum_required(VERSION 3.21)
project(mFiCustomCode)

set(MGPP_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/includes)
set(MGPP_HEADERS
	${MGPP_INCLUDES}/mg/event.h
	${MGPP_INCLUDES}/mg/connection.h
	${MGPP_INCLUDES}/mg/http_message.h
	${MGPP_INCLUDES}/mg/http_response.h
	${MGPP_INCLUDES}/mg/http_server.h
	${MGPP_INCLUDES}/mg/logger.h
	${MGPP_INCLUDES}/mg/server.h
	${MGPP_INCLUDES}/mg/strings.h
	${MGPP_INCLUDES}/mg/eventing_handler.h
	${MGPP_INCLUDES}/mg/timer.h
	${MGPP_INCLUDES}/mg/manager.h
)
set(MGPP_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/connection.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_message.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_response.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/http_server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/logger.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/eventing_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/mg/timer.cpp
)

find_package(mongoose REQUIRED)

add_library(${MGPP_TARGET_NAME} STATIC
	${MGPP_HEADERS}
	${MGPP_SOURCE_CODE}
)
set_target_properties(${MGPP_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION 1.0.0.0
)
target_include_directories(${MGPP_TARGET_NAME} PUBLIC ${MGPP_INCLUDES})
target_link_libraries(${MGPP_TARGET_NAME} PUBLIC ${SHMUELIE_SHARED_TARGET_NAME} mongoose)
