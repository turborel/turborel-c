# The tests (RULES.md D14): one executable for each area, on the shared
# harness, each a CTest test; and the rules a tool checks, as tests too.

add_library(trl_test_harness STATIC harness.c)
target_include_directories(trl_test_harness PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
trl_c_target(trl_test_harness)

# The internal headers, for tests that reach what the library hides; the
# tests link the library's objects themselves, as CMake links an object
# library's files only into what names it directly. TRL_STATIC: the
# objects are linked in, not imported from a DLL.
add_library(trl_test_internal INTERFACE)
target_include_directories(trl_test_internal INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(trl_test_internal INTERFACE TRL_STATIC)

# A test executable `name` of `source` and any further sources, run by
# CTest as `name`.
function(trl_add_test name source)
  add_executable(${name} ${source} ${ARGN})
  target_link_libraries(${name} PRIVATE turborel_objects trl_test_harness trl_test_internal)
  trl_c_target(${name})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
  trl_test_environment(${name})
  set_property(GLOBAL APPEND PROPERTY TRL_COVERED_EXECUTABLES ${name})
endfunction()

trl_add_test(codec.frame codec/frame_test.c)
trl_add_test(codec.fields codec/fields_test.c codec/fixture.c)
trl_add_test(codec.types codec/types_test.c codec/fixture.c)
trl_add_test(codec.values codec/values_test.c codec/fixture.c)
trl_add_test(codec.values_write codec/values_write_test.c codec/fixture.c)
trl_add_test(codec.json codec/json_test.c codec/fixture.c)
trl_add_test(codec.json_text codec/json_text_test.c codec/fixture.c)
trl_add_test(codec.json_edges codec/json_edges_test.c codec/fixture.c)
trl_add_test(codec.tail codec/tail_test.c codec/fixture.c)
trl_add_test(codec.requests codec/requests_test.c codec/fixture.c)
trl_add_test(codec.replies codec/replies_test.c codec/fixture.c)
trl_add_test(codec.compress codec/compress_test.c codec/fixture.c)
trl_add_test(codec.sweep codec/sweep_test.c codec/fixture.c codec/corpus.c)
trl_add_test(codec.limits codec/limits_test.c codec/fixture.c codec/corpus.c)
trl_add_test(api.decimal api/decimal_test.c)
trl_add_test(api.failure api/failure_test.c)
trl_add_test(api.version api/version_test.c)

# The source rules a tool can check (R-PROC-1), with no tool but CMake.
add_test(NAME rules.source COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                                   "${PROJECT_SOURCE_DIR}/cmake/check-source.cmake")
set_tests_properties(rules.source PROPERTIES LABELS rules)

if(TRL_LINT)
  add_test(NAME rules.format COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
                                     "-DCLANG_FORMAT=${TRL_CLANG_FORMAT_EXE}" -P
                                     "${PROJECT_SOURCE_DIR}/cmake/check-format.cmake")
  set_tests_properties(rules.format PROPERTIES LABELS rules)
endif()

if(TRL_TEST_PACKAGE)
  add_test(
    NAME package.install
    COMMAND
      "${CMAKE_COMMAND}" "-DBUILD_DIR=${PROJECT_BINARY_DIR}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DCONFIG=$<CONFIG>" "-DC_COMPILER=${CMAKE_C_COMPILER}" "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DSHARED=${BUILD_SHARED_LIBS}" -P "${PROJECT_SOURCE_DIR}/cmake/test-package.cmake")
  set_tests_properties(package.install PROPERTIES LABELS package)
endif()
