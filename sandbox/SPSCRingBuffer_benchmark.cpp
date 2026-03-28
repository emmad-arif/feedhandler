#include "SPSCRingBuffer.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

static void run_smoke() {
    constexpr size_t N = 1024;
    SPSCRingBuffer<uint64_t, N> ring;

    assert(ring.empty());
    assert(ring.size() == 0);

    // fill completely
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring.try_push(i));
    }
    assert(ring.full());
    assert(!ring.try_push(99)); // must be full

    // Drain and verify
    for (uint64_t i = 0; i < N; ++i) {
        uint64_t v = 0;
        assert(ring.try_pop(v));
        assert(v == i);
    }
    assert(ring.empty());
    assert(!ring.try_pop(*((uint64_t*)nullptr - 1))); // spurious ptr, avoid UB:

    // redo with a proper empty check
    {
        uint64_t dummy = 0;
        assert(!ring.try_pop(dummy));
    }

    std::cout << "[smoke] passed\n";
}

// producer thread pushes items, consumer thread verifies
static void run_stress(uint64_t count) {
    constexpr size_t kCapacity = 4096;
    SPSCRingBuffer<uint64_t, kCapacity> ring;

    std::atomic<bool> producer_done{false};
    uint64_t consumed  = 0;
    uint64_t checksum  = 0;

    std::thread consumer([&]() {
        uint64_t expected = 0;
        uint64_t value    = 0;
        while (!producer_done.load(std::memory_order_acquire) || !ring.empty()) {
            if (ring.try_pop(value)) {
                if (value != expected) {
                    std::cerr << "[stress] out-of-order: expected "
                              << expected << " got " << value << "\n";
                    std::exit(1);
                }
                ++expected;
                ++consumed;
                checksum += value;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (uint64_t i = 0; i < count; ++i) {
        while (!ring.try_push(i)) {
            std::this_thread::yield();
        }
    }
    producer_done.store(true, std::memory_order_release);
    consumer.join();

    const uint64_t expected_sum = (count - 1) * count / 2;
    assert(consumed == count);
    assert(checksum == expected_sum);
    std::cout << "[stress] passed (" << count << " messages)\n";
}

// measure messages/sec and avg latency
static void run_throughput(uint64_t count) {
    constexpr size_t kCapacity = 4096;
    SPSCRingBuffer<uint64_t, kCapacity> ring;

    std::atomic<bool> ready{false};
    std::atomic<bool> producer_done{false};
    uint64_t consumed = 0;

    std::thread consumer([&]() {
        uint64_t value = 0;
        while (!producer_done.load(std::memory_order_acquire) || !ring.empty()) {
            if (ring.try_pop(value)) {
                ++consumed;
            } else {
                std::this_thread::yield();
            }
        }
    });

    const uint64_t t0 = now_ns();

    for (uint64_t i = 0; i < count; ++i) {
        while (!ring.try_push(i)) {
            std::this_thread::yield();
        }
    }
    producer_done.store(true, std::memory_order_release);
    consumer.join();

    const uint64_t elapsed_ns = now_ns() - t0;
    const double   msgs_per_s = static_cast<double>(count) / (elapsed_ns * 1e-9);
    const double   ns_per_msg = static_cast<double>(elapsed_ns) / count;

    std::cout << "[throughput] " << count << " msgs"
              << "  elapsed=" << elapsed_ns / 1'000'000 << "ms"
              << "  " << std::fixed << std::setprecision(1)
              << msgs_per_s / 1e6 << " Mmsg/s"
              << "  " << ns_per_msg << " ns/msg\n";
}

// producer sends a timestamp, consumer echoes it back via a second ring
// measures end-to-end rtt

static void run_latency(uint64_t count) {
    constexpr size_t kCapacity = 64;
    SPSCRingBuffer<uint64_t, kCapacity> fwd;
    SPSCRingBuffer<uint64_t, kCapacity> bwd;

    std::atomic<bool> stop{false};

    std::thread consumer([&]() {
        uint64_t ts = 0;
        while (!stop.load(std::memory_order_acquire)) {
            if (fwd.try_pop(ts)) {
                while (!bwd.try_push(ts)) { /* spin */ }
            }
        }
    });

    std::vector<uint64_t> samples;
    samples.reserve(count);

    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t t0 = now_ns();
        while (!fwd.try_push(t0)) { /* spin */ }

        uint64_t echo = 0;
        while (!bwd.try_pop(echo)) { /* spin */ }
        samples.push_back(now_ns() - t0);
    }

    stop.store(true, std::memory_order_release);
    consumer.join();

    std::sort(samples.begin(), samples.end());
    auto pct = [&](double p) {
        return samples[static_cast<size_t>(p / 100.0 * samples.size())];
    };

    std::cout << "[latency] " << count << " round-trips"
              << "  p50=" << pct(50)  << "ns"
              << "  p90=" << pct(90)  << "ns"
              << "  p99=" << pct(99)  << "ns"
              << "  max=" << samples.back() << "ns\n";
}

int main() {
    run_smoke();
    run_stress(10'000'000);
    run_throughput(10'000'000);
    run_latency(100'000);
    return 0;
}
