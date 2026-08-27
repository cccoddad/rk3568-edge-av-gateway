#ifndef RKAV_RKNN_SMOKE_ARGUMENTS_H
#define RKAV_RKNN_SMOKE_ARGUMENTS_H

#include <stdint.h>

enum {
    RKAV_DEFAULT_ITERATIONS = 1,
    RKAV_MAXIMUM_ITERATIONS = 100000,
};

typedef struct {
    const char* model_path;
    uint32_t iterations;
    const char* input_path;
} RkavArguments;

typedef enum {
    RKAV_ARGUMENTS_OK = 0,
    RKAV_ARGUMENTS_USAGE_ERROR = 1,
    RKAV_ARGUMENTS_INVALID_ITERATIONS = 2,
} RkavArgumentsStatus;

RkavArgumentsStatus RkavParseArguments(int argc, char* const argv[], RkavArguments* arguments);

#endif
