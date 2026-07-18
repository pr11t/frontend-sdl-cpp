#include "network/TextureStore.h"

#include <Poco/String.h>

#include <utility>

namespace {

std::string LowerKey(const std::string& name)
{
    return Poco::toLower(name);
}

} // namespace

void TextureStore::Set(const std::string& name, DecodedImagePtr image)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (image)
    {
        _textures[LowerKey(name)] = std::move(image);
    }
    else
    {
        _textures.erase(LowerKey(name));
    }
    ++_generation;
}

bool TextureStore::Remove(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const bool removed = _textures.erase(LowerKey(name)) > 0;
    if (removed)
    {
        ++_generation;
    }
    return removed;
}

std::size_t TextureStore::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto count = _textures.size();
    _textures.clear();
    ++_generation;
    return count;
}

DecodedImagePtr TextureStore::Find(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _textures.find(LowerKey(name));
    return it == _textures.end() ? nullptr : it->second;
}

std::uint64_t TextureStore::Generation() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _generation;
}

std::vector<TextureStore::Entry> TextureStore::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Entry> entries;
    entries.reserve(_textures.size());
    for (const auto& [name, image] : _textures)
    {
        entries.push_back({name, image ? image->width : 0, image ? image->height : 0});
    }
    return entries;
}

void TextureStore::LoadCallback(const char* textureName, projectm_texture_load_data* data, void* userData)
{
    auto* store = static_cast<TextureStore*>(userData);
    if (store != nullptr && textureName != nullptr && data != nullptr)
    {
        store->Serve(textureName, data);
    }
}

void TextureStore::Serve(const char* textureName, projectm_texture_load_data* data)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _textures.find(LowerKey(textureName));
    if (it == _textures.end() || !it->second)
    {
        return;
    }

    // Pin the image so its buffer stays alive after this call returns, until
    // projectM has copied it (and until the next served texture replaces it).
    _pinned = it->second;
    const auto& image = *_pinned;
    if (image.pixels.empty() || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    data->data = image.pixels.data();
    data->width = static_cast<unsigned int>(image.width);
    data->height = static_cast<unsigned int>(image.height);
    data->channels = static_cast<unsigned int>(image.channels);
    data->texture_id = 0;
}
