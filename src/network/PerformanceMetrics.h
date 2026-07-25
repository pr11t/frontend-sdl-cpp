#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

struct PerformanceMetricsSnapshot
{
    int targetFps{0};
    double measuredFps{0.0};
    std::size_t sampleCount{0};
    std::uint64_t totalFrames{0};

    double lastFrameMilliseconds{0.0};
    double p50FrameMilliseconds{0.0};
    double p95FrameMilliseconds{0.0};
    double p99FrameMilliseconds{0.0};
    double maxFrameMilliseconds{0.0};

    double lastWorkMilliseconds{0.0};
    double p50WorkMilliseconds{0.0};
    double p95WorkMilliseconds{0.0};
    double p99WorkMilliseconds{0.0};
    double maxWorkMilliseconds{0.0};

    std::size_t missedDeadlines{0};
    double missedDeadlinePercent{0.0};
};

/**
 * Thread-safe rolling frame-performance statistics shared by the render loop
 * and HTTP API.
 */
class PerformanceMetricsStore
{
public:
    void Record(double frameMilliseconds, double workMilliseconds,
                int targetFps, double measuredFps);
    PerformanceMetricsSnapshot GetSnapshot() const;
    void Reset();

private:
    struct Sample
    {
        double frameMilliseconds{0.0};
        double workMilliseconds{0.0};
        bool missedDeadline{false};
    };

    static constexpr std::size_t maximumSamples = 600;

    mutable std::mutex _mutex;
    std::deque<Sample> _samples;
    std::uint64_t _totalFrames{0};
    int _targetFps{0};
    double _measuredFps{0.0};
};
