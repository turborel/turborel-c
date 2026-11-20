# Installs libturborel, its header, a CMake package and a pkg-config file
# (RULES.md D8), so a program finds it with find_package(turborel) or
# `pkg-config --cflags --libs turborel`.

include(CMakePackageConfigHelpers)

set(TRL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/turborel")

install(
  TARGETS turborel
  EXPORT turborelTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES include/turborel/turborel.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/turborel")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/turborel")

install(
  EXPORT turborelTargets
  NAMESPACE turborel::
  FILE turborelTargets.cmake
  DESTINATION "${TRL_CMAKE_DIR}")

configure_package_config_file(cmake/turborelConfig.cmake.in "${PROJECT_BINARY_DIR}/turborelConfig.cmake"
                              INSTALL_DESTINATION "${TRL_CMAKE_DIR}")
# A release's driver goes with that release's server alone (RULES.md D9),
# so a program asking for one version takes no other.
write_basic_package_version_file("${PROJECT_BINARY_DIR}/turborelConfigVersion.cmake" COMPATIBILITY ExactVersion)
install(FILES "${PROJECT_BINARY_DIR}/turborelConfig.cmake" "${PROJECT_BINARY_DIR}/turborelConfigVersion.cmake"
        DESTINATION "${TRL_CMAKE_DIR}")

# pkg-config: the static library's users define TRL_STATIC, as the
# header asks.
if(BUILD_SHARED_LIBS)
  set(TRL_PC_CFLAGS "")
else()
  set(TRL_PC_CFLAGS " -DTRL_STATIC")
endif()
# Relocatable: the prefix is found from where the file is, so an install
# moved, or made with `cmake --install --prefix`, is still found.
set(TRL_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(TRL_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
  set(TRL_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(TRL_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
  string(REGEX REPLACE "[^/]+" ".." trl_up "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  set(TRL_PC_PREFIX "\${pcfiledir}/${trl_up}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
  set(TRL_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
configure_file(cmake/turborel.pc.in "${PROJECT_BINARY_DIR}/turborel.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/turborel.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
