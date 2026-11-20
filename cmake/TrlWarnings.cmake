# The language level and the warnings every target of ours is built with
# (RULES.md D3, D4, R-LINT-1): C17 without extensions, every warning
# listed here an error.

# Warnings beyond -Wall -Wextra -Wpedantic that every GCC and Clang we
# support has: conversions that lose a value or a sign, shadowing,
# prototypes, qualifiers cast away, format strings, VLAs.
set(TRL_GNU_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wold-style-definition
    -Wcast-qual
    -Wcast-align
    -Wformat=2
    -Wundef
    -Wvla
    -Wwrite-strings
    -Wpointer-arith
    -Wdouble-promotion
    -Wswitch-enum)

# Clang's own: doc comments checked against what they document (R-DOC-1).
set(TRL_CLANG_WARNINGS -Wdocumentation -Wcomma -Wunreachable-code -Wassign-enum)

# GCC's own: logic that repeats itself.
set(TRL_GCC_WARNINGS -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init)

# MSVC: level 4, as errors; sources are UTF-8.
set(TRL_MSVC_WARNINGS /W4 /WX /utf-8)

# Builds `target` as C17, with the warnings above as errors.
function(trl_c_target target)
  set_target_properties(
    ${target}
    PROPERTIES C_STANDARD 17
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF
               C_VISIBILITY_PRESET hidden)
  if(MSVC)
    target_compile_options(${target} PRIVATE ${TRL_MSVC_WARNINGS})
  else()
    target_compile_options(
      ${target} PRIVATE ${TRL_GNU_WARNINGS} "$<$<C_COMPILER_ID:Clang,AppleClang>:${TRL_CLANG_WARNINGS}>"
                        "$<$<C_COMPILER_ID:GNU>:${TRL_GCC_WARNINGS}>")
  endif()
endfunction()
