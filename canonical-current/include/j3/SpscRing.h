#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace j3 {
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "Capacity must be >= 2");
public:
    bool push(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = slots_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }
    bool empty() const noexcept { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }
    std::size_t approximateSize() const noexcept {
        const auto h=head_.load(std::memory_order_acquire), t=tail_.load(std::memory_order_acquire);
        return h>=t?h-t:Capacity-(t-h);
    }
private:
    static constexpr std::size_t increment(std::size_t n) noexcept { return (n + 1) % Capacity; }
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic_size_t head_{0};
    alignas(64) std::atomic_size_t tail_{0};
};
}
