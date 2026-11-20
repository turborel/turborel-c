/**
 * @file strtod.c
 * @brief Numerals read in the "C" locale, through a locale object of the
 * call's own (RULES.md R-PLAT-2), so neither the locale a program set nor
 * a thread that sets one changes what a JSON number is (R-SAFE-5).
 *
 * POSIX has `newlocale` and `strtod_l`, Windows `_create_locale` and
 * `_strtod_l`; glibc declares `strtod_l` for `_GNU_SOURCE` alone.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _GNU_SOURCE
#endif

#include "codec/strtod.h"

/* Each platform declares its locale functions in headers of its own, which
   the include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <xlocale.h>
#endif

#ifdef _WIN32

bool trl_strtod_c(const char *text, double *out) {
    _locale_t c = _create_locale(LC_NUMERIC, "C");
    if (c == NULL) {
        return false;
    }
    *out = _strtod_l(text, NULL, c);
    _free_locale(c);
    return true;
}

#else

bool trl_strtod_c(const char *text, double *out) {
    locale_t c = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (c == (locale_t)0) {
        return false;
    }
    *out = strtod_l(text, NULL, c);
    freelocale(c);
    return true;
}

#endif
// NOLINTEND(misc-include-cleaner)
