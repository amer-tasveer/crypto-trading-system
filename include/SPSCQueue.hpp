#pragma once
#include <vector>
#include <atomic>
#include <cstddef>

// Prevents false sharing by aligning to cache line size
constexpr size_t CACHE_LINE_SIZE = 64;

template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
        : capacity_(next_power_of_2(capacity)), buffer_(capacity_) {
    }

    // Producer thread function
    bool try_push(T&& value) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (capacity_ - 1);

        // Check if queue is full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Full
        }

        buffer_[current_tail] = std::move(value);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer thread function
    bool try_pop(T& value) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Empty
        }

        value = std::move(buffer_[current_head]);
        head_.store((current_head + 1) & (capacity_ - 1), std::memory_order_release);
        return true;
    }

private:
    // Compute next power of 2
    size_t next_power_of_2(size_t n) {
        size_t p = 1;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    const size_t capacity_; // Still const, set in initializer list
    std::vector<T> buffer_;

    // Align to prevent false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
};