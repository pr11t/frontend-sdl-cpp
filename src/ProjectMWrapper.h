#pragma once

#include "notifications/PlaybackControlNotification.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <Poco/Logger.h>
#include <Poco/NObserver.h>

#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>

#include <memory>
#include <cstdint>

class ProjectMWrapper : public Poco::Util::Subsystem
{
public:
    const char* name() const override;

    void initialize(Poco::Util::Application& app) override;

    void uninitialize() override;

    /**
     * Returns the projectM instance handle.
     * @return The projectM instance handle used to call API functions.
     */
    projectm_handle ProjectM() const;

    /**
     * Returns the playlist handle.
     * @return The plaslist handle.
     */
    projectm_playlist_handle Playlist() const;

    /**
     * Renders a single projectM frame.
     */
    void RenderFrame() const;
    void RenderFrameToFramebuffer(std::uint32_t framebuffer) const;

    /**
     * @brief Returns the targeted FPS value.
     * @return The user-configured target FPS. Can be 0, which means unlimited.
     */
    int TargetFPS();

    /**
     * @brief Updates projectM with the current, actual FPS value.
     * @param fps The current FPS value.
     */
    void UpdateRealFPS(float fps);

    /**
     * @brief If splash is disabled, shows the initial preset.
     * If shuffle is on, a random preset will be picked. Otherwise, the first playlist item is displayed.
     */
    void DisplayInitialPreset();

    /**
     * @brief Changes beat sensitivity by the given value.
     * @param value A positive or negative delta value.
     */
    void ChangeBeatSensitivity(float value);

    /**
     * @brief Returns the libprojectM version this application was built against.
     * @return A string with the libprojectM build version.
     */
    std::string ProjectMBuildVersion();

    /**
     * @brief Returns the libprojectM version this applications currently runs with.
     * @return A string with the libprojectM runtime library version.
     */
    std::string ProjectMRuntimeVersion();

    /**
     * Copies the full path of the current preset into the OS clipboard.
     */
    void PresetFileNameToClipboard() const;

    bool LoadPresetFile(const std::string& filename, bool smoothTransition, std::string& error);
    bool LoadPresetSource(const std::string& source, bool smoothTransition, std::string& error);
    bool ReloadCurrentPreset(bool smoothTransition, std::string& error);
    const std::string& CurrentPresetFile() const;

    /**
     * @brief Writes a runtime configuration override and applies it live.
     *
     * Must be called on the render thread (it ends up calling projectM setters).
     * The value is stored in the highest-precedence runtime override layer, so it
     * wins over command-line and user configuration, then the change observer maps
     * the key to the matching projectM setter.
     * @param key Full config key, e.g. "projectM.displayDuration".
     * @param value Canonical string value (e.g. "true", "30").
     */
    void SetRuntimeConfig(const std::string& key, const std::string& value);

    /**
     * @brief Removes a runtime configuration override and re-applies the value
     * from the next layer down (command-line, user or default).
     *
     * Must be called on the render thread (see SetRuntimeConfig).
     * @param key Full config key, e.g. "projectM.displayDuration".
     */
    void ClearRuntimeConfig(const std::string& key);

private:
    /**
     * @brief projectM callback. Called whenever a preset is switched.
     * @param isHardCut True if the switch was a hard cut.
     * @param index New preset playlist index.
     * @param context Callback context, e.g. "this" pointer.
     */
    static void PresetSwitchedEvent(bool isHardCut, unsigned int index, void* context);
    static void CapturePresetLoadFailure(const char* filename, const char* message, void* context);

    void PlaybackControlNotificationHandler(const Poco::AutoPtr<PlaybackControlNotification>& notification);

    std::vector<std::string> GetPathListWithDefault(const std::string& baseKey, const std::string& defaultPath);

    /**
     * @brief Event callback if a configuration value has changed.
     * @param property The key and value that has been changed.
     */
    void OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property);

    /**
     * @brief Event callback if a configuration value has been removed.
     * @param key The key of the removed property.
     */
    void OnConfigurationPropertyRemoved(const std::string& key);

    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _userConfig; //!< The "user" configuration layer (settings changed in the UI).
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _runtimeConfig; //!< The highest-precedence runtime override layer (HTTP config API).
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _projectMConfigView; //!< View of the "projectM" configuration subkey in the "effective" configuration.

    projectm_handle _projectM{nullptr}; //!< Pointer to the projectM instance used by the application.
    projectm_playlist_handle _playlist{nullptr}; //!< Pointer to the projectM playlist manager instance.
    std::string _currentPresetFile;

    Poco::NObserver<ProjectMWrapper, PlaybackControlNotification> _playbackControlNotificationObserver{*this, &ProjectMWrapper::PlaybackControlNotificationHandler};

    Poco::Logger& _logger{Poco::Logger::get("SDLRenderingWindow")}; //!< The class logger.
};
