cmake_minimum_required(VERSION 3.12)
project(mFiCustomCode)

set(MFI_CLI_SOURCE_CODE
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)
add_executable(${MFI_CLI_TARGET_NAME}
	${MFI_CLI_SOURCE_CODE}
)
set_target_properties(${MFI_CLI_TARGET_NAME} PROPERTIES
	CXX_STANDARD 20
	CXX_EXTENSIONS OFF
	VERSION 1.0.0.0
)
target_link_libraries(${MFI_CLI_TARGET_NAME} PUBLIC ${MFI_API_TARGET_NAME})