#pragma once

enum class ControlCommandType
{
    NextPreset,
    PreviousPreset
};

struct ControlCommand
{
    ControlCommandType type;
    bool smoothTransition{false};
};
