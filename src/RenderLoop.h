#pragma once

#include "AudioCapture.h"
#include "ProjectMWrapper.h"
#include "SDLRenderingWindow.h"
#include "VideoDeck.h"
#include "VisualPostProcessor.h"

#include "notifications/QuitNotification.h"
#include "network/NetworkControlSubsystem.h"

#include <Poco/Logger.h>
#include <Poco/NObserver.h>
#include <Poco/Notification.h>

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

class ProjectMGUI;

class RenderLoop
{
public:
    RenderLoop();

    void Run();

protected:
    struct ModifierKeyStates {
        bool AnyPressed() const
        {
            return _shiftPressed || _ctrlPressed || _altPressed || _metaPressed;
        }

        bool _shiftPressed{false}; //!< L/R shift keys
        bool _ctrlPressed{false}; //!< L/R control keys
        bool _altPressed{false}; //!< L/R alt keys
        bool _metaPressed{false}; //!< Logo/meta/command key
    };

    /**
     * @brief Polls all SDL events in the queue and takes action if required.
     */
    void PollEvents();

    /**
     * @brief Checks if the GL viewport size has changed and if so, reconfigured projectM accordingly.
     */
    void CheckViewportSize();

    /**
     * @brief Executes remote-control commands on the render thread.
     */
    void DrainNetworkCommands();

    /// Starts or supersedes a background FFmpeg video-preparation request.
    void QueueVideoLoad(std::string name, std::string path);

    /// Activates a completed background video load without blocking the render loop.
    void PollVideoLoad();

    /// Prepares and collects a second decoder used for a seamless loop handoff.
    void QueueVideoLoopPreparation(std::string path);
    void StartVideoLoopPreparation(
        std::uint64_t generation, std::string path,
        std::unique_ptr<VideoDeck> retiredVideo = nullptr);
    void PollVideoLoopPreparation();
    void RestartVideoLoop(std::uint32_t nowMilliseconds);

    /// Adds/replaces the built-in video compositing pass while preserving user passes.
    void EnsureVideoCompositePass();

    /// Removes the built-in video compositing pass while preserving user passes.
    void RemoveVideoCompositePass();

    /**
     * @brief Handles SDL key press events.
     * @param event The key event.
     */
    void KeyEvent(const SDL_KeyboardEvent& event, bool down);

    /**
     * @brief Handles SDL mouse wheel events.
     * @param event The mouse wheel event
     */
    void ScrollEvent(const SDL_MouseWheelEvent& event);

    /**
     * @brief Handles SDL mouse button down events.
     * @param event The mouse button event
     */
    void MouseDownEvent(const SDL_MouseButtonEvent& event);

    /**
     * @brief Handles SDL mouse button up events.
     * @param event The mouse button event
     */
    void MouseUpEvent(const SDL_MouseButtonEvent& event);

    /**
     * @brief Handler for quit notifications.
     * @param notification The received notification.
     */
    void QuitNotificationHandler(const Poco::AutoPtr<QuitNotification>& notification);

    AudioCapture& _audioCapture;
    ProjectMWrapper& _projectMWrapper;
    SDLRenderingWindow& _sdlRenderingWindow;
    NetworkControlSubsystem& _networkControl;
    VisualPostProcessor _visualPostProcessor;
    std::unique_ptr<VideoDeck> _videoDeck; //!< POC: when set (--pocVideo), renders a video instead of projectM.

    struct VideoLoadRequest
    {
        std::uint64_t generation{0};
        std::string name;
        std::string path;
    };

    struct VideoLoadResult
    {
        std::uint64_t generation{0};
        std::string name;
        std::unique_ptr<VideoDeck> video;
        std::string error;
    };

    void StartVideoLoad(VideoLoadRequest request);

    std::future<VideoLoadResult> _videoLoad;
    std::optional<VideoLoadRequest> _queuedVideoLoad;
    std::uint64_t _videoLoadGeneration{0};

    struct VideoLoopResult
    {
        std::uint64_t generation{0};
        std::unique_ptr<VideoDeck> video;
        std::string error;
    };

    std::future<VideoLoopResult> _videoLoopLoad;
    std::optional<std::pair<std::uint64_t, std::string>> _queuedVideoLoopLoad;
    std::unique_ptr<VideoDeck> _videoLoopSuccessor;
    std::uint64_t _videoLoopGeneration{0};

    projectm_handle _projectMHandle{nullptr};
    projectm_playlist_handle _playlistHandle{nullptr};

    ProjectMGUI& _projectMGui;

    Poco::NObserver<RenderLoop, QuitNotification> _quitNotificationObserver{*this, &RenderLoop::QuitNotificationHandler}; //!< The observer for quit notifications.

    bool _wantsToQuit{false};

    bool _mouseDown{false}; //!< Left mouse button is pressed

    int _renderWidth{0};   //!< Window drawable (output) width.
    int _renderHeight{0};  //!< Window drawable (output) height.
    int _deckWidth{0};     //!< Internal size decks currently render at (scaled when post-processing).
    int _deckHeight{0};
    float _renderScale{1.0F}; //!< visual.renderScale: decks render at drawable*scale under post-processing.

    ModifierKeyStates _keyStates; //!< Current "pressed" states of modifier keys

    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _userConfig; //!< View of the "projectM" configuration subkey in the "user" configuration.

    Poco::Logger& _logger{Poco::Logger::get("RenderLoop")}; //!< The class logger.
};
