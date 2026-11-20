# Merges the profiles the tests left, reports coverage of src/, writes an
# HTML report, and fails if any total is below its floor (R-COV).
# Run by the `coverage` target of a build configured with TRL_COVERAGE.
#
# Takes: LLVM_COV, LLVM_PROFDATA, PROFILE_DIR, OBJECTS (the test
# executables, separated by |), SOURCE_DIR, REPORT_DIR.

# A script run by -P sets no policies of its own: these are 3.25's, the
# least CMake the driver takes, so each CMake reads it alike.
cmake_minimum_required(VERSION 3.25)

include("${CMAKE_CURRENT_LIST_DIR}/CoverageFloors.cmake")

file(GLOB trl_raw "${PROFILE_DIR}/*.profraw")
if(NOT trl_raw)
  message(FATAL_ERROR "no profile in ${PROFILE_DIR}: run the tests of this build first")
endif()

set(trl_merged "${PROFILE_DIR}/merged.profdata")
execute_process(COMMAND "${LLVM_PROFDATA}" merge -sparse -o "${trl_merged}" ${trl_raw} COMMAND_ERROR_IS_FATAL ANY)
# The next run of the tests starts from none.
file(REMOVE ${trl_raw})

string(REPLACE "|" ";" trl_objects "${OBJECTS}")
list(POP_FRONT trl_objects trl_first)
set(trl_args "${trl_first}")
foreach(trl_object IN LISTS trl_objects)
  list(APPEND trl_args "-object=${trl_object}")
endforeach()
# src/ alone: the tests and the fuzz targets are not what is measured.
list(APPEND trl_args "-instr-profile=${trl_merged}" "-ignore-filename-regex=[/\\\\](test|fuzz)[/\\\\]")

execute_process(COMMAND "${LLVM_COV}" report ${trl_args} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${LLVM_COV}" show ${trl_args} -format=html "-output-dir=${REPORT_DIR}"
                COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${LLVM_COV}" export ${trl_args} -summary-only
  OUTPUT_VARIABLE trl_json COMMAND_ERROR_IS_FATAL ANY)

set(trl_below "")
foreach(trl_kind IN ITEMS lines functions regions branches)
  string(TOUPPER "${trl_kind}" trl_upper)
  string(JSON trl_count GET "${trl_json}" data 0 totals ${trl_kind} count)
  string(JSON trl_percent GET "${trl_json}" data 0 totals ${trl_kind} percent)
  if(trl_count EQUAL 0)
    set(trl_percent 100)
  endif()
  message(STATUS "coverage of ${trl_kind}: ${trl_percent}% (floor ${TRL_FLOOR_${trl_upper}}%)")
  if(trl_percent LESS TRL_FLOOR_${trl_upper})
    list(APPEND trl_below "${trl_kind} ${trl_percent}% < ${TRL_FLOOR_${trl_upper}}%")
  endif()
endforeach()

message(STATUS "HTML report: ${REPORT_DIR}/index.html")
if(trl_below)
  list(JOIN trl_below ", " trl_text)
  message(FATAL_ERROR "coverage below its floor (R-COV-2): ${trl_text}")
endif()
