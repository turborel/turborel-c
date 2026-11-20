# The checks a build can turn on (RULES.md D10): clang-tidy and
# clang-format, the sanitizers, and coverage. Each applies to every target
# made after this file is included.

if(TRL_LINT)
  find_program(TRL_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
  find_program(TRL_CLANG_FORMAT_EXE NAMES clang-format REQUIRED)
  set(trl_tidy "${TRL_CLANG_TIDY_EXE}" "--quiet")
  # A clang-tidy that is not Apple's does not know where Apple's SDK is.
  if(APPLE)
    execute_process(
      COMMAND xcrun --show-sdk-path
      OUTPUT_VARIABLE trl_sdk
      OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    list(APPEND trl_tidy "--extra-arg-before=-isysroot${trl_sdk}")
  endif()
  set(CMAKE_C_CLANG_TIDY ${trl_tidy})
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(TRL_SANITIZE)
  if(MSVC)
    # MSVC has AddressSanitizer alone.
    add_compile_options(/fsanitize=address)
  else()
    set(trl_sanitize -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_compile_options(${trl_sanitize})
    add_link_options(${trl_sanitize})
  endif()
endif()

if(TRL_FUZZ)
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "TRL_FUZZ needs LLVM's Clang, which ships libFuzzer; this is ${CMAKE_C_COMPILER_ID}")
  endif()
  # Everything instrumented for the fuzzer to follow, and sanitized; the
  # fuzz targets alone link libFuzzer (fuzz/CMakeLists.txt).
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(TRL_COVERAGE)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TRL_COVERAGE needs Clang, for source-based coverage (RULES.md D10)")
  endif()
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
  # The tools of the same LLVM as the compiler: beside it, or Xcode's.
  if(APPLE)
    execute_process(
      COMMAND xcrun --find llvm-cov
      OUTPUT_VARIABLE TRL_LLVM_COV
      OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
      COMMAND xcrun --find llvm-profdata
      OUTPUT_VARIABLE TRL_LLVM_PROFDATA
      OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  else()
    file(REAL_PATH "${CMAKE_C_COMPILER}" trl_compiler)
    get_filename_component(trl_compiler_dir "${trl_compiler}" DIRECTORY)
    find_program(
      TRL_LLVM_COV
      NAMES llvm-cov llvm-cov-${CMAKE_C_COMPILER_VERSION_MAJOR}
      HINTS "${trl_compiler_dir}" REQUIRED)
    find_program(
      TRL_LLVM_PROFDATA
      NAMES llvm-profdata llvm-profdata-${CMAKE_C_COMPILER_VERSION_MAJOR}
      HINTS "${trl_compiler_dir}" REQUIRED)
  endif()
  set(TRL_PROFILE_DIR "${CMAKE_BINARY_DIR}/profiles")
endif()

# The environment each test runs in: under coverage, where its profile goes.
function(trl_test_environment test)
  if(TRL_COVERAGE)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "LLVM_PROFILE_FILE=${TRL_PROFILE_DIR}/%p-%m.profraw")
  endif()
endfunction()

# The `coverage` target: the profiles of the last test run merged,
# reported, and held to the floors in cmake/CoverageFloors.cmake (R-COV).
function(trl_coverage_target)
  get_property(trl_objects GLOBAL PROPERTY TRL_COVERED_EXECUTABLES)
  set(trl_object_files "")
  foreach(trl_object IN LISTS trl_objects)
    list(APPEND trl_object_files "$<TARGET_FILE:${trl_object}>")
  endforeach()
  list(JOIN trl_object_files "|" trl_joined)
  add_custom_target(
    coverage
    COMMAND
      "${CMAKE_COMMAND}" "-DLLVM_COV=${TRL_LLVM_COV}" "-DLLVM_PROFDATA=${TRL_LLVM_PROFDATA}"
      "-DPROFILE_DIR=${TRL_PROFILE_DIR}" "-DOBJECTS=${trl_joined}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DREPORT_DIR=${CMAKE_BINARY_DIR}/coverage" -P "${PROJECT_SOURCE_DIR}/cmake/coverage-report.cmake"
    VERBATIM)
endfunction()
