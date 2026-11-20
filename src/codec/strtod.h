/**
 * @file strtod.h
 * @brief A decimal numeral read as the nearest double, whatever locale the
 * program has set, and from any thread.
 */
#ifndef TRL_CODEC_STRTOD_H
#define TRL_CODEC_STRTOD_H

#include <stdbool.h>

/**
 * @brief Reads `text`, a numeral with `.` as its decimal point, as `strtod`
 * reads one in the "C" locale: correctly rounded to the nearest double.
 * @param text The numeral, ended by a null.
 * @param out The double.
 * @return Nonzero if read; zero if the "C" locale could not be had.
 */
bool trl_strtod_c(const char *text, double *out);

#endif
