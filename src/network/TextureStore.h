#pragma once

#include "DecodedImage.h"

#include <projectM-4/callbacks.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Thread-safe store of named, in-memory textures served to projectM.
 *
 * Presets reference a texture by name (e.g. `image=cover`). When projectM needs
 * an uncached texture it invokes LoadCallback, which serves a matching image
 * from this store, or leaves it unset so projectM falls back to the filesystem.
 *
 * Uploads/removals happen on the network thread; the callback runs on the
 * render thread. All access is guarded by a mutex. Names are matched
 * case-insensitively (projectM lower-cases texture names internally).
 */
class TextureStore
{
public:
    struct Entry
    {
        std::string name;
        int width{0};
        int height{0};
    };

    /// Adds or replaces the texture under @p name. Returns the stored dimensions.
    void Set(const std::string& name, DecodedImagePtr image);

    /// Removes the texture under @p name. Returns true if one existed.
    bool Remove(const std::string& name);

    /// Removes all textures. Returns the number removed.
    std::size_t Clear();

    /// Lists the currently stored textures (name and dimensions).
    std::vector<Entry> List() const;

    /**
     * @brief projectM texture-load callback. Register with
     * projectm_set_texture_load_event_callback, passing a TextureStore* as
     * user_data. Runs on the render thread.
     */
    static void LoadCallback(const char* textureName, projectm_texture_load_data* data, void* userData);

private:
    void Serve(const char* textureName, projectm_texture_load_data* data);

    mutable std::mutex _mutex;
    std::map<std::string, DecodedImagePtr> _textures; //!< Keyed by lower-cased name.
    DecodedImagePtr _pinned; //!< Keeps the last-served image alive until projectM has consumed it.
};
