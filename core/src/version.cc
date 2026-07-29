#include "naina/naina.h"

#define NAINA_STR_(x) #x
#define NAINA_STR(x) NAINA_STR_(x)

// Release builds report a bare semantic version; anything else carries "-dev".
//
// Conditional rather than hardcoded because a distributable artifact must not
// disagree with its own metadata: a wheel whose METADATA says 0.2.0 while
// naina.__version__ says "0.2.0-dev" makes every bug report ambiguous about
// whether the reporter is on a release or a local build. NAINA_RELEASE is set
// by the packaging config (see pyproject.toml) and stays off for local builds.
extern "C" const char* naina_version_string(void) {
#if defined(NAINA_RELEASE)
    return NAINA_STR(NAINA_VERSION_MAJOR) "." NAINA_STR(NAINA_VERSION_MINOR) "." NAINA_STR(
        NAINA_VERSION_PATCH);
#else
    return NAINA_STR(NAINA_VERSION_MAJOR) "." NAINA_STR(NAINA_VERSION_MINOR) "." NAINA_STR(
        NAINA_VERSION_PATCH) "-dev";
#endif
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
