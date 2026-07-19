#include "ProjectMWrapper.h"

#include "ProjectMSDLApplication.h"
#include "SDLRenderingWindow.h"

#include "notifications/DisplayToastNotification.h"

#include <Poco/Delegate.h>
#include <Poco/File.h>
#include <Poco/NotificationCenter.h>

#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr std::size_t kMaxDecks = 4;
} // namespace

const char* ProjectMWrapper::name() const
{
    return "ProjectM Wrapper";
}

void ProjectMWrapper::initialize(Poco::Util::Application& app)
{
    auto& projectMSDLApp = dynamic_cast<ProjectMSDLApplication&>(app);
    _projectMConfigView = projectMSDLApp.config().createView("projectM");
    _userConfig = projectMSDLApp.UserConfiguration();
    _runtimeConfig = projectMSDLApp.RuntimeConfiguration();

    if (_decks.empty())
    {
        auto& sdlWindow = app.getSubsystem<SDLRenderingWindow>();
        int canvasWidth{0};
        int canvasHeight{0};
        sdlWindow.GetDrawableSize(canvasWidth, canvasHeight);

        auto presetPaths = GetPathListWithDefault("presetPath", app.config().getString("application.dir", ""));
        auto texturePaths = GetPathListWithDefault("texturePath", app.config().getString("", ""));

        auto deckCount = static_cast<std::size_t>(std::max(1, app.config().getInt("visual.decks", 1)));
        deckCount = std::min(deckCount, kMaxDecks);
        poco_information_f1(_logger, "Creating %z projectM deck(s).", deckCount);

        if (deckCount > 1 && !app.config().getBool("visual.postProcessingEnabled", false))
        {
            poco_warning(_logger,
                         "More than one deck was requested but post-processing is disabled; "
                         "only deck 0 is visible. Restart with --enableVisualPostProcessing and a "
                         "shader chain that samples deck1, deck2, ... to composite the extra decks.");
        }

        for (std::size_t index = 0; index < deckCount; ++index)
        {
            auto deck = std::make_unique<Deck>(index, _projectMConfigView);
            deck->Initialize(canvasWidth, canvasHeight, presetPaths, texturePaths);
            _decks.push_back(std::move(deck));
        }
    }

    Poco::NotificationCenter::defaultCenter().addObserver(_playbackControlNotificationObserver);

    // Observe user configuration changes (set via the settings window) and runtime
    // overrides (set via the HTTP config API). Both deliver full "projectM." keys.
    _userConfig->propertyChanged += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    _userConfig->propertyRemoved += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);
    _runtimeConfig->propertyChanged += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    _runtimeConfig->propertyRemoved += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);
}

void ProjectMWrapper::uninitialize()
{
    _runtimeConfig->propertyRemoved -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);
    _runtimeConfig->propertyChanged -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    _userConfig->propertyRemoved -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);
    _userConfig->propertyChanged -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    Poco::NotificationCenter::defaultCenter().removeObserver(_playbackControlNotificationObserver);

    _decks.clear();
}

std::size_t ProjectMWrapper::DeckCount() const
{
    return _decks.size();
}

Deck& ProjectMWrapper::DeckAt(std::size_t index)
{
    return *_decks[index];
}

Deck& ProjectMWrapper::MainDeck() const
{
    return *_decks[0];
}

projectm_handle ProjectMWrapper::ProjectM() const
{
    return MainDeck().ProjectM();
}

projectm_playlist_handle ProjectMWrapper::Playlist() const
{
    return MainDeck().Playlist();
}

int ProjectMWrapper::TargetFPS()
{
    return _projectMConfigView->getInt("fps", 60);
}

void ProjectMWrapper::UpdateRealFPS(float fps)
{
    for (auto& deck : _decks)
    {
        deck->UpdateRealFPS(fps);
    }
}

void ProjectMWrapper::RenderFrame() const
{
    MainDeck().RenderToScreen();
}

void ProjectMWrapper::RenderFrameToFramebuffer(std::uint32_t framebuffer) const
{
    MainDeck().RenderToFramebuffer(framebuffer);
}

void ProjectMWrapper::DisplayInitialPreset()
{
    for (auto& deck : _decks)
    {
        deck->DisplayInitialPreset();
    }
}

void ProjectMWrapper::ChangeBeatSensitivity(float value)
{
    auto projectM = MainDeck().ProjectM();
    projectm_set_beat_sensitivity(projectM, projectm_get_beat_sensitivity(projectM) + value);
    Poco::NotificationCenter::defaultCenter().postNotification(
        new DisplayToastNotification(Poco::format("Beat Sensitivity: %.2hf", projectm_get_beat_sensitivity(projectM))));
}

std::string ProjectMWrapper::ProjectMBuildVersion()
{
    return PROJECTM_VERSION_STRING;
}

std::string ProjectMWrapper::ProjectMRuntimeVersion()
{
    auto* projectMVersion = projectm_get_version_string();
    std::string projectMRuntimeVersion(projectMVersion);
    projectm_free_string(projectMVersion);
    return projectMRuntimeVersion;
}

void ProjectMWrapper::PresetFileNameToClipboard() const
{
    SDL_SetClipboardText(MainDeck().CurrentPresetFile().c_str());
}

bool ProjectMWrapper::LoadPresetFile(const std::string& filename, bool smoothTransition, std::string& error)
{
    return MainDeck().LoadPresetFile(filename, smoothTransition, error);
}

bool ProjectMWrapper::LoadPresetSource(const std::string& source, bool smoothTransition, std::string& error)
{
    return MainDeck().LoadPresetSource(source, smoothTransition, error);
}

bool ProjectMWrapper::ReloadCurrentPreset(bool smoothTransition, std::string& error)
{
    return MainDeck().ReloadCurrentPreset(smoothTransition, error);
}

const std::string& ProjectMWrapper::CurrentPresetFile() const
{
    return MainDeck().CurrentPresetFile();
}

bool ProjectMWrapper::LoadPresetFile(std::size_t deck, const std::string& filename, bool smoothTransition, std::string& error)
{
    return DeckAt(std::min(deck, _decks.size() - 1)).LoadPresetFile(filename, smoothTransition, error);
}

bool ProjectMWrapper::LoadPresetSource(std::size_t deck, const std::string& source, bool smoothTransition, std::string& error)
{
    return DeckAt(std::min(deck, _decks.size() - 1)).LoadPresetSource(source, smoothTransition, error);
}

bool ProjectMWrapper::ReloadCurrentPreset(std::size_t deck, bool smoothTransition, std::string& error)
{
    return DeckAt(std::min(deck, _decks.size() - 1)).ReloadCurrentPreset(smoothTransition, error);
}

std::string ProjectMWrapper::CurrentPresetFile(std::size_t deck) const
{
    return _decks[std::min(deck, _decks.size() - 1)]->CurrentPresetFile();
}

void ProjectMWrapper::NextPreset(std::size_t deck, bool smoothTransition)
{
    DeckAt(std::min(deck, _decks.size() - 1)).NextPreset(smoothTransition);
}

void ProjectMWrapper::PreviousPreset(std::size_t deck, bool smoothTransition)
{
    DeckAt(std::min(deck, _decks.size() - 1)).PreviousPreset(smoothTransition);
}

void ProjectMWrapper::RandomPreset(std::size_t deck, bool smoothTransition)
{
    DeckAt(std::min(deck, _decks.size() - 1)).RandomPreset(smoothTransition);
}

void ProjectMWrapper::PlaybackControlNotificationHandler(const Poco::AutoPtr<PlaybackControlNotification>& notification)
{
    // Keyboard / GUI playback controls act on deck 0.
    auto& deck = MainDeck();
    const bool smooth = notification->SmoothTransition();

    switch (notification->ControlAction())
    {
        case PlaybackControlNotification::Action::NextPreset:
            deck.NextPreset(smooth);
            break;
        case PlaybackControlNotification::Action::PreviousPreset:
            deck.PreviousPreset(smooth);
            break;
        case PlaybackControlNotification::Action::LastPreset:
            projectm_playlist_play_last(deck.Playlist(), !smooth);
            break;
        case PlaybackControlNotification::Action::RandomPreset:
            deck.RandomPreset(smooth);
            break;
        case PlaybackControlNotification::Action::ToggleShuffle:
            _userConfig->setBool("projectM.shuffleEnabled", !projectm_playlist_get_shuffle(deck.Playlist()));
            break;
        case PlaybackControlNotification::Action::TogglePresetLocked:
            _userConfig->setBool("projectM.presetLocked", !projectm_get_preset_locked(deck.ProjectM()));
            break;
    }
}

std::vector<std::string> ProjectMWrapper::GetPathListWithDefault(const std::string& baseKey, const std::string& defaultPath)
{
    using Poco::Util::AbstractConfiguration;

    std::vector<std::string> pathList;
    auto defaultPresetPath = _projectMConfigView->getString(baseKey, defaultPath);
    if (!defaultPresetPath.empty())
    {
        pathList.push_back(defaultPresetPath);
    }
    AbstractConfiguration::Keys subKeys;
    _projectMConfigView->keys(baseKey, subKeys);
    for (const auto& key : subKeys)
    {
        auto path = _projectMConfigView->getString(baseKey + "." + key, "");
        if (!path.empty())
        {
            pathList.push_back(std::move(path));
        }
    }
    return pathList;
}

void ProjectMWrapper::OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property)
{
    OnConfigurationPropertyRemoved(property.key());
}

void ProjectMWrapper::OnConfigurationPropertyRemoved(const std::string& key)
{
    // Live config applies to all decks (v1: global config namespace).
    for (auto& deck : _decks)
    {
        deck->ApplyConfigKey(key);
    }
}

void ProjectMWrapper::SetRuntimeConfig(const std::string& key, const std::string& value)
{
    // Writing the highest-precedence runtime layer fires the change observer above,
    // which applies the matching projectM setter to every deck. Render thread only.
    _runtimeConfig->setString(key, value);
}

void ProjectMWrapper::ClearRuntimeConfig(const std::string& key)
{
    if (_runtimeConfig->hasProperty(key))
    {
        _runtimeConfig->remove(key);
    }
}
