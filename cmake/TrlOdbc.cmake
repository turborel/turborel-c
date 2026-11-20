# The ODBC 3.x driver, libturborelodbc (RULES.md D2, D12): built where
# ODBC's headers are found, the Windows SDK's, or unixODBC's or iODBC's
# elsewhere, with the installer library a driver reads a data source's
# attributes by: odbccp32 on Windows, odbcinst or iodbcinst elsewhere.
# The driver links the library's objects in, so it is one file a driver
# manager loads.

option(TRL_ODBC "Build the ODBC driver, libturborelodbc, where ODBC's headers are found" ON)

set(TRL_HAVE_ODBC OFF)
if(TRL_ODBC)
  if(WIN32)
    set(TRL_HAVE_ODBC ON)
    set(TRL_ODBC_INCLUDE_DIR "")
    set(TRL_ODBCINST_LIBRARY odbccp32 legacy_stdio_definitions)
    message(STATUS "ODBC driver: the Windows SDK's ODBC")
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(TRL_ODBC_PC QUIET odbc)
    endif()
    find_path(
      TRL_ODBC_INCLUDE_DIR sql.h
      HINTS ${TRL_ODBC_PC_INCLUDE_DIRS}
      PATHS /opt/homebrew/include /usr/local/include /usr/include)
    find_library(
      TRL_ODBCINST_LIBRARY
      NAMES odbcinst iodbcinst
      HINTS ${TRL_ODBC_PC_LIBRARY_DIRS}
      PATHS /opt/homebrew/lib /usr/local/lib)
    if(TRL_ODBC_INCLUDE_DIR AND TRL_ODBCINST_LIBRARY)
      set(TRL_HAVE_ODBC ON)
      message(STATUS "ODBC driver: headers in ${TRL_ODBC_INCLUDE_DIR}, ${TRL_ODBCINST_LIBRARY}")
    else()
      message(STATUS "ODBC driver: not built; ODBC's headers and installer library, as unixODBC's, not found")
    endif()
  endif()
else()
  message(STATUS "ODBC driver: not built, as TRL_ODBC asks")
endif()

# The driver's objects, built once for the driver and for its tests.
function(trl_odbc_targets)
  add_library(
    turborelodbc_objects OBJECT
    src/odbc/attrs.c
    src/odbc/catalog.c
    src/odbc/catalog_more.c
    src/odbc/catalog_types.c
    src/odbc/connect.c
    src/odbc/convert.c
    src/odbc/convert_datetime.c
    src/odbc/convert_scalar.c
    src/odbc/desc.c
    src/odbc/describe.c
    src/odbc/diag.c
    src/odbc/escapes.c
    src/odbc/execute.c
    src/odbc/fetch.c
    src/odbc/handles.c
    src/odbc/info.c
    src/odbc/modes.c
    src/odbc/params.c
    src/odbc/text.c
    src/odbc/types.c
    src/odbc/wide.c
    src/odbc/wide_catalog.c)
  target_include_directories(turborelodbc_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  if(TRL_ODBC_INCLUDE_DIR)
    target_include_directories(turborelodbc_objects SYSTEM PUBLIC "${TRL_ODBC_INCLUDE_DIR}")
  endif()
  target_link_libraries(turborelodbc_objects PUBLIC turborel_objects ${TRL_ODBCINST_LIBRARY})
  # The C library's mathematics, a library of its own on Linux.
  if(NOT WIN32)
    target_link_libraries(turborelodbc_objects PUBLIC m)
  endif()
  trl_c_target(turborelodbc_objects)
  set_target_properties(turborelodbc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

  # A module a driver manager loads by its path, not a library a program links.
  add_library(turborelodbc MODULE $<TARGET_OBJECTS:turborel_objects>)
  target_link_libraries(turborelodbc PRIVATE turborelodbc_objects)
  set_target_properties(turborelodbc PROPERTIES PREFIX lib OUTPUT_NAME turborelodbc C_VISIBILITY_PRESET hidden)
  # ODBC's functions alone are exported, the C API's within kept the
  # module's own, so a program that links libturborel too keeps its own.
  if(WIN32)
    target_sources(turborelodbc PRIVATE src/odbc/turborelodbc.def)
  elseif(APPLE)
    target_link_options(turborelodbc PRIVATE "LINKER:-exported_symbol,_SQL*")
  else()
    target_link_options(turborelodbc PRIVATE
                        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/odbc/turborelodbc.map")
  endif()
  install(
    TARGETS turborelodbc
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()
