# The libraries libturborel will use (RULES.md D12): OpenSSL 3.5 or later,
# or AWS-LC, for TLS and hashes, and zlib, for compressed frames. Each is
# looked for and reported, and none is required: no code uses them yet.
# The codec takes zlib with compressed frames, and the connection takes
# the TLS library with TLS and SCRAM; a build without one then refuses
# what needs it, saying which library it lacks.

set(TRL_HAVE_TLS OFF)
if(TRL_TLS STREQUAL "OpenSSL")
  find_package(OpenSSL 3.5 COMPONENTS Crypto SSL)
  if(OpenSSL_FOUND)
    set(TRL_HAVE_TLS ON)
    message(STATUS "TLS and hashes: OpenSSL ${OPENSSL_VERSION}")
  else()
    message(STATUS "TLS and hashes: OpenSSL 3.5 or later not found; set OPENSSL_ROOT_DIR to its prefix")
  endif()
elseif(TRL_TLS STREQUAL "AWS-LC")
  # AWS-LC installs OpenSSL's headers and library names, and reports
  # OpenSSL 1.1.1's version number, so it is found without a version and
  # told apart by the macro its headers define.
  find_package(OpenSSL COMPONENTS Crypto SSL)
  if(OpenSSL_FOUND)
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
    check_symbol_exists(OPENSSL_IS_AWSLC "openssl/base.h" TRL_IS_AWS_LC)
    unset(CMAKE_REQUIRED_INCLUDES)
  endif()
  if(OpenSSL_FOUND AND TRL_IS_AWS_LC)
    set(TRL_HAVE_TLS ON)
    message(STATUS "TLS and hashes: AWS-LC")
  else()
    message(STATUS "TLS and hashes: AWS-LC not found; set OPENSSL_ROOT_DIR to its prefix")
  endif()
elseif(TRL_TLS STREQUAL "none")
  message(STATUS "TLS and hashes: none, as TRL_TLS asks")
else()
  message(FATAL_ERROR "TRL_TLS is ${TRL_TLS}; it is OpenSSL, AWS-LC or none")
endif()

# zlib built into libturborel where none is found and TRL_ZLIB_FETCH asks,
# as on Windows, which has none of its own: a pinned release, its archive
# checked by its SHA-256, its sources compiled with the library's own, its
# names prefixed z_ (Z_PREFIX) so they meet no other zlib a program links.
set(TRL_ZLIB_RELEASE "1.3.2")
set(TRL_ZLIB_SHA256 "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16")
set(TRL_ZLIB_PARTS adler32 compress crc32 deflate inffast inflate inftrees trees uncompr zutil)

# Builds the zlib fetched into `target`, its sources without the
# warnings and the lint of the library's own.
function(trl_zlib_built_into target)
  set(sources "")
  foreach(part IN LISTS TRL_ZLIB_PARTS)
    list(APPEND sources "${TRL_ZLIB_DIR}/${part}.c")
  endforeach()
  target_sources(${target} PRIVATE ${sources})
  target_include_directories(${target} SYSTEM PRIVATE "${TRL_ZLIB_DIR}")
  target_compile_definitions(${target} PRIVATE Z_PREFIX)
  if(MSVC)
    set_source_files_properties(${sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "/W0"
                                                                                  SKIP_LINTING ON)
  else()
    set_source_files_properties(${sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-w"
                                                                                  SKIP_LINTING ON)
  endif()
endfunction()

set(TRL_HAVE_ZLIB OFF)
set(TRL_ZLIB_BUILT OFF)
if(TRL_ZLIB)
  find_package(ZLIB)
  if(ZLIB_FOUND)
    set(TRL_HAVE_ZLIB ON)
    message(STATUS "Compressed frames: zlib ${ZLIB_VERSION_STRING}")
  elseif(TRL_ZLIB_FETCH)
    include(FetchContent)
    FetchContent_Declare(
      trl_zlib
      URL "https://github.com/madler/zlib/releases/download/v${TRL_ZLIB_RELEASE}/zlib-${TRL_ZLIB_RELEASE}.tar.gz"
      URL_HASH "SHA256=${TRL_ZLIB_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      # Its sources alone are taken; its own build is not added.
      SOURCE_SUBDIR trl-not-built)
    FetchContent_MakeAvailable(trl_zlib)
    set(TRL_ZLIB_DIR "${trl_zlib_SOURCE_DIR}")
    set(TRL_HAVE_ZLIB ON)
    set(TRL_ZLIB_BUILT ON)
    message(STATUS "Compressed frames: zlib ${TRL_ZLIB_RELEASE}, built in from its release")
  else()
    message(STATUS "Compressed frames: zlib not found")
  endif()
else()
  message(STATUS "Compressed frames: zlib not looked for, as TRL_ZLIB asks")
endif()
