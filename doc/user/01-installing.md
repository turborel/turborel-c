# Installing

[Contents](README.md)

This chapter will tell what the driver needs, a C17 compiler, GCC, Clang or MSVC, and CMake 3.25 or later on Linux,
macOS or Windows, and how a program takes `libturborel`, found as the CMake package `turborel` or by `turborel.pc` in from
archives on the repository's GitHub releases. No package is published until every Turborel driver is
done; until then the driver is built from its source, as the
repository's
[README](../../README.md)
says. The chapter is written with the standard API, at milestone M7, and
again when the packages are published.

## Building from source

The driver builds with a C17 compiler, GCC, Clang or MSVC, and CMake 3.25
or later. OpenSSL 3.5 or later, or AWS-LC, and zlib are looked for, and
used once the parts that need them are built.

```
git clone https://github.com/turborel/turborel-c.git
cd turborel-c
cmake --workflow --preset release          # configure, build and test
cmake --install build/release --prefix /usr/local
```

A program then finds the library with CMake,

```cmake
find_package(turborel REQUIRED)
target_link_libraries(app PRIVATE turborel::turborel)
```

or with pkg-config, `pkg-config --cflags --libs turborel`. A program
linking the static library, built with `-DBUILD_SHARED_LIBS=OFF`, has
`TRL_STATIC` defined for it by either.
