# Versions

[Contents](README.md)

The library's version, and the protocol's.

## NAME

`trl_version`, `TRL_VERSION_STRING`, `TRL_VERSION_MAJOR`, `TRL_VERSION_MINOR`, `TRL_VERSION_PATCH`, `TRL_PROTOCOL_VERSION`: the version of the library and of the protocol it speaks.

## SYNOPSIS

```c
#include <turborel/turborel.h>

const char *trl_version(void);
```

## DESCRIPTION

`trl_version` gives the version of the library linked, as text. The macros give the version the header declares, its text and its three parts, and the native protocol's version, 1. The version is the turborel release the library is made for ([Appendix D](../appendix/d-versions.md)).

## RETURN VALUE

A string the library owns, valid for as long as the program runs.

## THREADS

May be called from any thread at any time.

## EXAMPLES

```c
printf("libturborel %s, protocol %d\n", trl_version(), TRL_PROTOCOL_VERSION);
```

```
libturborel 0.0.0, protocol 1
```
