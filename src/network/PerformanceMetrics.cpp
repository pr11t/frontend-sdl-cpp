#include "network/PerformanceMetrics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty())
    {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

} // namespace

void PerformanceMetricsStore::Record(double frameMilliseconds, double workMilliseconds,
                                     int targetFps, double measuredFps)
{
    std::lock_guard<std::mutex> lock(_mutex);

    const double deadline = targetFps > 0 ? 1000.0 / targetFps : 0.0;
    _samples.push_back({
        frameMilliseconds,
        workMilliseconds,
        deadline > 0.0 && workMilliseconds > deadline
    });
    if (_samples.size() > maximumSamples)
    {
        _samples.pop_front();
    }

    ++_totalFrames;
    _targetFps = targetFps;
    _measuredFps = measuredFps;
}

PerformanceMetricsSnapshot PerformanceMetricsStore::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    PerformanceMetricsSnapshot snapshot;
    snapshot.targetFps = _targetFps;
    snapshot.measuredFps = _measuredFps;
    snapshot.sampleCount = _samples.size();
    snapshot.totalFrames = _totalFrames;
    if (_samples.empty())
    {
        return snapshot;
    }

    std::vector<double> frameTimes;
    std::vector<double> workTimes;
    frameTimes.reserve(_samples.size());
    workTimes.reserve(_samples.size());
    for (const auto& sample : _samples)
    {
        frameTimes.push_back(sample.frameMilliseconds);
        workTimes.push_back(sample.workMilliseconds);
        if (sample.missedDeadline)
        {
            ++snapshot.missedDeadlines;
        }
    }

    snapshot.lastFrameMilliseconds = _samples.back().frameMilliseconds;
    snapshot.p50FrameMilliseconds = Percentile(frameTimes, 0.50);
    snapshot.p95FrameMilliseconds = Percentile(frameTimes, 0.95);
    snapshot.p99FrameMilliseconds = Percentile(frameTimes, 0.99);
    snapshot.maxFrameMilliseconds = *std::max_element(frameTimes.begin(), frameTimes.end());

    snapshot.lastWorkMilliseconds = _samples.back().workMilliseconds;
    snapshot.p50WorkMilliseconds = Percentile(workTimes, 0.50);
    snapshot.p95WorkMilliseconds = Percentile(workTimes, 0.95);
    snapshot.p99WorkMilliseconds = Percentile(workTimes, 0.99);
    snapshot.maxWorkMilliseconds = *std::max_element(workTimes.begin(), workTimes.end());

    snapshot.missedDeadlinePercent =
        100.0 * static_cast<double>(snapshot.missedDeadlines) /
        static_cast<double>(snapshot.sampleCount);
    return snapshot;
}

void PerformanceMetricsStore::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _samples.clear();
    _totalFrames = 0;
}
