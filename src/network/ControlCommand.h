#pragma once

#include "network/VisualState.h"
#include "notifications/ToastOptions.h"

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
    ClearConfig,
    ReloadTextures,
    ShowToast
};

struct ControlCommand
{
    ControlCommandType type;
    bool smoothTransition{false};
    std::uint64_t jobId{0};
    std::string payload;     //!< Also carries the toast text for ShowToast.
    VisualStatePatch visualPatch;
    std::string configKey;   //!< Full config key for SetConfig, e.g. "projectM.displayDuration".
    std::string configValue; //!< Canonical string value for SetConfig, applied on the render thread.
    ToastOptions toast;      //!< Options for ShowToast.
    std::uint32_t deckIndex{0}; //!< Target deck for preset/playback commands.
};
