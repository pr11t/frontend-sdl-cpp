#pragma once

#include "network/VideoLayout.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Process-lifetime storage for uploaded videos and their playback state.
 *
 * Video bytes are written to a private temporary directory so FFmpeg can use its
 * normal seekable-file path. The directory and all uploads are removed when the
 * application exits.
 */
class VideoStore
{
public:
    struct Entry
    {
        std::string name;
        std::string path;
        std::size_t sizeBytes{0};
        VideoLayout layout;
    };

    struct Playback
    {
        std::string state{"disabled"}; // disabled, loading, playing, error
        std::string name;
        std::string error;
        int width{0};
        int height{0};
        VideoLayout layout;
    };

    VideoStore();
    ~VideoStore();

    VideoStore(const VideoStore&) = delete;
    VideoStore& operator=(const VideoStore&) = delete;

    Entry Set(const std::string& name, const std::string& bytes,
              VideoLayout layout = {});
    bool SetLayout(const std::string& name, VideoLayout layout);
    bool Remove(const std::string& name);
    std::size_t Clear();
    bool Find(const std::string& name, Entry& entry) const;
    std::vector<Entry> List() const;

    void SetLoading(const std::string& name, VideoLayout layout = {});
    void SetPlaying(const std::string& name, int width, int height,
                    VideoLayout layout = {});
    void UpdatePlaybackLayout(VideoLayout layout);
    void SetError(const std::string& name, const std::string& error);
    void SetDisabled();
    Playback GetPlayback() const;

private:
    std::string _directory;
    mutable std::mutex _mutex;
    std::vector<Entry> _entries;
    Playback _playback;
};
