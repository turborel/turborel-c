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

set(TRL_HAVE_ZLIB OFF)
if(TRL_ZLIB)
  find_package(ZLIB)
  if(ZLIB_FOUND)
    set(TRL_HAVE_ZLIB ON)
    message(STATUS "Compressed frames: zlib ${ZLIB_VERSION_STRING}")
  else()
    message(STATUS "Compressed frames: zlib not found")
  endif()
else()
  message(STATUS "Compressed frames: zlib not looked for, as TRL_ZLIB asks")
endif()
