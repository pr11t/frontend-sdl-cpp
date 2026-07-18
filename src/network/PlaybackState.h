#pragma once

#include <mutex>
#include <string>

struct PlaybackState
{
    std::string presetName;
    bool fileBacked{false};
};

class PlaybackStateStore
{
public:
    PlaybackState Get() const;
    void SetCurrentPresetFile(const std::string& filename);

private:
    mutable std::mutex _mutex;
    PlaybackState _state;
};
