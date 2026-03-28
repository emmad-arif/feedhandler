#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T, size_t Capacity>
class SPSCRingBuffer {

public:
    SPSCRingBuffer() = default;

    // returns false if the buffer is full
    bool try_push(const T& item) noexcept;

    // returns false if the buffer is empty
    bool try_pop(T& item) noexcept;

    size_t size() const noexcept;

    bool empty() const noexcept;
    bool full() const noexcept;

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static constexpr size_t kMask = Capacity - 1;

    std::atomic<uint64_t> write_idx_{0};

    std::atomic<uint64_t> read_idx_{0};

    T buffer_[Capacity];
};

template <typename T, size_t Capacity>
bool SPSCRingBuffer<T, Capacity>::try_push(const T& item) noexcept {
    const uint64_t w = write_idx_.load(std::memory_order_relaxed);
    const uint64_t r = read_idx_.load(std::memory_order_acquire);

    if (w - r >= Capacity) {
        return false; // full
    }

    buffer_[w & kMask] = item;
    write_idx_.store(w + 1, std::memory_order_release);
    return true;
}

template <typename T, size_t Capacity>
bool SPSCRingBuffer<T, Capacity>::try_pop(T& item) noexcept {
    const uint64_t r = read_idx_.load(std::memory_order_relaxed);
    const uint64_t w = write_idx_.load(std::memory_order_acquire);

    if (r >= w) {
        return false; // empty
    }

    item = buffer_[r & kMask];
    read_idx_.store(r + 1, std::memory_order_release);
    return true;
}

template <typename T, size_t Capacity>
size_t SPSCRingBuffer<T, Capacity>::size() const noexcept {
    const uint64_t w = write_idx_.load(std::memory_order_acquire);
    const uint64_t r = read_idx_.load(std::memory_order_acquire);
    return static_cast<size_t>(w - r);
}

template <typename T, size_t Capacity>
bool SPSCRingBuffer<T, Capacity>::empty() const noexcept {
    return size() == 0;
}

template <typename T, size_t Capacity>
bool SPSCRingBuffer<T, Capacity>::full() const noexcept {
    return size() >= Capacity;
}
