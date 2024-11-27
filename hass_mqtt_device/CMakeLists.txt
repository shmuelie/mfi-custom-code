cmake_minimum_required(VERSION 3.10)

# Assume that we are on rPi if it is an arm variant
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    add_definitions(-DARM_ARCH)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    add_definitions(-DARM64_ARCH)
endif()

# Export compile flags
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create the library
add_library(hass_mqtt_device STATIC ${SOURCES})
set_target_properties(hass_mqtt_device PROPERTIES
    # Specify the required C++ standard
    CXX_STANDARD 17
)

# Compiler flags
target_compile_options(hass_mqtt_device PRIVATE
  $<$<CONFIG:Debug>:-g3 -O0>
  $<$<CONFIG:Release>:-O3>
)

# Use ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Include directories
target_include_directories(hass_mqtt_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hass_mqtt_device PUBLIC ${MOSQUITTO_INCLUDE_DIRS})

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
find_package(nlohmann_json REQUIRED)

# Link libraries
target_link_libraries(hass_mqtt_device ${MOSQUITTO_LIBRARIES} nlohmann_json::nlohmann_json)


# Specify where the library should be installed
install(TARGETS hass_mqtt_device DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)