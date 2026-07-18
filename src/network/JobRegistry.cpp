#include "network/JobRegistry.h"

#include <stdexcept>

JobRegistry::JobRegistry(std::size_t capacity)
    : _capacity(capacity)
{
    if (_capacity == 0)
    {
        throw std::invalid_argument("Job registry capacity must be greater than zero.");
    }
}

std::uint64_t JobRegistry::Create(const std::string& operation)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto id = _nextId++;
    _jobs.emplace(id, Job{id, operation, JobState::Queued, ""});
    _order.push_back(id);
    Trim();
    return id;
}

bool JobRegistry::Get(std::uint64_t id, Job& job) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto found = _jobs.find(id);
    if (found == _jobs.end())
    {
        return false;
    }
    job = found->second;
    return true;
}

void JobRegistry::MarkRunning(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _jobs.find(id);
    if (found != _jobs.end())
    {
        found->second.state = JobState::Running;
    }
}

void JobRegistry::Complete(std::uint64_t id, bool success, const std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _jobs.find(id);
    if (found != _jobs.end())
    {
        found->second.state = success ? JobState::Succeeded : JobState::Failed;
        found->second.error = error;
    }
}

void JobRegistry::Trim()
{
    while (_order.size() > _capacity)
    {
        _jobs.erase(_order.front());
        _order.pop_front();
    }
}

const char* JobStateName(JobState state)
{
    switch (state)
    {
        case JobState::Queued:
            return "queued";
        case JobState::Running:
            return "running";
        case JobState::Succeeded:
            return "succeeded";
        case JobState::Failed:
            return "failed";
    }
    return "unknown";
}
