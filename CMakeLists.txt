# libturborel, the C driver for turborel (RULES.md D1, D13).
cmake_minimum_required(VERSION 3.25)

# The version is read from the public header, its one source (RULES.md D5).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/turborel/base.h" trl_version_lines
     REGEX "^#define TRL_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(trl_part IN ITEMS MAJOR MINOR PATCH)
  string(REGEX MATCH "TRL_VERSION_${trl_part} ([0-9]+)" trl_match "${trl_version_lines}")
  if(NOT trl_match)
    message(FATAL_ERROR "include/turborel/base.h defines no TRL_VERSION_${trl_part}")
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
option(TRL_FAULTS "Let the tests make an allocation fail on purpose (never in a shipped library)" OFF)
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
# Each platform's sockets in a file of its own (R-PLAT-2).
if(WIN32)
  set(trl_platform_sources src/net/socket_windows.c)
else()
  set(trl_platform_sources src/net/socket_posix.c)
endif()

add_library(
  turborel_objects OBJECT
  src/version.c
  src/decimal.c
  src/failure.c
  src/format.c
  src/memory.c
  src/auth/base64.c
  src/auth/binding.c
  src/auth/crypto.c
  src/auth/key_cache.c
  src/auth/mechanism.c
  src/auth/scram.c
  src/conn/cancel.c
  src/conn/conn.c
  src/conn/control.c
  src/conn/frames.c
  src/conn/options.c
  src/conn/session.c
  src/conn/target.c
  src/conn/told.c
  src/data/copy.c
  src/data/params.c
  src/data/values.c
  src/data/xml_lex.c
  src/data/xml_parse.c
  src/data/xml_text.c
  src/data/xml_tree.c
  src/net/tls.c
  src/net/tls_settings.c
  src/net/transport.c
  src/stmt/prepared.c
  src/stmt/query.c
  src/stmt/result.c
  ${trl_platform_sources}
  src/codec/arena.c
  src/codec/buffer.c
  src/codec/compress.c
  src/codec/decfloat.c
  src/codec/error.c
  src/codec/fields.c
  src/codec/frame.c
  src/codec/int128.c
  src/codec/json_check.c
  src/codec/json_nodes.c
  src/codec/json_parse.c
  src/codec/json_text.c
  src/codec/json_write.c
  src/codec/numerals.c
  src/codec/reader.c
  src/codec/replies.c
  src/codec/requests.c
  src/codec/strtod.c
  src/codec/tail.c
  src/codec/types.c
  src/codec/utf8.c
  src/codec/values.c
  src/codec/values_write.c)
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

# Allocation faults, for the tests alone (RULES.md D14).
if(TRL_FAULTS)
  target_compile_definitions(turborel_objects PUBLIC TRL_FAULTS)
endif()

# zlib for compressed frames, where it was found (RULES.md D12).
if(TRL_HAVE_ZLIB)
  target_compile_definitions(turborel_objects PRIVATE TRL_HAVE_ZLIB)
  target_link_libraries(turborel_objects PUBLIC ZLIB::ZLIB)
endif()

# The TLS library, for TLS and SCRAM's hashes, where it was found (D12).
if(TRL_HAVE_TLS)
  target_compile_definitions(turborel_objects PRIVATE TRL_HAVE_TLS)
  target_link_libraries(turborel_objects PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# What each platform's sockets, locks and certificate stores take.
set(trl_system_libraries "")
if(WIN32)
  list(APPEND trl_system_libraries ws2_32 crypt32)
elseif(APPLE)
  list(APPEND trl_system_libraries "-framework Security" "-framework CoreFoundation")
else()
  find_package(Threads REQUIRED)
  list(APPEND trl_system_libraries Threads::Threads)
endif()
target_link_libraries(turborel_objects PUBLIC ${trl_system_libraries})

add_library(turborel $<TARGET_OBJECTS:turborel_objects>)
if(TRL_HAVE_ZLIB)
  target_link_libraries(turborel PRIVATE ZLIB::ZLIB)
endif()
if(TRL_HAVE_TLS)
  target_link_libraries(turborel PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
target_link_libraries(turborel PRIVATE ${trl_system_libraries})
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
