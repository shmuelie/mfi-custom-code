cmake_minimum_required(VERSION 3.21)

option(MFI_CROSS_COMPILE "Cross Compile for MIPS" OFF)

set(BUILDROOT_BINARY_DIR ${CMAKE_BINARY_DIR}/br)
set(MIPS_TOOLCHAIN_FILE  ${BUILDROOT_BINARY_DIR}/host/share/buildroot/toolchainfile.cmake)
set(MIPS_DEFAULT_CONFIG  ${CMAKE_CURRENT_SOURCE_DIR}/configs/mfi_defconfig)
set(BUILDROOT_CONFIG     ${BUILDROOT_BINARY_DIR}/.config)
set(BUILDROOT_REPO       ${CMAKE_SOURCE_DIR}/buildroot)
if(MFI_CROSS_COMPILE AND ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Linux"))
	message(STATUS "Cross Compiling for MIPS")
	if(${MIPS_DEFAULT_CONFIG} IS_NEWER_THAN ${BUILDROOT_CONFIG})
		message(STATUS "Configuring Buildroot")
		execute_process(
			COMMAND make BR2_EXTERNAL=${CMAKE_CURRENT_SOURCE_DIR} O=${BUILDROOT_BINARY_DIR} mfi_defconfig
			WORKING_DIRECTORY ${BUILDROOT_REPO}
		)
	endif()
	execute_process(
		COMMAND make BR2_EXTERNAL=${CMAKE_CURRENT_SOURCE_DIR} O=${BUILDROOT_BINARY_DIR}
		WORKING_DIRECTORY ${BUILDROOT_REPO}
	)

	if (EXISTS ${MIPS_TOOLCHAIN_FILE})
		set(CMAKE_TOOLCHAIN_FILE ${MIPS_TOOLCHAIN_FILE} PARENT_SCOPE)
	else()
		message(FATAL_ERROR "Toolchain file for MIPS not found")
	endif()
endif()