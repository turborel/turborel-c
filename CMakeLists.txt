# libturborel, the C driver for turborel (RULES.md D1, D13).
cmake_minimum_required(VERSION 3.25)

# The version is read from the public header, its one source (RULES.md D5).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/turborel/turborel.h" trl_version_lines
     REGEX "^#define TRL_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(trl_part IN ITEMS MAJOR MINOR PATCH)
  string(REGEX MATCH "TRL_VERSION_${trl_part} ([0-9]+)" trl_match "${trl_version_lines}")
  if(NOT trl_match)
    message(FATAL_ERROR "include/turborel/turborel.h defines no TRL_VERSION_${trl_part}")
  endif()
  set(trl_version_${trl_part} "${CMAKE_MATCH_1}")
endforeach()

project(
  turborel
  VERSION "${trl_version_MAJOR}.${trl_version_MINOR}.${trl_version_PATCH}"
  DESCRIPTION "The C driver for turborel, over its native protocol"
  HOMEPAGE_URL "https://turborel.com/docs/drivers/c/"
  LANGUAGES C)

include(GNUInstallDirs)
include(CMakeDependentOption)

option(BUILD_SHARED_LIBS "Build libturborel as a shared library" ON)
option(TRL_BUILD_TESTS "Build the tests and register them with CTest" ${PROJECT_IS_TOP_LEVEL})
option(TRL_LINT "Run clang-tidy as each file compiles, and check formatting with clang-format" OFF)
option(TRL_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TRL_COVERAGE "Build with Clang's source-based coverage, and add the coverage target" OFF)
option(TRL_FUZZ "Build the libFuzzer targets (Clang only)" OFF)
cmake_dependent_option(TRL_TEST_PACKAGE "Test the installed package from a program outside the tree" OFF
                       "TRL_BUILD_TESTS" OFF)
set(TRL_TLS "OpenSSL" CACHE STRING "The TLS and hash library: OpenSSL (3.5 or later), AWS-LC, or none")
set_property(CACHE TRL_TLS PROPERTY STRINGS OpenSSL AWS-LC none)
option(TRL_ZLIB "Look for zlib, for compressed frames" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TrlWarnings)
include(TrlChecks)
include(TrlDependencies)

# One set of objects makes the library and is linked into the tests, which
# reach the internal codec the library hides (RULES.md D13, D14).
add_library(turborel_objects OBJECT src/version.c src/codec/frame.c)
target_include_directories(
  turborel_objects
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
trl_c_target(turborel_objects)
set_target_properties(turborel_objects PROPERTIES POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
if(BUILD_SHARED_LIBS)
  target_compile_definitions(turborel_objects PRIVATE TRL_BUILDING)
else()
  target_compile_definitions(turborel_objects PUBLIC TRL_STATIC)
endif()

add_library(turborel $<TARGET_OBJECTS:turborel_objects>)
add_library(turborel::turborel ALIAS turborel)
target_include_directories(turborel PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(turborel PUBLIC c_std_17)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(turborel PUBLIC TRL_STATIC)
endif()
set_target_properties(
  turborel
  PROPERTIES OUTPUT_NAME turborel
             PREFIX lib
             IMPORT_PREFIX lib
             VERSION "${PROJECT_VERSION}"
             SOVERSION "${PROJECT_VERSION_MAJOR}"
             C_VISIBILITY_PRESET hidden)

include(TrlInstall)

if(TRL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(TRL_FUZZ)
  add_subdirectory(fuzz)
endif()

if(TRL_COVERAGE)
  trl_coverage_target()
endif()
