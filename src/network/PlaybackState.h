#pragma once

#include <mutex>
#include <string>

class PresetRepository;

struct PlaybackState
{
    std::string presetName;
    std::string presetId;
    bool fileBacked{false};
};

class PlaybackStateStore
{
public:
    void SetPresetRepository(const PresetRepository* presets);
    PlaybackState Get() const;
    void SetCurrentPresetFile(const std::string& filename);

private:
    mutable std::mutex _mutex;
    PlaybackState _state;
    const PresetRepository* _presets{nullptr};
};
