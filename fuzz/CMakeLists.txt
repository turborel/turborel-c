# The libFuzzer targets of the codec (RULES.md D4). TRL_FUZZ builds
# everything instrumented and sanitized (cmake/TrlChecks.cmake); these
# targets link libFuzzer, which LLVM's Clang ships and Apple's does not.

# A fuzz target `name` of `source`, over the library's objects.
function(trl_add_fuzzer name source)
  add_executable(${name} ${source} $<TARGET_OBJECTS:turborel_objects>)
  if(TRL_HAVE_ZLIB)
    target_link_libraries(${name} PRIVATE ZLIB::ZLIB)
  endif()
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
  target_compile_definitions(${name} PRIVATE TRL_STATIC)
  trl_c_target(${name})
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
endfunction()

trl_add_fuzzer(fuzz.frame frame_fuzz.c)
