// naina — Tensor, DType, Span.
//
// Internal types used by the backend layer and module code. NOT part of the
// public C ABI — bindings should not touch these directly.
#ifndef NAINA_TENSOR_HPP
#define NAINA_TENSOR_HPP

#include "naina.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace naina {

enum class Device { Auto, CPU, GPU, NPU };  // mirror of C enum, scoped form

enum class DType : uint8_t {
    F32 = 0,
    F16,
    BF16,
    I64,
    I32,
    I16,
    I8,
    U8,
    Bool,
};

constexpr size_t dtype_size(DType d) noexcept {
    switch (d) {
        case DType::F32:  return 4;
        case DType::F16:  return 2;
        case DType::BF16: return 2;
        case DType::I64:  return 8;
        case DType::I32:  return 4;
        case DType::I16:  return 2;
        case DType::I8:   return 1;
        case DType::U8:   return 1;
        case DType::Bool: return 1;
    }
    return 0;
}

// Lightweight non-owning view. (std::span is C++20; this is a no-frills fallback
// to avoid dragging <span> through every header.)
template <typename T>
class Span {
public:
    constexpr Span() noexcept = default;
    constexpr Span(T* data, size_t size) noexcept : data_(data), size_(size) {}

    template <size_t N>
    constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    Span(std::vector<std::remove_const_t<T>>& v) noexcept
        : data_(v.data()), size_(v.size()) {}
    Span(const std::vector<std::remove_const_t<T>>& v) noexcept
        : data_(v.data()), size_(v.size()) {}

    constexpr T*     data()  const noexcept { return data_; }
    constexpr size_t size()  const noexcept { return size_; }
    constexpr bool   empty() const noexcept { return size_ == 0; }

    constexpr T& operator[](size_t i) const noexcept { return data_[i]; }
    constexpr T* begin() const noexcept { return data_; }
    constexpr T* end()   const noexcept { return data_ + size_; }

private:
    T*     data_ = nullptr;
    size_t size_ = 0;
};

// Arena allocator for per-inference scratch. Reset between frames; allocations
// are bump-pointer. Not thread-safe; one arena per stream / pipeline.
class Arena {
public:
    explicit Arena(size_t initial_bytes = 4 * 1024 * 1024);
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(size_t bytes, size_t alignment = 16);
    void  reset() noexcept;
    size_t used() const noexcept { return used_; }
    size_t capacity() const noexcept { return cap_; }

private:
    void grow(size_t need);

    std::byte* buf_ = nullptr;
    size_t     used_ = 0;
    size_t     cap_  = 0;
};

// Tensor: shape + dtype + non-owning OR owning buffer. Backends consume tensors
// to feed sessions; postprocessing consumes outputs.
//
// Two construction modes:
//   - `Tensor::view(...)` — wraps external memory, lib does not own.
//   - `Tensor::owned(...)` — heap-allocated, freed on destruction.
//   - `Tensor::in_arena(...)` — bump-allocated from an Arena, no per-tensor free.
class Tensor {
public:
    Tensor() = default;

    static Tensor view(void* data, std::vector<int64_t> shape, DType dtype) {
        Tensor t;
        t.data_  = data;
        t.shape_ = std::move(shape);
        t.dtype_ = dtype;
        t.owned_ = false;
        return t;
    }

    static Tensor owned(std::vector<int64_t> shape, DType dtype);
    static Tensor in_arena(Arena& arena, std::vector<int64_t> shape, DType dtype);

    ~Tensor();
    Tensor(Tensor&& other) noexcept;
    Tensor& operator=(Tensor&& other) noexcept;
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;

    void*       data()       noexcept { return data_; }
    const void* data() const noexcept { return data_; }

    template <typename T> T*       as()       noexcept { return static_cast<T*>(data_); }
    template <typename T> const T* as() const noexcept { return static_cast<const T*>(data_); }

    DType dtype() const noexcept { return dtype_; }
    const std::vector<int64_t>& shape() const noexcept { return shape_; }

    size_t numel() const noexcept {
        if (shape_.empty()) return 0;
        return std::accumulate(shape_.begin(), shape_.end(),
                               size_t{1}, std::multiplies<size_t>());
    }
    size_t nbytes() const noexcept { return numel() * dtype_size(dtype_); }
    bool   empty()  const noexcept { return data_ == nullptr || numel() == 0; }

private:
    void* data_ = nullptr;
    std::vector<int64_t> shape_;
    DType dtype_ = DType::F32;
    bool  owned_ = false;  // free in dtor iff true
};

}  // namespace naina

#endif  // NAINA_TENSOR_HPP
