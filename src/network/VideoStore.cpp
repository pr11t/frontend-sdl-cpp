#include "network/VideoStore.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/TemporaryFile.h>
#include <Poco/UUIDGenerator.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

VideoStore::VideoStore()
    : _directory(Poco::TemporaryFile::tempName())
{
    Poco::File(_directory).createDirectories();
}

VideoStore::~VideoStore()
{
    try
    {
        Poco::File directory(_directory);
        if (directory.exists())
        {
            directory.remove(true);
        }
    }
    catch (...)
    {
        // Destructors must not throw. The OS can clean up an orphaned temp path.
    }
}

VideoStore::Entry VideoStore::Set(const std::string& name, const std::string& bytes,
                                  VideoLayout layout)
{
    const auto unique = Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    const auto path = Poco::Path(_directory).append(unique + "-" + name).toString();
    {
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Could not create temporary video storage.");
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            Poco::File(path).remove();
            throw std::runtime_error("Could not write the complete video upload.");
        }
    }

    Entry entry{name, path, bytes.size(), layout};
    std::lock_guard<std::mutex> lock(_mutex);
    const auto existing = std::find_if(
        _entries.begin(), _entries.end(),
        [&](const Entry& item) { return item.name == name; });
    if (existing == _entries.end())
    {
        _entries.push_back(entry);
    }
    else
    {
        // Keep the old temporary file until process shutdown. A queued render
        // command or active FFmpeg decoder may still have it open.
        *existing = entry;
    }
    return entry;
}

bool VideoStore::SetLayout(const std::string& name, VideoLayout layout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto existing = std::find_if(
        _entries.begin(), _entries.end(),
        [&](const Entry& item) { return item.name == name; });
    if (existing == _entries.end())
    {
        return false;
    }
    existing->layout = layout;
    return true;
}

bool VideoStore::Remove(const std::string& name)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto existing = std::find_if(
            _entries.begin(), _entries.end(),
            [&](const Entry& item) { return item.name == name; });
        if (existing == _entries.end())
        {
            return false;
        }
        path = existing->path;
        _entries.erase(existing);
    }
    try
    {
        Poco::File file(path);
        if (file.exists())
        {
            file.remove();
        }
    }
    catch (...)
    {
        // The store entry is removed even if cleanup of its temporary file fails.
    }
    return true;
}

std::size_t VideoStore::Clear()
{
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        entries.swap(_entries);
    }
    for (const auto& entry : entries)
    {
        try
        {
            Poco::File file(entry.path);
            if (file.exists())
            {
                file.remove();
            }
        }
        catch (...)
        {
        }
    }
    return entries.size();
}

bool VideoStore::Find(const std::string& name, Entry& entry) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto existing = std::find_if(
        _entries.begin(), _entries.end(),
        [&](const Entry& item) { return item.name == name; });
    if (existing == _entries.end())
    {
        return false;
    }
    entry = *existing;
    return true;
}

std::vector<VideoStore::Entry> VideoStore::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries;
}

void VideoStore::SetLoading(const std::string& name, VideoLayout layout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _playback = {"loading", name, "", 0, 0, layout};
}

void VideoStore::SetPlaying(const std::string& name, int width, int height,
                            VideoLayout layout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _playback = {"playing", name, "", width, height, layout};
}

void VideoStore::UpdatePlaybackLayout(VideoLayout layout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _playback.layout = layout;
}

void VideoStore::SetError(const std::string& name, const std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _playback = {"error", name, error, 0, 0, {}};
}

void VideoStore::SetDisabled()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _playback = {};
}

VideoStore::Playback VideoStore::GetPlayback() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _playback;
}
