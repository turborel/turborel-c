/**
 * @file consumer.c
 * @brief A program linking the installed libturborel (RULES.md D13): it
 * succeeds if the header and the library it found are of one release.
 */
#include <turborel/turborel.h>

#include <stdlib.h>
#include <string.h>

int main(void) {
    return strcmp(trl_version(), TRL_VERSION_STRING) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
