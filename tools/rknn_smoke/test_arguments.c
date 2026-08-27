#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arguments.h"

static void Fail(const char* message) {
    fprintf(stderr, "test failure: %s\n", message);
    exit(1);
}

static void Require(bool condition, const char* message) {
    if (!condition) {
        Fail(message);
    }
}

int main(void) {
    RkavArguments arguments = {0};

    char* model_only[] = {"rknn-smoke", "model.rknn"};
    Require(RkavParseArguments(2, model_only, &arguments) == RKAV_ARGUMENTS_OK,
            "model-only arguments parse");
    Require(strcmp(arguments.model_path, "model.rknn") == 0, "model path is retained");
    Require(arguments.iterations == 1U, "model-only invocation defaults to one iteration");
    Require(arguments.input_path == NULL, "model-only invocation has no input path");

    char* repeated[] = {"rknn-smoke", "model.rknn", "10"};
    Require(RkavParseArguments(3, repeated, &arguments) == RKAV_ARGUMENTS_OK,
            "iteration arguments parse");
    Require(arguments.iterations == 10U, "iteration count is parsed without an input path");
    Require(arguments.input_path == NULL, "repeated invocation has no input path");

    char* repeated_with_input[] = {"rknn-smoke", "model.rknn", "10", "frame.rgb"};
    Require(RkavParseArguments(4, repeated_with_input, &arguments) == RKAV_ARGUMENTS_OK,
            "iteration and input arguments parse");
    Require(arguments.iterations == 10U, "iteration count is parsed with an input path");
    Require(strcmp(arguments.input_path, "frame.rgb") == 0, "input path is retained");

    char* zero_iterations[] = {"rknn-smoke", "model.rknn", "0"};
    Require(RkavParseArguments(3, zero_iterations, &arguments) == RKAV_ARGUMENTS_INVALID_ITERATIONS,
            "zero iterations are rejected");

    char* excessive_iterations[] = {"rknn-smoke", "model.rknn", "100001"};
    Require(RkavParseArguments(3, excessive_iterations, &arguments) ==
                RKAV_ARGUMENTS_INVALID_ITERATIONS,
            "excessive iterations are rejected");

    char* malformed_iterations[] = {"rknn-smoke", "model.rknn", "10x", "frame.rgb"};
    Require(RkavParseArguments(4, malformed_iterations, &arguments) ==
                RKAV_ARGUMENTS_INVALID_ITERATIONS,
            "malformed iterations are rejected with an input path");

    Require(RkavParseArguments(1, model_only, &arguments) == RKAV_ARGUMENTS_USAGE_ERROR,
            "missing model is rejected");
    Require(RkavParseArguments(2, model_only, NULL) == RKAV_ARGUMENTS_USAGE_ERROR,
            "missing output structure is rejected");

    puts("rknn_smoke_arguments_test=passed");
    return 0;
}
