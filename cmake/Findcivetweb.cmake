# Provides a `civetweb` INTERFACE/imported target exposing the civetweb C++ API
# (CivetServer/CivetHandler) plus the civetweb C library.
#
# Cross builds (MFI_CROSS_COMPILE): use the civetweb static lib + headers that
# Buildroot installed into the target sysroot (BR2_PACKAGE_CIVETWEB_LIB).
#
# Host builds: fetch civetweb via FetchContent with the C++ API enabled and SSL
# and tests disabled, mirroring the device configuration (plain HTTP).

if(MFI_CROSS_COMPILE)
	find_path(CIVETWEB_INCLUDE_DIR
		NAMES CivetServer.h civetweb.h
		PATH_SUFFIXES include
	)
	find_library(CIVETWEB_LIBRARY NAMES civetweb)
	find_library(CIVETWEB_CPP_LIBRARY NAMES civetweb-cpp)

	if(NOT CIVETWEB_INCLUDE_DIR OR NOT CIVETWEB_LIBRARY)
		message(FATAL_ERROR "civetweb not found in sysroot; ensure BR2_PACKAGE_CIVETWEB_LIB=y")
	endif()

	add_library(civetweb STATIC IMPORTED GLOBAL)
	set_target_properties(civetweb PROPERTIES
		IMPORTED_LOCATION "${CIVETWEB_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${CIVETWEB_INCLUDE_DIR}"
	)
	# civetweb's C++ API may be built into the main lib or a separate -cpp lib.
	set(_civetweb_link_libs "")
	if(CIVETWEB_CPP_LIBRARY)
		list(APPEND _civetweb_link_libs "${CIVETWEB_CPP_LIBRARY}")
	endif()
	find_package(Threads REQUIRED)
	list(APPEND _civetweb_link_libs Threads::Threads m)
	set_target_properties(civetweb PROPERTIES
		INTERFACE_LINK_LIBRARIES "${_civetweb_link_libs}"
	)
else()
	include(FetchContent)
	set(CIVETWEB_ENABLE_CXX ON CACHE BOOL "" FORCE)
	set(CIVETWEB_ENABLE_SSL OFF CACHE BOOL "" FORCE)
	set(CIVETWEB_ENABLE_SSL_DYNAMIC_LOADING OFF CACHE BOOL "" FORCE)
	set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(CIVETWEB_ENABLE_ASAN OFF CACHE BOOL "" FORCE)
	set(CIVETWEB_INSTALL_EXECUTABLE OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(
		civetweb
		GIT_REPOSITORY https://github.com/civetweb/civetweb.git
		GIT_TAG        v1.16
	)
	FetchContent_MakeAvailable(civetweb)

	# Upstream exports `civetweb-cpp` (C++ API) which links the `civetweb-c-library`.
	# Expose a uniform `civetweb` alias for consumers.
	if(NOT TARGET civetweb AND TARGET civetweb-cpp)
		add_library(civetweb INTERFACE)
		target_link_libraries(civetweb INTERFACE civetweb-cpp)
	endif()
endif()
