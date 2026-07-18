#include "network/PlaybackState.h"

#include <Poco/Path.h>

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
}
