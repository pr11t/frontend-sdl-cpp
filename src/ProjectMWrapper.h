#pragma once

#include "Deck.h"
#include "notifications/PlaybackControlNotification.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <Poco/Logger.h>
#include <Poco/NObserver.h>

#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Owns one or more projectM "decks" (independent instances). Deck 0 is
 * the primary visualizer; the existing single-instance public surface delegates
 * to it, so existing callers are unaffected.
 */
class ProjectMWrapper : public Poco::Util::Subsystem
{
public:
    const char* name() const override;

    void initialize(Poco::Util::Application& app) override;

    void uninitialize() override;

    /// Number of decks created at startup.
    std::size_t DeckCount() const;

    /// Access a deck by index (0-based). Callers must ensure index < DeckCount().
    Deck& DeckAt(std::size_t index);

    /**
     * Returns deck 0's projectM instance handle.
     */
    projectm_handle ProjectM() const;

    /**
     * Returns deck 0's playlist handle.
     */
    projectm_playlist_handle Playlist() const;

    /**
     * Renders deck 0 directly to the screen.
     */
    void RenderFrame() const;

    /**
     * Renders deck 0 into a caller-owned framebuffer (used by the post-processor).
     */
    void RenderFrameToFramebuffer(std::uint32_t framebuffer) const;

    int TargetFPS();

    void UpdateRealFPS(float fps);

    /// Shows the initial preset on every deck (if the splash is disabled).
    void DisplayInitialPreset();

    void ChangeBeatSensitivity(float value);

    std::string ProjectMBuildVersion();

    std::string ProjectMRuntimeVersion();

    void PresetFileNameToClipboard() const;

    // Deck-0 convenience overloads (backward compatible).
    bool LoadPresetFile(const std::string& filename, bool smoothTransition, std::string& error);
    bool LoadPresetSource(const std::string& source, bool smoothTransition, std::string& error);
    bool ReloadCurrentPreset(bool smoothTransition, std::string& error);
    const std::string& CurrentPresetFile() const;

    // Deck-indexed operations.
    bool LoadPresetFile(std::size_t deck, const std::string& filename, bool smoothTransition, std::string& error);
    bool LoadPresetSource(std::size_t deck, const std::string& source, bool smoothTransition, std::string& error);
    bool ReloadCurrentPreset(std::size_t deck, bool smoothTransition, std::string& error);
    std::string CurrentPresetFile(std::size_t deck) const;
    void NextPreset(std::size_t deck, bool smoothTransition);
    void PreviousPreset(std::size_t deck, bool smoothTransition);
    void RandomPreset(std::size_t deck, bool smoothTransition);

    /**
     * @brief Writes a runtime configuration override and applies it live to all decks.
     * Must be called on the render thread.
     */
    void SetRuntimeConfig(const std::string& key, const std::string& value);

    /**
     * @brief Removes a runtime configuration override and re-applies the underlying value.
     * Must be called on the render thread.
     */
    void ClearRuntimeConfig(const std::string& key);

private:
    void PlaybackControlNotificationHandler(const Poco::AutoPtr<PlaybackControlNotification>& notification);

    std::vector<std::string> GetPathListWithDefault(const std::string& baseKey, const std::string& defaultPath);

    void OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property);
    void OnConfigurationPropertyRemoved(const std::string& key);

    Deck& MainDeck() const;

    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _userConfig; //!< The "user" configuration layer.
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _runtimeConfig; //!< The runtime override layer (HTTP config API).
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _projectMConfigView; //!< View of the "projectM" subkey in the effective config.

    std::vector<std::unique_ptr<Deck>> _decks; //!< Deck 0 is the primary; extras are composited.

    Poco::NObserver<ProjectMWrapper, PlaybackControlNotification> _playbackControlNotificationObserver{*this, &ProjectMWrapper::PlaybackControlNotificationHandler};

    Poco::Logger& _logger{Poco::Logger::get("ProjectMWrapper")};
};
