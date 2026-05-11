#include "naina/backend.hpp"

#include <cstdio>
#include <memory>
#include <string_view>

using naina::backend::available_backends;
using naina::backend::IBackend;
using naina::backend::ISession;
using naina::backend::register_backend;
using naina::backend::select_best;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Stub backend used to verify the registry mechanics independently of any
// real inference engine.
class StubBackend final : public IBackend {
public:
    explicit StubBackend(naina_backend id, bool avail = true) : id_(id), avail_(avail) {}

    std::string_view name() const override { return "stub"; }
    naina_backend id() const override { return id_; }
    bool available() const override { return avail_; }
    std::unique_ptr<ISession> load(const std::filesystem::path&,
                                   const naina::backend::SessionOptions&,
                                   naina_status* out_status) override {
        if (out_status != nullptr) {
            *out_status = NAINA_E_UNSUPPORTED;
        }
        return nullptr;
    }

private:
    naina_backend id_;
    bool avail_;
};

int main() {
    // Register two stubs: one available, one not.
    register_backend([] { return std::unique_ptr<IBackend>(new StubBackend(NAINA_BACKEND_NCNN)); });
    register_backend(
        [] { return std::unique_ptr<IBackend>(new StubBackend(NAINA_BACKEND_MNN, false)); });

    auto avail = available_backends();
    EXPECT(avail.size() >= 1);  // at least the NCNN stub; ORT may also be linked in

    // Honor explicit hint.
    auto* picked = select_best(NAINA_BACKEND_NCNN, naina::Device::Auto);
    EXPECT(picked != nullptr);
    if (picked != nullptr) {
        EXPECT(picked->id() == NAINA_BACKEND_NCNN);
    }

    // Refuse to substitute when the hinted backend isn't available.
    auto* refused = select_best(NAINA_BACKEND_MNN, naina::Device::Auto);
    EXPECT(refused == nullptr);

    // AUTO always returns something when at least one is available.
    auto* auto_pick = select_best(NAINA_BACKEND_AUTO, naina::Device::Auto);
    EXPECT(auto_pick != nullptr);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("backend_registry tests passed\n");
    return 0;
}
