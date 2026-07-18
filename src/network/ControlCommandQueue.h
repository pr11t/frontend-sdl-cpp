#pragma once

#include "network/ControlCommand.h"

#include <cstddef>
#include <deque>
#include <mutex>

class ControlCommandQueue
{
public:
    explicit ControlCommandQueue(std::size_t capacity = 256);

    bool TryEnqueue(ControlCommand command);

    bool TryDequeue(ControlCommand& command);

    std::size_t Size() const;

    std::size_t Capacity() const;

private:
    const std::size_t _capacity;
    mutable std::mutex _mutex;
    std::deque<ControlCommand> _commands;
};
