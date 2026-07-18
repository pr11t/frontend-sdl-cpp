#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

enum class JobState
{
    Queued,
    Running,
    Succeeded,
    Failed
};

struct Job
{
    std::uint64_t id{0};
    std::string operation;
    JobState state{JobState::Queued};
    std::string error;
};

class JobRegistry
{
public:
    explicit JobRegistry(std::size_t capacity = 256);

    std::uint64_t Create(const std::string& operation);
    bool Get(std::uint64_t id, Job& job) const;
    void MarkRunning(std::uint64_t id);
    void Complete(std::uint64_t id, bool success, const std::string& error = "");

private:
    void Trim();

    const std::size_t _capacity;
    mutable std::mutex _mutex;
    std::uint64_t _nextId{1};
    std::map<std::uint64_t, Job> _jobs;
    std::deque<std::uint64_t> _order;
};

const char* JobStateName(JobState state);
