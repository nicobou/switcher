# CMake Config
cmake_minimum_required(VERSION 3.5.1)
project(switcher)
#
# CONFIGURATION
#

# Project Variables
set(SWITCHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SWITCHER_VERSION_MAJOR 3)
set(SWITCHER_VERSION_MINOR 1)
set(SWITCHER_VERSION_PATCH 3)
set(SWITCHER_VERSION_STRING ${SWITCHER_VERSION_MAJOR}.${SWITCHER_VERSION_MINOR}.${SWITCHER_VERSION_PATCH})
set(SWITCHER_API_VERSION ${SWITCHER_VERSION_MAJOR}.${SWITCHER_VERSION_MINOR})
set(SWITCHER_LIBRARY switcher-${SWITCHER_API_VERSION})
set(SHMDATA_REQUIRED_VERSION 1.3)

# Package (pkg-config) Information
set(PACKAGE_NAME switcher)
set(PACKAGE_DESCRIPTION "Switcher for audio, video, data and network streams")
set(PACKAGE_URL "https://gitlab.com/sat-metalab/switcher")
set(PACKAGE_VERSION ${SWITCHER_VERSION_STRING})

# Compilation
set(CMAKE_CXX_STANDARD 17)
set(DATADIR ${CMAKE_INSTALL_PREFIX}/share/switcher/)

set(TEST_COVERAGE OFF CACHE BOOL "Enable test coverage")
if (TEST_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSWITCHER_TEST_COVERAGE -g -O0 --coverage")
endif()

# CPack - General
set(CPACK_PACKAGE_NAME ${PACKAGE_NAME})
set(CPACK_PACKAGE_VENDOR "Metalab - Société des arts technologiques")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_PACKAGE_VERSION_MAJOR "${SWITCHER_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${SWITCHER_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${SWITCHER_VERSION_PATCH}")
set(CPACK_GENERATOR DEB)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_COMPONENTS_ALL applications libraries headers)

# CPack - Debian Package
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}")
set(CPACK_DEBIAN_PROJECT_HOMEPAGE "https://gitlab.com/sat-metalab/switcher/")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR} <nbouillot@sat.qc.ca>")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Switcher is an integration environment, able to interoperate with other software and protocols. Switcher provides low latency streaming of multichannel audio, video and data through IP networks. Switcher provides managing of several instances of services (called Quiddities). A Quiddity can be created and removed dynamically and can be controlled through property get/set as well as method invocations and information tree monitoring. Switcher provides introspection mechanisms to help write higher level software written in python3 or C++. Switcher can save and load the state of quiddities.
")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libshmdata (>= 1.3)")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md
  DESTINATION share/doc/${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}/)

# CPack - Applications
set(CPACK_COMPONENT_APPLICATIONS_DISPLAY_NAME "Switcher & Switcher Control Application")
set(CPACK_COMPONENT_APPLICATIONS_DESCRIPTION "Application to control switcher remotely")
set(CPACK_COMPONENT_APPLICATIONS_DEPENDS libraries)

# CPack - Libraries
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Shared libraries used to build programs with Switcher")

# CPack - Includes
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C++ Headers")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION "C/C++ header files for use with Switcher")
set(CPACK_COMPONENT_HEADERS_DEPENDS libraries)

# CPack - Source
set(CPACK_SOURCE_IGNORE_FILES "/\.hooks/;${CMAKE_SOURCE_DIR}/build;${CMAKE_SOURCE_DIR}/html/;${CMAKE_SOURCE_DIR}/scripts/;.git/;${CPACK_SOURCE_IGNORE_FILES}")

# Add local cmake directory to cmake module path in case we need it
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# INCLUDES
#

include(FeatureSummary)
include(CPack)
include(PackageSourceTest)
include(CMakeDependentOption)

#
# PROJECT
#

project(Switcher)
enable_testing()
option(ENABLE_GPL "Compile plugins using GPL libraries" OFF)

# Prerequisites
find_package(PkgConfig REQUIRED)

# This is required in the case where macport screwed with the paths on mac os x
set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

pkg_check_modules(SHMDATA REQUIRED shmdata-${SHMDATA_REQUIRED_VERSION})
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-controller-1.0 gstreamer-sdp-1.0 gstreamer-video-1.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(JSONGLIB REQUIRED json-glib-1.0)

# LOGGER
find_package(spdlog REQUIRED)
add_compile_definitions(SPDLOG_FMT_EXTERNAL)

# Global stuff

set(COMMON_LIBRARIES
    ${SHMDATA_LIBRARIES}
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${JSONGLIB_LIBRARIES}
    )

link_directories(
    ${SHMDATA_LIBRARY_DIRS}
    ${GST_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${JSONGLIB_LIBRARY_DIRS}
)

link_libraries(
    ${COMMON_LIBRARIES}
    dl
    pthread
    stdc++fs
    uuid
    spdlog::spdlog
)

set(COMMON_INCLUDE_DIRS
    ${SHMDATA_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${JSONGLIB_INCLUDE_DIRS}
    )

include_directories(
    ${SWITCHER_DIR} # Switcher needs to include from itself using "switcher/*"
    ${COMMON_INCLUDE_DIRS}
)

add_compile_options(${SHMDATA_CFLAGS})
add_compile_options(${GST_CFLAGS})
add_compile_options(${GLIB_CFLAGS})
add_compile_options(${JSONGLIB_CFLAGS})

# BUILD TYPE
if(CMAKE_BUILD_TYPE EQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

# WARNINGS
add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-Wno-unused-parameter)
add_compile_options(-Wno-cast-function-type)
add_definitions("-DDATADIR=\"${DATADIR}\"")

# COLOR OUTPUT
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fdiagnostics-color CAN_DIAGNOSTICS_COLOR)
if(CAN_DIAGNOSTICS_COLOR)
  add_definitions(-fdiagnostics-color)
endif()

# OSX
if(APPLE AND ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OSX TRUE)
    add_definitions(-DOSX)
    add_compile_options(-ObjC++) # Allows objective-c building
endif ()

#
# MODULES
#

add_subdirectory(switcher)
add_subdirectory(tests)
add_subdirectory(plugins)
add_subdirectory(src)
add_subdirectory(wrappers)
add_subdirectory(tools)

#
# OTHER TARGETS
#

# Documentation
find_package(Doxygen)
if (DOXYGEN_FOUND)
    set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/doc/doxygen/doxyfile.in)
    set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/doxyfile)
    configure_file(${doxyfile_in} ${doxyfile} @ONLY)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif ()

# Uninstall
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeUninstall.cmake"
    IMMEDIATE @ONLY)
add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/CMakeUninstall.cmake)

#
# POST
#

# Report
feature_summary(WHAT ALL)

