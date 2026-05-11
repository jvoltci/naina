#include "naina/naina.h"

#include <cstdio>
#include <cstring>

int main() {
    const char* v = naina_version_string();
    if (!v || std::strlen(v) == 0) {
        std::fprintf(stderr, "version string is empty\n");
        return 1;
    }
    std::printf("naina version: %s\n", v);

    if (std::strcmp(naina_status_str(NAINA_OK), "ok") != 0) {
        std::fprintf(stderr, "status_str(OK) wrong: %s\n", naina_status_str(NAINA_OK));
        return 2;
    }
    if (std::strcmp(naina_status_str(NAINA_E_OOM), "out of memory") != 0) {
        std::fprintf(stderr, "status_str(OOM) wrong\n");
        return 3;
    }
    return 0;
}
