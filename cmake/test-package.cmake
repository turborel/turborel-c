# The installed package, used as a program outside the tree uses it
# (RULES.md D13), run by CTest as `package.install` in a build configured
# with TRL_TEST_PACKAGE: the build installed to a directory of its own,
# then test/package/ configured against it by find_package(turborel),
# built and run; and, where pkg-config is, but for Windows, the program
# compiled again by its flags alone.
#
# Takes: BUILD_DIR, SOURCE_DIR, CONFIG, C_COMPILER, GENERATOR, SHARED.

# A script run by -P sets no policies of its own: these are 3.25's, the
# least CMake the driver takes, so each CMake reads it alike.
cmake_minimum_required(VERSION 3.25)

set(trl_stage "${BUILD_DIR}/package-test/stage")
set(trl_consumer "${BUILD_DIR}/package-test/consumer")
file(REMOVE_RECURSE "${BUILD_DIR}/package-test")

execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}" --prefix "${trl_stage}"
                COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/test/package" -B "${trl_consumer}" -G "${GENERATOR}"
          "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_PREFIX_PATH=${trl_stage}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${trl_consumer}" --config "${CONFIG}" COMMAND_ERROR_IS_FATAL
                                                                                              ANY)

# The program finds the shared library where it was installed.
if(WIN32)
  set(ENV{PATH} "${trl_stage}/bin;$ENV{PATH}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${trl_consumer}" -C "${CONFIG}" --output-on-failure
                COMMAND_ERROR_IS_FATAL ANY)

# MSVC takes no pkg-config flags, and a pkg-config there may be another
# implementation's; the CMake package above is the way on Windows.
if(WIN32)
  return()
endif()
find_program(trl_pkg_config pkg-config)
if(NOT trl_pkg_config)
  message(STATUS "pkg-config not found: turborel.pc is installed, not tried")
  return()
endif()
file(GLOB_RECURSE trl_pc "${trl_stage}/*/pkgconfig/turborel.pc")
get_filename_component(trl_pc_dir "${trl_pc}" DIRECTORY)
set(ENV{PKG_CONFIG_PATH} "${trl_pc_dir}")
execute_process(
  COMMAND "${trl_pkg_config}" --cflags --libs turborel
  OUTPUT_VARIABLE trl_flags
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "pkg-config turborel: ${trl_flags}")
separate_arguments(trl_flag_list UNIX_COMMAND "${trl_flags}")
set(trl_program "${BUILD_DIR}/package-test/by-pkg-config")
execute_process(COMMAND "${C_COMPILER}" -std=c17 "${SOURCE_DIR}/test/package/consumer.c" ${trl_flag_list} -o
                        "${trl_program}" COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "-L([^ ]+)" trl_libdir "${trl_flags}")
set(ENV{LD_LIBRARY_PATH} "${CMAKE_MATCH_1}")
set(ENV{DYLD_LIBRARY_PATH} "${CMAKE_MATCH_1}")
execute_process(COMMAND "${trl_program}" COMMAND_ERROR_IS_FATAL ANY)
