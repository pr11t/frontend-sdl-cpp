#include "network/PlaybackState.h"

#include "network/PresetRepository.h"

#include <Poco/Path.h>

#include <algorithm>

void PlaybackStateStore::SetPresetRepository(const PresetRepository* presets)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _presets = presets;
}

void PlaybackStateStore::SetDeckCount(std::size_t count)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _states.resize(std::max<std::size_t>(count, 1));
}

std::size_t PlaybackStateStore::DeckCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _states.size();
}

PlaybackState PlaybackStateStore::Get(std::size_t deck) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (deck >= _states.size())
    {
        return {};
    }
    return _states[deck];
}

void PlaybackStateStore::SetCurrentPresetFile(std::size_t deck, const std::string& filename)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (deck >= _states.size())
    {
        return;
    }
    auto& state = _states[deck];
    state.fileBacked = !filename.empty();
    state.presetName = filename.empty() ? std::string() : Poco::Path(filename).getFileName();
    state.presetId = (_presets && !filename.empty()) ? _presets->ResolveId(filename) : std::string();
}
