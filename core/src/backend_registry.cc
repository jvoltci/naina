#include "naina/backend.hpp"

#include <array>
#include <mutex>

namespace naina::backend {

namespace {

struct Registry {
    std::mutex mu;
    std::vector<BackendFactory> factories;
    std::vector<std::unique_ptr<IBackend>> instances;
    bool initialized = false;
};

// Function-local static avoids the static-init-order fiasco when
// NAINA_REGISTER_BACKEND fires during dynamic init.
Registry& registry() {
    static Registry r;
    return r;
}

void ensure_initialized(Registry& r) {
    if (r.initialized) {
        return;
    }
    r.instances.reserve(r.factories.size());
    for (auto& factory : r.factories) {
        r.instances.push_back(factory());
    }
    r.initialized = true;
}

}  // namespace

void register_backend(BackendFactory factory) {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    r.factories.push_back(std::move(factory));
    // Late registrations invalidate the cached instances; rebuild on next lookup.
    r.instances.clear();
    r.initialized = false;
}

std::vector<IBackend*> available_backends() {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    ensure_initialized(r);
    std::vector<IBackend*> out;
    out.reserve(r.instances.size());
    for (auto& p : r.instances) {
        if (p && p->available()) {
            out.push_back(p.get());
        }
    }
    return out;
}

IBackend* select_best(naina_backend hint, Device device_hint) {
    (void)device_hint;  // Reserved for per-device selection in v1.1+.

    auto avail = available_backends();
    if (avail.empty()) {
        return nullptr;
    }

    // 1) Honor explicit hint if available.
    if (hint != NAINA_BACKEND_AUTO) {
        for (auto* b : avail) {
            if (b->id() == hint) {
                return b;
            }
        }
        // Asked for something specific that isn't available; refuse to silently
        // substitute. Callers can pass NAINA_BACKEND_AUTO if they want fallback.
        return nullptr;
    }

    // 2) Per-platform priority order. Earlier = preferred.
    static constexpr std::array<naina_backend, 7> kPriority = {{
#if defined(__APPLE__)
        NAINA_BACKEND_COREML,
#endif
        NAINA_BACKEND_TENSORRT,
        NAINA_BACKEND_OPENVINO,
        NAINA_BACKEND_NCNN,
        NAINA_BACKEND_ONNXRUNTIME,
        NAINA_BACKEND_EXECUTORCH,
        NAINA_BACKEND_MNN,
    }};
    for (auto pref : kPriority) {
        for (auto* b : avail) {
            if (b->id() == pref) {
                return b;
            }
        }
    }

    // 3) First available wins.
    return avail.front();
}

}  // namespace naina::backend
