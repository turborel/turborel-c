# The turborel-c Documentation Set

turborel-c is the C driver for turborel, a relational database system: `libturborel`, a C17 library that connects a program to a turborel server over its native protocol, with a libpq-style C API and an ODBC driver over it.

turborel-c is in alpha: what these manuals describe may change in any release until 1.0.0 ([Versions](appendix/d-versions.md)). Each change updates the pages it touches, so the manuals describe the release they come with.

## The volumes

| Volume | For | |
|---|---|---|
| [I. User's Guide](user/README.md) | Anyone writing a C program against turborel | Building, connecting, queries, transactions, and the rest, chapter by chapter |
| [II. API Reference](client/README.md) | The same, looking something up | Each handle and group of functions of `turborel/turborel.h`, and the ODBC driver |
| [Appendices](appendix/README.md) | Everyone | Types, errors, settings, versions |

## Conventions

**Code** is C17, set in a fixed-width font; its output follows it, as the program prints it. Every function of the library begins `trl_`, every macro `TRL_`.

**Citations** name a section of turborel's protocol spec, as [PROTOCOL Frames], or of turborel-client's manual, as [CLIENT Pipelines].

**Errors** are given by status code, SQLSTATE and SQLCODE ([Appendix B](appendix/b-errors.md)).
