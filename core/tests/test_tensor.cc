#include "naina/tensor.hpp"

#include <cstdio>
#include <cstring>
#include <new>

using naina::Arena;
using naina::DType;
using naina::dtype_size;
using naina::Tensor;

static int failures = 0;

#define EXPECT(cond)                                                                   \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static void test_dtype_size() {
    EXPECT(dtype_size(DType::F32) == 4);
    EXPECT(dtype_size(DType::F16) == 2);
    EXPECT(dtype_size(DType::I64) == 8);
    EXPECT(dtype_size(DType::U8) == 1);
    EXPECT(dtype_size(DType::Bool) == 1);
}

static void test_view() {
    float data[6] = {1, 2, 3, 4, 5, 6};
    Tensor t = Tensor::view(data, {2, 3}, DType::F32);
    EXPECT(t.numel() == 6);
    EXPECT(t.nbytes() == 24);
    EXPECT(t.dtype() == DType::F32);
    EXPECT(t.as<float>()[5] == 6.0F);
    EXPECT(t.data() == data);  // zero-copy
}

static void test_owned() {
    Tensor t = Tensor::owned({4, 4}, DType::F32);
    EXPECT(t.numel() == 16);
    EXPECT(t.nbytes() == 64);
    auto* p = t.as<float>();
    EXPECT(p != nullptr);
    for (int i = 0; i < 16; ++i) {
        p[i] = static_cast<float>(i);
    }
    EXPECT(p[15] == 15.0F);
}

static void test_owned_move() {
    Tensor a = Tensor::owned({3}, DType::F32);
    a.as<float>()[0] = 7.0F;
    void* a_data = a.data();
    Tensor b = std::move(a);
    EXPECT(b.data() == a_data);
    EXPECT(b.as<float>()[0] == 7.0F);
    EXPECT(a.data() == nullptr);  // NOLINT(bugprone-use-after-move)
}

static void test_arena() {
    Arena arena(4096);
    EXPECT(arena.capacity() >= 4096);
    EXPECT(arena.used() == 0);

    void* a = arena.allocate(64, 64);
    void* b = arena.allocate(128, 32);
    EXPECT(a != nullptr);
    EXPECT(b != nullptr);
    EXPECT(reinterpret_cast<uintptr_t>(a) % 64 == 0);
    EXPECT(reinterpret_cast<uintptr_t>(b) % 32 == 0);
    EXPECT(arena.used() >= 192);

    arena.reset();
    EXPECT(arena.used() == 0);

    void* c = arena.allocate(64, 64);
    EXPECT(c == a);  // reset returns the same starting address
}

static void test_arena_overflow_throws() {
    Arena arena(256);
    bool threw = false;
    try {
        (void)arena.allocate(1024, 1);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    EXPECT(threw);
}

static void test_in_arena() {
    Arena arena(4096);
    Tensor t = Tensor::in_arena(arena, {10}, DType::F32);
    EXPECT(t.numel() == 10);
    EXPECT(t.nbytes() == 40);
    EXPECT(arena.used() >= 40);
    // No throw on destruction (arena owns the memory).
}

int main() {
    test_dtype_size();
    test_view();
    test_owned();
    test_owned_move();
    test_arena();
    test_arena_overflow_throws();
    test_in_arena();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("tensor tests passed\n");
    return 0;
}
