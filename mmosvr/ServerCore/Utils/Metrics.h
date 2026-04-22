#pragma once

// Lightweight thread-safe metrics for load measurement.
// - Counter    : atomic accumulator (packet/byte counters, per-period exchange-reset).
// - Histogram  : bucketed latency observer (approximate p50/p95/p99 via bucket CDF).
// - ScopedTimer: RAII wrapper measuring duration into a Histogram.
// - ServerMetrics namespace: singleton-ish global instances consumed by MetricsReporter.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>


namespace Metrics
{
    struct Counter
    {
        std::atomic<std::uint64_t> value{0};

        void Add(std::uint64_t n = 1) noexcept
        {
            value.fetch_add(n, std::memory_order_relaxed);
        }

        std::uint64_t Load() const noexcept
        {
            return value.load(std::memory_order_relaxed);
        }

        // 주기적 CSV 덤프용 — 값 읽고 0으로 리셋. relaxed 충분 (덤프 주기가 원자성 기준).
        std::uint64_t Exchange(std::uint64_t v = 0) noexcept
        {
            return value.exchange(v, std::memory_order_relaxed);
        }
    };

    // 버킷 상한 (us). idx i: [bounds[i-1], bounds[i]). 마지막 버킷: [bounds[N-1], +inf).
    inline constexpr std::array<std::uint64_t, 11> kBucketBoundsUs = {
        50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000
    };
    inline constexpr std::size_t kNumBuckets = kBucketBoundsUs.size() + 1;

    struct Histogram
    {
        std::array<std::atomic<std::uint64_t>, kNumBuckets> buckets{};
        std::atomic<std::uint64_t> sumUs{0};
        std::atomic<std::uint64_t> maxUs{0};

        void Observe(std::uint64_t us) noexcept
        {
            std::size_t idx = kNumBuckets - 1;
            for (std::size_t i = 0; i < kBucketBoundsUs.size(); ++i)
            {
                if (us < kBucketBoundsUs[i]) { idx = i; break; }
            }
            buckets[idx].fetch_add(1, std::memory_order_relaxed);
            sumUs.fetch_add(us, std::memory_order_relaxed);

            std::uint64_t cur = maxUs.load(std::memory_order_relaxed);
            while (us > cur &&
                !maxUs.compare_exchange_weak(cur, us, std::memory_order_relaxed))
            {
            }
        }

        struct Snapshot
        {
            std::uint64_t count = 0;
            std::uint64_t sumUs = 0;
            std::uint64_t maxUs = 0;
            std::array<std::uint64_t, kNumBuckets> buckets{};

            double MeanUs() const
            {
                return count ? static_cast<double>(sumUs) / static_cast<double>(count) : 0.0;
            }

            // 버킷 CDF 에서 target = count*p 에 도달하는 버킷의 상한값 반환 (근사).
            double PercentileUs(double p) const
            {
                if (count == 0) return 0.0;
                const std::uint64_t target = static_cast<std::uint64_t>(count * p);
                std::uint64_t acc = 0;
                for (std::size_t i = 0; i < kNumBuckets; ++i)
                {
                    acc += buckets[i];
                    if (acc >= target)
                    {
                        return (i < kBucketBoundsUs.size())
                            ? static_cast<double>(kBucketBoundsUs[i])
                            : static_cast<double>(maxUs);
                    }
                }
                return static_cast<double>(maxUs);
            }
        };

        Snapshot SnapshotAndReset() noexcept
        {
            Snapshot s;
            for (std::size_t i = 0; i < kNumBuckets; ++i)
            {
                s.buckets[i] = buckets[i].exchange(0, std::memory_order_relaxed);
                s.count += s.buckets[i];
            }
            s.sumUs = sumUs.exchange(0, std::memory_order_relaxed);
            s.maxUs = maxUs.exchange(0, std::memory_order_relaxed);
            return s;
        }
    };

    class ScopedTimer
    {
    public:
        explicit ScopedTimer(Histogram& h) noexcept
            : hist_(h), start_(std::chrono::steady_clock::now())
        {
        }

        ~ScopedTimer() noexcept
        {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count();
            hist_.Observe(static_cast<std::uint64_t>(us));
        }

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        Histogram& hist_;
        std::chrono::steady_clock::time_point start_;
    };
}


// 전역 서버 지표 — Reporter 가 주기적으로 스냅샷/리셋.
// inline 변수 (C++17) 로 헤더에서 바로 정의해 링커가 단일 심볼로 머지.
namespace ServerMetrics
{
    inline Metrics::Histogram tickTimeUs;
    inline Metrics::Histogram zoneUpdateUs;
    inline Metrics::Histogram broadcastUs;
    inline Metrics::Histogram packetFlushUs;

    inline Metrics::Counter packetsSent;
    inline Metrics::Counter bytesSent;
    inline Metrics::Counter packetsRecv;
    inline Metrics::Counter bytesRecv;
    inline Metrics::Counter sendCalls;
    inline Metrics::Counter tickOverBudget;

    inline std::atomic<int> currentSessions{0};
    inline std::atomic<int> currentPlayers{0};
    inline std::atomic<int> currentMonsters{0};
    inline std::atomic<int> currentProjectiles{0};
}
