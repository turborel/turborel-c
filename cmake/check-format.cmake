# Every C source and header formatted as .clang-format says (R-STYLE-1),
# run by CTest as `rules.format` in a build configured with TRL_LINT.
# `clang-format -i` on a file formats it.
#
# Takes: SOURCE_DIR, CLANG_FORMAT.

# A script run by -P sets no policies of its own: these are 3.25's, the
# least CMake the driver takes, so each CMake reads it alike.
cmake_minimum_required(VERSION 3.25)

file(GLOB_RECURSE trl_files "${SOURCE_DIR}/include/*.h" "${SOURCE_DIR}/src/*.[ch]" "${SOURCE_DIR}/test/*.[ch]"
     "${SOURCE_DIR}/fuzz/*.[ch]")
list(SORT trl_files)
execute_process(
  COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${trl_files}
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE trl_result)
if(NOT trl_result EQUAL 0)
  message(FATAL_ERROR "files not formatted as .clang-format says (R-STYLE-1); `clang-format -i FILE` formats one")
endif()
list(LENGTH trl_files trl_count)
message(STATUS "${trl_count} files formatted as .clang-format says")
