#include "network/PlaybackState.h"

#include "network/PresetRepository.h"

#include <Poco/Path.h>

void PlaybackStateStore::SetPresetRepository(const PresetRepository* presets)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _presets = presets;
}

PlaybackState PlaybackStateStore::Get() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _state;
}

void PlaybackStateStore::SetCurrentPresetFile(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _state.fileBacked = !filename.empty();
    _state.presetName = filename.empty() ? std::string()
                                         : Poco::Path(filename).getFileName();
    _state.presetId = (_presets && !filename.empty()) ? _presets->ResolveId(filename)
                                                      : std::string();
}
