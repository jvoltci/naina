#include "naina/naina.h"

#define NAINA_STR_(x) #x
#define NAINA_STR(x) NAINA_STR_(x)

extern "C" const char* naina_version_string(void) {
    return NAINA_STR(NAINA_VERSION_MAJOR) "." NAINA_STR(NAINA_VERSION_MINOR) "." NAINA_STR(
        NAINA_VERSION_PATCH) "-dev";
}

extern "C" const char* naina_status_str(naina_status s) {
    switch (s) {
        case NAINA_OK:
            return "ok";
        case NAINA_E_INVALID_ARG:
            return "invalid argument";
        case NAINA_E_NOT_INITIALIZED:
            return "not initialized";
        case NAINA_E_MODEL_NOT_FOUND:
            return "model not found";
        case NAINA_E_BACKEND_UNAVAIL:
            return "backend unavailable";
        case NAINA_E_INFERENCE_FAILED:
            return "inference failed";
        case NAINA_E_OOM:
            return "out of memory";
        case NAINA_E_UNSUPPORTED:
            return "unsupported";
        case NAINA_E_IO:
            return "io error";
    }
    return "unknown";
}
