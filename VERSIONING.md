# Versioning

How turborel-c's releases are numbered, and how they are published.

[The version](#the-version) · [Alpha](#alpha) ·
[Where the version is](#where-the-version-is) · [Publishing](#publishing)

---

## The version

A driver's version is the turborel release it is made for: the same
number as the server's, as turborel's
[VERSIONING.md](https://github.com/turborel/turborel/blob/main/VERSIONING.md)
numbers it, **MAJOR.MINOR.PATCH**. A turborel release and each driver's
release share the number, so libturborel 1.2.0 is the driver of turborel
1.2.0. The driver has no version scheme of its own, and what a release
may change follows from its number as turborel's VERSIONING.md says.

## Alpha

turborel-c is in alpha, version **0.0.0**, as turborel is, and speaks the
native protocol's version 1, the only version. While the version is
0.y.z nothing is promised: the C API, its behaviour and the files it
installs may change in any build, as turborel itself may.

## Where the version is

- `TRL_VERSION_MAJOR`, `TRL_VERSION_MINOR` and `TRL_VERSION_PATCH` in
  `include/turborel/turborel.h` are the number, written once, and
  `TRL_VERSION_STRING` is its text. CMake reads the number from there
  (RULES.md D9).
- `trl_version()` gives the number of the library linked, so a program
  can tell a header and a library of different releases apart.
- `TRL_PROTOCOL_VERSION` is the protocol's version the library speaks.
- The shared library carries the number as its version, and its major
  part as its `SOVERSION`; the CMake package and `turborel.pc` carry it
  too. The CMake package asks for an exact version, as a release's driver
  goes with that release's server.

## Publishing

C has no central registry. A release is published as archives on the
repository's GitHub releases: the source, and the library built for
Linux, macOS and Windows. Ports to vcpkg and Conan Center may follow, by
pull request to those projects.

Nothing is published until every turborel driver is done, and then the
owner publishes them together. `.github/workflows/release.yml`, run only
by hand, builds and tests the release on each platform and makes its
archives; its step that publishes them is written but left off, and the
owner turns it on.

A release is made from a clean checkout of a pushed commit that has
passed CI, at the number of the turborel release it is made for.
