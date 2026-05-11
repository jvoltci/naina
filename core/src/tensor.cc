#include "naina/tensor.hpp"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace naina {

// ── Arena ────────────────────────────────────────────────────────────

namespace {
constexpr size_t kArenaAlign = 64;
size_t round_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
}  // namespace

Arena::Arena(size_t initial_bytes) {
    cap_ = round_up(initial_bytes, kArenaAlign);
    if (cap_ == 0) {
        return;
    }
    buf_ = static_cast<std::byte*>(std::aligned_alloc(kArenaAlign, cap_));
    if (buf_ == nullptr) {
        cap_ = 0;
        throw std::bad_alloc{};
    }
}

Arena::~Arena() {
    if (buf_ != nullptr) {
        std::free(buf_);
    }
}

void* Arena::allocate(size_t bytes, size_t alignment) {
    const size_t aligned = round_up(used_, alignment);
    if (aligned + bytes > cap_) {
        // Growth would invalidate any already-handed-out pointers, which
        // breaks the arena contract. The caller must pre-size adequately.
        throw std::bad_alloc{};
    }
    used_ = aligned + bytes;
    return buf_ + aligned;
}

void Arena::reset() noexcept { used_ = 0; }

void Arena::grow(size_t need) {
    // Reserved for a future chunked-arena impl; intentionally unused for now.
    (void)need;
}

// ── Tensor ───────────────────────────────────────────────────────────

Tensor Tensor::owned(std::vector<int64_t> shape, DType dtype) {
    Tensor t;
    t.shape_ = std::move(shape);
    t.dtype_ = dtype;
    const size_t n = t.nbytes();
    if (n == 0) {
        t.data_ = nullptr;
        t.owned_ = false;
        return t;
    }
    const size_t padded = round_up(n, kArenaAlign);
    t.data_ = std::aligned_alloc(kArenaAlign, padded);
    if (t.data_ == nullptr) {
        throw std::bad_alloc{};
    }
    t.owned_ = true;
    return t;
}

Tensor Tensor::in_arena(Arena& arena, std::vector<int64_t> shape, DType dtype) {
    Tensor t;
    t.shape_ = std::move(shape);
    t.dtype_ = dtype;
    const size_t n = t.nbytes();
    t.data_ = n != 0 ? arena.allocate(n, kArenaAlign) : nullptr;
    t.owned_ = false;
    return t;
}

Tensor::~Tensor() {
    if (owned_ && data_ != nullptr) {
        std::free(data_);
    }
}

Tensor::Tensor(Tensor&& other) noexcept
    : data_(other.data_),
      shape_(std::move(other.shape_)),
      dtype_(other.dtype_),
      owned_(other.owned_) {
    other.data_ = nullptr;
    other.owned_ = false;
}

Tensor& Tensor::operator=(Tensor&& other) noexcept {
    if (this != &other) {
        if (owned_ && data_ != nullptr) {
            std::free(data_);
        }
        data_ = other.data_;
        shape_ = std::move(other.shape_);
        dtype_ = other.dtype_;
        owned_ = other.owned_;
        other.data_ = nullptr;
        other.owned_ = false;
    }
    return *this;
}

}  // namespace naina
