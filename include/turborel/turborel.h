/**
 * @file turborel.h
 * @brief libturborel, the C driver for turborel.
 *
 * libturborel connects a C program to a turborel server over turborel's
 * native protocol, version 1, which it speaks itself (RULES.md D1). This
 * header is the one a program includes; it includes the parts of the
 * interface, each in a header of its own under turborel/. Every function
 * is named `trl_` and every macro `TRL_`.
 *
 * This release holds the library's version, its exact numbers, and
 * connections: their targets, TLS, mechanisms and SCRAM key cache, what
 * they ask for and are told, terminate and cancel. Statements and their
 * results come in later releases.
 *
 * @par Version
 * The library's version is the turborel release it is made for, the same
 * number as the server's (VERSIONING.md). The alpha is 0.0.0, and speaks
 * protocol version 1.
 *
 * @par Failures
 * A call that can fail returns a trl_status. TRL_FAILED says the handle it
 * was called on keeps the failure, of SQLCODE, SQLSTATE and message, until
 * the next call on that handle (RULES.md D9).
 *
 * @par Ownership
 * Each handle is made by its own functions and freed by its `_free`
 * function, which takes null and does nothing. A string the library
 * returns is borrowed: valid until the next call on the same handle or
 * until it is freed. What the program passes in is copied, unless a
 * function says it is kept (RULES.md D10).
 *
 * @par Thread safety
 * The library keeps no global mutable state and needs no initialization.
 * A handle is used by one thread at a time; different handles may be used
 * on different threads at once. A key cache may be used by every thread
 * at once, and a canceller cancels a connection's statement from another
 * thread (RULES.md D11). The library starts no thread.
 *
 * @par Linking
 * A program linking the static library defines `TRL_STATIC` before it
 * includes this header; the CMake package and the pkg-config file define
 * it for the program.
 */
#ifndef TRL_TURBOREL_H
#define TRL_TURBOREL_H

#include "turborel/base.h"       // IWYU pragma: export
#include "turborel/connect.h"    // IWYU pragma: export
#include "turborel/connection.h" // IWYU pragma: export
#include "turborel/failure.h"    // IWYU pragma: export
#include "turborel/numbers.h"    // IWYU pragma: export
#include "turborel/values.h"     // IWYU pragma: export
#include "turborel/xml.h"        // IWYU pragma: export

#endif
