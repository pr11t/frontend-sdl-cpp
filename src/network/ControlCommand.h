#pragma once

#include "network/VisualState.h"

#include <cstdint>
#include <string>

enum class ControlCommandType
{
    NextPreset,
    PreviousPreset,
    RandomPreset,
    LoadPresetFile,
    ReloadCurrentPreset,
    LoadPresetSource,
    UpdateVisualState,
    ResetVisualState
};

struct ControlCommand
{
    ControlCommandType type;
    bool smoothTransition{false};
    std::uint64_t jobId{0};
    std::string payload;
    VisualStatePatch visualPatch;
};
