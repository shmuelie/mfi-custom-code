function(add_version_info TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		VERSION ${${TARGET_NAME}_VERSION}
	)
	configure_file(
		${CMAKE_CURRENT_FUNCTION_LIST_DIR}/AddProjectVersionData.h.in
		"${CMAKE_BINARY_DIR}/configured/${TARGET_NAME}/version_info.h"
	)
	target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}/configured/${TARGET_NAME}/")
endfunction()