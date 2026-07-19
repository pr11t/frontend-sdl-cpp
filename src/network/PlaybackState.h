#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class PresetRepository;

struct PlaybackState
{
    std::string presetName;
    std::string presetId;
    bool fileBacked{false};
};

/**
 * @brief Per-deck "current preset" state, updated on the render thread and read
 * by the API. Deck 0 exists by default; SetDeckCount sizes the rest.
 */
class PlaybackStateStore
{
public:
    void SetPresetRepository(const PresetRepository* presets);
    void SetDeckCount(std::size_t count);
    std::size_t DeckCount() const;

    PlaybackState Get(std::size_t deck = 0) const;
    void SetCurrentPresetFile(std::size_t deck, const std::string& filename);

private:
    mutable std::mutex _mutex;
    std::vector<PlaybackState> _states{1};
    const PresetRepository* _presets{nullptr};
};
