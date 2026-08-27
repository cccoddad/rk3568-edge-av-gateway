#include "arguments.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

static bool ParseIterations(const char* text, uint32_t* value) {
    char* end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0UL ||
        parsed > RKAV_MAXIMUM_ITERATIONS) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

RkavArgumentsStatus RkavParseArguments(int argc, char* const argv[], RkavArguments* arguments) {
    if (argc < 2 || argc > 4 || argv == NULL || arguments == NULL) {
        return RKAV_ARGUMENTS_USAGE_ERROR;
    }

    arguments->model_path = argv[1];
    arguments->iterations = RKAV_DEFAULT_ITERATIONS;
    arguments->input_path = NULL;

    if (argc >= 3 && !ParseIterations(argv[2], &arguments->iterations)) {
        return RKAV_ARGUMENTS_INVALID_ITERATIONS;
    }
    if (argc == 4) {
        arguments->input_path = argv[3];
    }
    return RKAV_ARGUMENTS_OK;
}
