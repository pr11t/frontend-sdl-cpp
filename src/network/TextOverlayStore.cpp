#include "network/TextOverlayStore.h"

#include <utility>

void TextOverlayStore::Set(const std::string& name, TextOverlay overlay)
{
    std::lock_guard<std::mutex> lock(_mutex);
    overlay.name = name;
    _overlays[name] = std::move(overlay);
}

bool TextOverlayStore::Find(const std::string& name, TextOverlay& overlay) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto entry = _overlays.find(name);
    if (entry == _overlays.end())
    {
        return false;
    }
    overlay = entry->second;
    return true;
}

bool TextOverlayStore::Remove(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _overlays.erase(name) > 0;
}

std::size_t TextOverlayStore::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto count = _overlays.size();
    _overlays.clear();
    return count;
}

std::vector<TextOverlay> TextOverlayStore::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<TextOverlay> overlays;
    overlays.reserve(_overlays.size());
    for (const auto& entry : _overlays)
    {
        overlays.push_back(entry.second);
    }
    return overlays;
}
