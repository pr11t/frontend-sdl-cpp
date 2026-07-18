#include "network/ControlCommandQueue.h"

#include <stdexcept>

ControlCommandQueue::ControlCommandQueue(std::size_t capacity)
    : _capacity(capacity)
{
    if (_capacity == 0)
    {
        throw std::invalid_argument("Control command queue capacity must be greater than zero.");
    }
}

bool ControlCommandQueue::TryEnqueue(ControlCommand command)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_commands.size() >= _capacity)
    {
        return false;
    }

    _commands.push_back(command);
    return true;
}

bool ControlCommandQueue::TryDequeue(ControlCommand& command)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_commands.empty())
    {
        return false;
    }

    command = _commands.front();
    _commands.pop_front();
    return true;
}

std::size_t ControlCommandQueue::Size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _commands.size();
}

std::size_t ControlCommandQueue::Capacity() const
{
    return _capacity;
}
