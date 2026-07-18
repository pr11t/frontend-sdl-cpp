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
    ResetVisualState,
    SetConfig,
    ClearConfig
};

struct ControlCommand
{
    ControlCommandType type;
    bool smoothTransition{false};
    std::uint64_t jobId{0};
    std::string payload;
    VisualStatePatch visualPatch;
    std::string configKey;   //!< Full config key for SetConfig, e.g. "projectM.displayDuration".
    std::string configValue; //!< Canonical string value for SetConfig, applied on the render thread.
};
