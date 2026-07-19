#pragma once

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <Poco/Logger.h>
#include <Poco/Util/AbstractConfiguration.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief One independent projectM instance ("deck") with its own playlist and
 * offscreen render target.
 *
 * Deck 0 is the primary visualizer (its output is the base of the compositor);
 * additional decks are rendered into their own framebuffers and exposed to the
 * post-processing shader chain as named textures ("deck1", "deck2", ...).
 */
class Deck
{
public:
    Deck(std::size_t index, Poco::AutoPtr<Poco::Util::AbstractConfiguration> configView);
    ~Deck();

    void Initialize(int width, int height,
                    const std::vector<std::string>& presetPaths,
                    const std::vector<std::string>& texturePaths);
    void Shutdown();

    std::size_t Index() const { return _index; }
    projectm_handle ProjectM() const { return _projectM; }
    projectm_playlist_handle Playlist() const { return _playlist; }

    /// Resizes the deck's render target and updates projectM's window size.
    void Resize(int width, int height);

    /// Renders one frame into the deck's own framebuffer; returns the color texture id.
    std::uint32_t RenderToTexture();

    /// Renders one frame into an already-bound caller-owned framebuffer (no bind/clear).
    void RenderToFramebuffer(std::uint32_t framebuffer);

    /// Renders one frame directly to the currently-bound framebuffer (the screen).
    void RenderToScreen();

    bool LoadPresetFile(const std::string& filename, bool smoothTransition, std::string& error);
    bool LoadPresetSource(const std::string& source, bool smoothTransition, std::string& error);
    bool ReloadCurrentPreset(bool smoothTransition, std::string& error);
    const std::string& CurrentPresetFile() const { return _currentPresetFile; }

    void NextPreset(bool smoothTransition);
    void PreviousPreset(bool smoothTransition);
    void RandomPreset(bool smoothTransition);

    void DisplayInitialPreset();

    /// Applies a single "projectM.*" config key to this deck.
    void ApplyConfigKey(const std::string& key);

    int TargetFPS() const;
    void UpdateRealFPS(float fps);

private:
    static void PresetSwitchedEvent(bool isHardCut, unsigned int index, void* context);
    static void CapturePresetLoadFailure(const char* filename, const char* message, void* context);
    bool CreateRenderTarget(int width, int height);
    void DestroyRenderTarget();

    std::size_t _index;
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _config; //!< "projectM" effective config view (shared, read-only).
    projectm_handle _projectM{nullptr};
    projectm_playlist_handle _playlist{nullptr};
    std::string _currentPresetFile;
    std::uint32_t _framebuffer{0};
    std::uint32_t _texture{0};
    int _width{0};
    int _height{0};
    int _meshX{48};              //!< Resolved per-pixel mesh width (per-deck override or global).
    int _meshY{32};              //!< Resolved per-pixel mesh height.
    bool _meshOverridden{false}; //!< True when this deck sets its own mesh, so global changes don't clobber it.
    Poco::Logger& _logger{Poco::Logger::get("Deck")};
};
