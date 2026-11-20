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
trl_add_test(auth.base64 auth/base64_test.c)
trl_add_test(auth.binding auth/binding_test.c)
trl_add_test(auth.key_cache auth/key_cache_test.c)
trl_add_test(auth.mechanism auth/mechanism_test.c)
trl_add_test(auth.scram auth/scram_test.c)
trl_add_test(conn.login conn/login_test.c conn/script.c conn/scram_server.c)
trl_add_test(conn.session conn/session_test.c conn/script.c)
trl_add_test(conn.edges conn/edges_test.c conn/script.c)
trl_add_test(conn.statements conn/statements_test.c conn/script.c conn/serving.c)
trl_add_test(conn.control conn/control_test.c conn/script.c conn/serving.c conn/scram_server.c)
trl_add_test(conn.lobs conn/lobs_test.c conn/script.c)
trl_add_test(conn.undecoded conn/undecoded_test.c conn/script.c)
trl_add_test(conn.import conn/import_test.c conn/bulk_serving.c conn/script.c)
trl_add_test(conn.bulk conn/bulk_test.c conn/bulk_serving.c conn/script.c)
trl_add_test(conn.role conn/role_test.c conn/script.c)
trl_add_test(api.decimal api/decimal_test.c)
trl_add_test(api.failure api/failure_test.c)
trl_add_test(api.values api/values_test.c)
trl_add_test(api.xml api/xml_test.c)
trl_add_test(data.copy data/copy_test.c codec/corpus.c codec/fixture.c)
target_include_directories(data.copy PRIVATE codec)
trl_add_test(data.text data/text_test.c)
trl_add_test(data.type_text data/type_text_test.c)
trl_add_test(bulk.names bulk/names_test.c)
trl_add_test(bulk.delimited bulk/delimited_test.c)
trl_add_test(bulk.assign bulk/assign_test.c)
trl_add_test(settings.settings settings/settings_test.c settings/settings_common.c)
trl_add_test(settings.edges settings/edges_test.c settings/settings_common.c)
if(TRL_FAULTS)
  if(TRL_HAVE_TLS AND NOT WIN32 AND TRL_TLS STREQUAL "OpenSSL")
    find_package(Threads REQUIRED)
    trl_add_test(api.faults api/faults_test.c api/faults.c conn/script.c conn/scram_server.c net/fake.c server/certs.c)
    target_include_directories(api.faults PRIVATE conn net server)
    target_compile_definitions(api.faults PRIVATE TRL_HAVE_TLS)
    target_link_libraries(api.faults PRIVATE Threads::Threads)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/faults-test")
  else()
    trl_add_test(api.faults api/faults_test.c api/faults.c conn/script.c conn/scram_server.c)
    target_include_directories(api.faults PRIVATE conn)
  endif()
  trl_add_test(api.bulk_faults api/bulk_faults_test.c api/faults.c conn/bulk_serving.c conn/script.c)
  target_include_directories(api.bulk_faults PRIVATE conn)
  trl_add_test(api.settings_faults api/settings_faults_test.c api/faults.c)
endif()
trl_add_test(api.version api/version_test.c)

# Connections over real sockets and TLS settings, without a server: a
# fake of the test's own on a thread, on the POSIX systems (R-PLAT-4).
if(WIN32)
  trl_add_test(net.pipe net/pipe_test.c)
endif()
if(NOT WIN32)
  find_package(Threads REQUIRED)
  trl_add_test(net.socket net/socket_test.c net/fake.c conn/script.c)
  target_include_directories(net.socket PRIVATE conn)
  target_link_libraries(net.socket PRIVATE Threads::Threads)
endif()
if(TRL_HAVE_TLS AND NOT WIN32)
  trl_add_test(net.tls net/tls_test.c net/fake.c conn/script.c server/certs.c)
  target_include_directories(net.tls PRIVATE conn server)
  target_compile_definitions(net.tls PRIVATE TRL_HAVE_TLS)
  target_link_libraries(net.tls PRIVATE Threads::Threads)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tls-test")
endif()

# The live tests, against a real server (R-TEST-8): opt-in by
# TRL_TEST_SERVER, skipped without it. They start turboreld as a child
# process, which only Linux and macOS have here, and make their
# certificates with the TLS library; Windows runs none (R-PLAT-4).
if(TRL_HAVE_TLS AND NOT WIN32)
  trl_add_test(live.connections live/live_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.connections PRIVATE server)
  set_tests_properties(live.connections PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.statements live/statements_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.statements PRIVATE server)
  set_tests_properties(live.statements PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.session live/session_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.session PRIVATE server)
  set_tests_properties(live.session PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.lobs live/lobs_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.lobs PRIVATE server)
  set_tests_properties(live.lobs PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.bulk live/bulk_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.bulk PRIVATE server)
  set_tests_properties(live.bulk PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.import live/import_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.import PRIVATE server)
  set_tests_properties(live.import PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
  trl_add_test(live.settings live/settings_test.c live/live.c server/server.c server/certs.c)
  target_include_directories(live.settings PRIVATE server)
  target_link_libraries(live.settings PRIVATE Threads::Threads)
  set_tests_properties(live.settings PROPERTIES LABELS live SKIP_RETURN_CODE 77 RESOURCE_LOCK turboreld TIMEOUT 300)
endif()

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
