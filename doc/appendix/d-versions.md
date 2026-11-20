# Appendix D: Versions

[Contents](README.md)

The driver's version is the number of the Turborel release it is made
for, the same as the server's, and the driver has no version scheme of
its own. This is version 0.0.0, the alpha, made for Turborel 0.0.0, and
it speaks the native protocol's version 1, the only version, which grows
by capabilities rather than by new versions. Until 1.0.0 anything these
manuals describe may change in any release. How releases are numbered
and published is in
[VERSIONING.md](../../VERSIONING.md).

A program reads the version of the library it linked with
`trl_version()`, and that of the header it was compiled with from
`TRL_VERSION_STRING` and its parts, `TRL_VERSION_MAJOR`,
`TRL_VERSION_MINOR` and `TRL_VERSION_PATCH`; `TRL_PROTOCOL_VERSION` is
the protocol's version. The string `trl_version()` gives belongs to the
library and lasts as long as the program, and it may be called from any
thread.

```c
printf("libturborel %s, protocol %d\n", trl_version(), TRL_PROTOCOL_VERSION);
```

```
libturborel 0.0.0, protocol 1
```
