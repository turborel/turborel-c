# Installs libturborel, its headers, a CMake package and a pkg-config file
# (RULES.md D13), so a program finds it with find_package(turborel) or
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
install(DIRECTORY include/turborel DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/turborel")

install(
  EXPORT turborelTargets
  NAMESPACE turborel::
  FILE turborelTargets.cmake
  DESTINATION "${TRL_CMAKE_DIR}")

# What the static library needs its users to link, which a shared one
# carries itself.
set(TRL_CONFIG_DEPENDENCIES "")
set(trl_pc_modules "")
set(TRL_PC_LIBS_PRIVATE "")
if(TRL_HAVE_ZLIB)
  string(APPEND TRL_CONFIG_DEPENDENCIES "find_dependency(ZLIB)\n")
  list(APPEND trl_pc_modules zlib)
endif()
if(TRL_HAVE_TLS)
  string(APPEND TRL_CONFIG_DEPENDENCIES "find_dependency(OpenSSL)\n")
  list(APPEND trl_pc_modules libssl libcrypto)
endif()
if(WIN32)
  set(TRL_PC_LIBS_PRIVATE "Libs.private: -lws2_32 -lcrypt32")
elseif(APPLE)
  set(TRL_PC_LIBS_PRIVATE "Libs.private: -framework Security -framework CoreFoundation")
else()
  string(APPEND TRL_CONFIG_DEPENDENCIES "find_dependency(Threads)\n")
  set(TRL_PC_LIBS_PRIVATE "Libs.private: -pthread")
endif()
if(BUILD_SHARED_LIBS)
  set(TRL_CONFIG_DEPENDENCIES "")
endif()
set(TRL_PC_REQUIRES "")
if(trl_pc_modules)
  list(JOIN trl_pc_modules " " trl_pc_joined)
  set(TRL_PC_REQUIRES "Requires.private: ${trl_pc_joined}")
endif()

configure_package_config_file(cmake/turborelConfig.cmake.in "${PROJECT_BINARY_DIR}/turborelConfig.cmake"
                              INSTALL_DESTINATION "${TRL_CMAKE_DIR}")
# A release's driver goes with that release's server alone (RULES.md D5),
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
