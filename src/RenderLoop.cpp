#include "RenderLoop.h"

#include "FPSLimiter.h"

#include "gui/ProjectMGUI.h"

#include <Poco/NotificationCenter.h>
#include <Poco/Path.h>

#include <Poco/Util/Application.h>

#include <SDL2/SDL.h>

#include "ProjectMSDLApplication.h"
#include "network/ShaderChainStore.h"
#include "network/TextureStore.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

RenderLoop::RenderLoop()
    : _audioCapture(Poco::Util::Application::instance().getSubsystem<AudioCapture>())
    , _projectMWrapper(Poco::Util::Application::instance().getSubsystem<ProjectMWrapper>())
    , _sdlRenderingWindow(Poco::Util::Application::instance().getSubsystem<SDLRenderingWindow>())
    , _networkControl(Poco::Util::Application::instance().getSubsystem<NetworkControlSubsystem>())
    , _projectMHandle(_projectMWrapper.ProjectM())
    , _playlistHandle(_projectMWrapper.Playlist())
    , _projectMGui(Poco::Util::Application::instance().getSubsystem<ProjectMGUI>())
    , _userConfig(ProjectMSDLApplication::instance().UserConfiguration())
{
    _renderScale = static_cast<float>(
        Poco::Util::Application::instance().config().getDouble("visual.renderScale", 1.0));
    _renderScale = std::min(1.0F, std::max(0.05F, _renderScale));

    // Serve named in-memory textures (e.g. album art) to presets, per deck. The
    // callback runs on this (render) thread, reading the network subsystem's store.
    for (std::size_t deck = 0; deck < _projectMWrapper.DeckCount(); ++deck)
    {
        projectm_set_texture_load_event_callback(_projectMWrapper.DeckAt(deck).ProjectM(),
                                                 &TextureStore::LoadCallback,
                                                 &_networkControl.Textures());
    }
}

void RenderLoop::Run()
{
    FPSLimiter limiter;

    auto& notificationCenter{Poco::NotificationCenter::defaultCenter()};

    notificationCenter.addObserver(_quitNotificationObserver);

    _projectMWrapper.DisplayInitialPreset();
    for (std::size_t deck = 0; deck < _projectMWrapper.DeckCount(); ++deck)
    {
        _networkControl.Playback().SetCurrentPresetFile(deck, _projectMWrapper.CurrentPresetFile(deck));
    }
    CheckViewportSize();
    if (_networkControl.Visuals().Get().enabled)
    {
        if (!_visualPostProcessor.Initialize(_renderWidth, _renderHeight, _renderScale))
        {
            _networkControl.Visuals().SetEnabled(false);
        }
        else
        {
            // Now that post-processing is active, resize the decks down to its
            // internal (scaled) resolution.
            CheckViewportSize();
        }
    }

    // POC: if a video path was supplied, decode it into a "video deck".
    const std::string pocVideoPath =
        Poco::Util::Application::instance().config().getString("poc.video", "");
    if (!pocVideoPath.empty())
    {
        try
        {
            _videoDeck = std::make_unique<VideoDeck>(pocVideoPath);
            _videoDeck->Initialize();
            _networkControl.Videos().SetPlaying(
                Poco::Path(pocVideoPath).getFileName(),
                _videoDeck->Width(), _videoDeck->Height());
            QueueVideoLoopPreparation(pocVideoPath);
        }
        catch (const std::exception& ex)
        {
            poco_error_f1(_logger, "POC video disabled: %s", std::string(ex.what()));
            _videoDeck.reset();
        }
    }

    // Deck 0 is the compositor base and the video decoder is exposed as the
    // named "video" texture. This needs only one projectM deck.
    if (_videoDeck && _visualPostProcessor.Active())
    {
        const std::string overlayPreset =
            Poco::Util::Application::instance().config().getString("poc.overlayPreset", "");
        if (!overlayPreset.empty())
        {
            std::string error;
            if (!_projectMWrapper.LoadPresetFile(overlayPreset, false, error))
            {
                poco_error_f1(_logger, "POC overlay preset failed to load: %s", error);
            }
            projectm_set_preset_locked(_projectMWrapper.DeckAt(0).ProjectM(), true);
        }

        EnsureVideoCompositePass();
    }

    while (!_wantsToQuit)
    {
        limiter.TargetFPS(_projectMWrapper.TargetFPS());
        limiter.StartFrame();

        DrainNetworkCommands();
        PollVideoLoad();
        PollVideoLoopPreparation();
        PollEvents();
        CheckViewportSize();
        _audioCapture.FillBuffer();
        if (_videoDeck)
        {
            const auto now = SDL_GetTicks();
            if (_videoDeck->Update(now) == VideoDeck::UpdateResult::EndOfStream)
            {
                RestartVideoLoop(now);
            }
        }
        if (_visualPostProcessor.Active())
        {
            // Render each extra projectM deck into its own texture; deck 0 is
            // rendered as the compositor base inside VisualPostProcessor::Render.
            std::map<std::string, std::uint32_t> deckTextures;
            for (std::size_t deck = 1; deck < _projectMWrapper.DeckCount(); ++deck)
            {
                deckTextures["deck" + std::to_string(deck)] =
                    _projectMWrapper.DeckAt(deck).RenderToTexture();
            }
            if (_videoDeck)
            {
                deckTextures["video"] = _videoDeck->Texture();
            }

            PostProcessInputs inputs;
            inputs.time = static_cast<float>(SDL_GetTicks()) / 1000.0F;
            _visualPostProcessor.Render(_projectMWrapper,
                                        _networkControl.Visuals().Get(),
                                        _networkControl.Shaders(),
                                        _networkControl.Textures(),
                                        inputs,
                                        deckTextures);
        }
        else if (_videoDeck)
        {
            _videoDeck->RenderToScreen(_renderWidth, _renderHeight); // no post-processing: video only
        }
        else
        {
            _projectMWrapper.RenderFrame();
        }
        for (std::size_t deck = 0; deck < _projectMWrapper.DeckCount(); ++deck)
        {
            _networkControl.Playback().SetCurrentPresetFile(deck, _projectMWrapper.CurrentPresetFile(deck));
        }
        _projectMGui.Draw();

        _sdlRenderingWindow.Swap();

        limiter.EndFrame();

        // Pass projectM the actual FPS value of the last frame.
        const auto measuredFps = limiter.FPS();
        _networkControl.Performance().Record(
            limiter.FrameTimeMilliseconds(),
            limiter.WorkTimeMilliseconds(),
            _projectMWrapper.TargetFPS(),
            measuredFps);
        _projectMGui.SetPerformanceMetrics(
            measuredFps, limiter.FrameTimeMilliseconds());
        _projectMWrapper.UpdateRealFPS(measuredFps);
    }

    notificationCenter.removeObserver(_quitNotificationObserver);
    _visualPostProcessor.Shutdown();

    for (std::size_t deck = 0; deck < _projectMWrapper.DeckCount(); ++deck)
    {
        projectm_playlist_set_preset_switched_event_callback(
            _projectMWrapper.DeckAt(deck).Playlist(), nullptr, nullptr);
    }
}

void RenderLoop::EnsureVideoCompositePass()
{
    constexpr auto shaderName = "pocVideoComposite";
    _networkControl.Shaders().SetShader(shaderName, std::string(
        "uniform float videoLevel;\n"
        "vec4 effect(vec2 uv){\n"
        "  vec3 base = texture(uInput, uv).rgb;\n"
        "  vec3 vid = texture(uTexture, vec2(uv.x, 1.0 - uv.y)).rgb;\n"
        "  vec3 o = vid * videoLevel + base;\n"
        "  return vec4(min(o, vec3(1.0)), 1.0);\n"
        "}\n"));

    auto chain = _networkControl.Shaders().GetSnapshot().chain;
    chain.erase(
        std::remove_if(chain.begin(), chain.end(),
                       [](const ShaderPassConfig& pass) {
                           return pass.shader == "pocVideoComposite";
                       }),
        chain.end());

    ShaderPassConfig videoPass;
    videoPass.shader = shaderName;
    videoPass.texture = "video";
    videoPass.params["videoLevel"] = 0.7F;
    chain.insert(chain.begin(), std::move(videoPass));
    _networkControl.Shaders().SetChain(std::move(chain));
}

void RenderLoop::RemoveVideoCompositePass()
{
    auto chain = _networkControl.Shaders().GetSnapshot().chain;
    const auto firstRemoved = std::remove_if(
        chain.begin(), chain.end(),
        [](const ShaderPassConfig& pass) {
            return pass.shader == "pocVideoComposite";
        });
    if (firstRemoved == chain.end())
    {
        return;
    }
    chain.erase(firstRemoved, chain.end());
    _networkControl.Shaders().SetChain(std::move(chain));
}

void RenderLoop::QueueVideoLoad(std::string name, std::string path)
{
    VideoLoadRequest request{
        ++_videoLoadGeneration,
        std::move(name),
        std::move(path)
    };

    if (_videoLoad.valid())
    {
        // Keep only the newest waiting request. The active FFmpeg open cannot
        // be cancelled safely, but its result will be discarded by generation.
        _queuedVideoLoad = std::move(request);
        return;
    }

    StartVideoLoad(std::move(request));
}

void RenderLoop::StartVideoLoad(VideoLoadRequest request)
{
    _videoLoad = std::async(
        std::launch::async,
        [request = std::move(request)]() mutable {
            VideoLoadResult result;
            result.generation = request.generation;
            result.name = std::move(request.name);
            try
            {
                result.video = std::make_unique<VideoDeck>(std::move(request.path));
                result.video->Prepare();
            }
            catch (const std::exception& error)
            {
                result.video.reset();
                result.error = error.what();
            }
            return result;
        });
}

void RenderLoop::PollVideoLoad()
{
    if (!_videoLoad.valid() ||
        _videoLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    auto result = _videoLoad.get();

    // If another request arrived while this one was preparing, never briefly
    // activate the stale video. Start only the latest queued request.
    if (_queuedVideoLoad)
    {
        auto next = std::move(*_queuedVideoLoad);
        _queuedVideoLoad.reset();
        StartVideoLoad(std::move(next));
    }

    if (result.generation != _videoLoadGeneration)
    {
        return;
    }

    try
    {
        if (!result.video)
        {
            throw std::runtime_error(result.error.empty()
                                         ? "Unknown video preparation error."
                                         : result.error);
        }

        // OpenGL remains on the render thread. FFmpeg opening, probing,
        // decoder/scaler allocation, and first-frame decoding are already done.
        result.video->InitializeGraphics();
        const int width = result.video->Width();
        const int height = result.video->Height();
        _videoDeck = std::move(result.video);
        if (_visualPostProcessor.Active())
        {
            EnsureVideoCompositePass();
        }
        _networkControl.Videos().SetPlaying(result.name, width, height);
        QueueVideoLoopPreparation(_videoDeck->Path());
        poco_information_f1(_logger, "Runtime video playing: %s", result.name);
    }
    catch (const std::exception& error)
    {
        _networkControl.Videos().SetError(result.name, error.what());
        poco_error_f2(_logger, "Runtime video failed (%s): %s",
                      result.name, std::string(error.what()));
    }
}

void RenderLoop::QueueVideoLoopPreparation(std::string path)
{
    const auto generation = ++_videoLoopGeneration;
    _videoLoopSuccessor.reset();

    if (_videoLoopLoad.valid())
    {
        _queuedVideoLoopLoad = std::make_pair(generation, std::move(path));
        return;
    }

    StartVideoLoopPreparation(generation, std::move(path));
}

void RenderLoop::StartVideoLoopPreparation(
    std::uint64_t generation, std::string path,
    std::unique_ptr<VideoDeck> retiredVideo)
{
    _videoLoopLoad = std::async(
        std::launch::async,
        [generation, path = std::move(path),
         retiredVideo = std::move(retiredVideo)]() mutable {
            VideoLoopResult result;
            result.generation = generation;
            try
            {
                // The previous FFmpeg decoder has no GL resources. Release it
                // here so codec teardown cannot delay the loop-boundary frame.
                retiredVideo.reset();
                result.video = std::make_unique<VideoDeck>(std::move(path));
                result.video->Prepare();
            }
            catch (const std::exception& error)
            {
                result.video.reset();
                result.error = error.what();
            }
            return result;
        });
}

void RenderLoop::PollVideoLoopPreparation()
{
    if (!_videoLoopLoad.valid() ||
        _videoLoopLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    auto result = _videoLoopLoad.get();
    if (_queuedVideoLoopLoad)
    {
        auto next = std::move(*_queuedVideoLoopLoad);
        _queuedVideoLoopLoad.reset();
        StartVideoLoopPreparation(next.first, std::move(next.second));
    }

    if (result.generation != _videoLoopGeneration)
    {
        return;
    }
    if (!result.video)
    {
        poco_error_f1(_logger, "Could not prepare seamless video loop: %s",
                      result.error);
        return;
    }

    _videoLoopSuccessor = std::move(result.video);
}

void RenderLoop::RestartVideoLoop(std::uint32_t nowMilliseconds)
{
    if (!_videoDeck || !_videoLoopSuccessor)
    {
        // Keep the final frame visible. Preparation is normally complete long
        // before EOF; if not, the handoff happens on a later render frame.
        return;
    }

    try
    {
        const auto path = _videoDeck->Path();
        auto retiredVideo = std::move(_videoLoopSuccessor);
        _videoDeck->RestartFromPrepared(*retiredVideo, nowMilliseconds);
        StartVideoLoopPreparation(
            ++_videoLoopGeneration, path, std::move(retiredVideo));
    }
    catch (const std::exception& error)
    {
        _videoLoopSuccessor.reset();
        poco_error_f1(_logger, "Seamless video loop handoff failed: %s",
                      std::string(error.what()));
    }
}

void RenderLoop::DrainNetworkCommands()
{
    constexpr std::size_t maxCommandsPerFrame = 32;

    for (std::size_t commandIndex = 0; commandIndex < maxCommandsPerFrame; ++commandIndex)
    {
        ControlCommand command{};
        if (!_networkControl.Commands().TryDequeue(command))
        {
            break;
        }

        // Clamp the requested deck to a valid index; unknown decks fall back to 0.
        std::size_t deck = command.deckIndex;
        if (deck >= _projectMWrapper.DeckCount())
        {
            deck = 0;
        }

        switch (command.type)
        {
            // Playback goes direct to the target deck (off the notification bus,
            // which drives only the keyboard/GUI deck-0 path).
            case ControlCommandType::NextPreset:
                _projectMWrapper.NextPreset(deck, command.smoothTransition);
                continue;

            case ControlCommandType::PreviousPreset:
                _projectMWrapper.PreviousPreset(deck, command.smoothTransition);
                continue;

            case ControlCommandType::RandomPreset:
                _projectMWrapper.RandomPreset(deck, command.smoothTransition);
                continue;

            case ControlCommandType::LoadPresetFile:
            case ControlCommandType::ReloadCurrentPreset:
            case ControlCommandType::LoadPresetSource: {
                _networkControl.Jobs().MarkRunning(command.jobId);
                std::string error;
                bool success = false;
                if (command.type == ControlCommandType::LoadPresetFile)
                {
                    success = _projectMWrapper.LoadPresetFile(
                        deck, command.payload, command.smoothTransition, error);
                }
                else if (command.type == ControlCommandType::ReloadCurrentPreset)
                {
                    success = _projectMWrapper.ReloadCurrentPreset(
                        deck, command.smoothTransition, error);
                }
                else
                {
                    success = _projectMWrapper.LoadPresetSource(
                        deck, command.payload, command.smoothTransition, error);
                }
                _networkControl.Jobs().Complete(command.jobId, success, error);
                continue;
            }

            case ControlCommandType::UpdateVisualState:
                _networkControl.Visuals().Apply(command.visualPatch);
                continue;

            case ControlCommandType::ResetVisualState:
                _networkControl.Visuals().Reset();
                continue;

            case ControlCommandType::SetConfig:
                _projectMWrapper.SetRuntimeConfig(command.configKey, command.configValue);
                continue;

            case ControlCommandType::ClearConfig:
                _projectMWrapper.ClearRuntimeConfig(command.configKey);
                continue;

            case ControlCommandType::ReloadTextures:
                // Drop cached textures on every deck so newly uploaded/removed
                // named textures are re-fetched via the texture-load callback.
                for (std::size_t d = 0; d < _projectMWrapper.DeckCount(); ++d)
                {
                    projectm_reset_textures(_projectMWrapper.DeckAt(d).ProjectM());
                }
                continue;

            case ControlCommandType::ShowToast:
                // Posted on the render thread so the GUI observer mutates the
                // toast state on the same thread that draws it.
                Poco::NotificationCenter::defaultCenter().postNotification(
                    new DisplayToastNotification(command.toast));
                continue;

            case ControlCommandType::LoadVideo:
                QueueVideoLoad(command.resourceName, command.payload);
                continue;

            case ControlCommandType::DisableVideo:
                ++_videoLoadGeneration;
                _queuedVideoLoad.reset();
                ++_videoLoopGeneration;
                _queuedVideoLoopLoad.reset();
                _videoLoopSuccessor.reset();
                _videoDeck.reset();
                if (_visualPostProcessor.Active())
                {
                    RemoveVideoCompositePass();
                }
                _networkControl.Videos().SetDisabled();
                poco_information(_logger, "Runtime video disabled.");
                continue;
        }
    }
}

void RenderLoop::PollEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        _projectMGui.ProcessInput(event);

        switch (event.type)
        {
            case SDL_MOUSEWHEEL:

                if (!_projectMGui.WantsMouseInput())
                {
                    ScrollEvent(event.wheel);
                }

                break;

            case SDL_KEYDOWN:
                if (!_projectMGui.WantsKeyboardInput())
                {
                    KeyEvent(event.key, true);
                }
                break;

            case SDL_KEYUP:
                if (!_projectMGui.WantsKeyboardInput())
                {
                    KeyEvent(event.key, false);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (!_projectMGui.WantsMouseInput())
                {
                    MouseDownEvent(event.button);
                }

                break;

            case SDL_MOUSEBUTTONUP:
                if (!_projectMGui.WantsMouseInput())
                {
                    MouseUpEvent(event.button);
                }

                break;

            case SDL_DROPFILE: {
                char* droppedFilePath = event.drop.file;

                // first we want to get the config settings that are relevant ehre
                // namely skipToDropped and droppedFolderOverride
                // we can get them from the projectMWrapper, in the _projectMConfigView available on it
                bool skipToDropped = _userConfig->getBool("projectM.skipToDropped", true);
                bool droppedFolderOverride = _userConfig->getBool("projectM.droppedFolderOverride", false);


                bool shuffle = projectm_playlist_get_shuffle(_playlistHandle);
                if (shuffle && skipToDropped)
                {
                    // if shuffle is enabled, we disable it temporarily, so the dropped preset is played next
                    // if skipToDropped is false, we also keep shuffle enabled, as it doesn't matter since the current preset is unaffected
                    projectm_playlist_set_shuffle(_playlistHandle, false);
                }

                int index = projectm_playlist_get_position(_playlistHandle) + 1;

                do
                {
                    Poco::File droppedFile(droppedFilePath);
                    if (!droppedFile.isDirectory())
                    {
                        // handle dropped preset file
                        Poco::Path droppedFileP(droppedFilePath);
                        if (!droppedFile.exists() || (droppedFileP.getExtension() != "milk" && droppedFileP.getExtension() != "prjm"))
                        {
                            std::string toastMessage = std::string("Invalid preset file: ") + droppedFilePath;
                            Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(toastMessage));
                            poco_information_f1(_logger, "%s", toastMessage);
                            break; // exit the block and go to the shuffle check
                        }

                        if (projectm_playlist_insert_preset(_playlistHandle, droppedFilePath, index, true))
                        {
                            if (skipToDropped)
                            {
                                projectm_playlist_play_next(_playlistHandle, true);
                            }
                            poco_information_f1(_logger, "Added preset: %s", std::string(droppedFilePath));
                            // no need to toast single presets, as its obvious if a preset was loaded.
                        }
                    }
                    else
                    {
                        // handle dropped directory

                        // if droppedFolderOverride is enabled, we clear the playlist first
                        // current edge case: if the dropped directory is invalid or contains no presets, then it still clears the playlist
                        if (droppedFolderOverride)
                        {
                            projectm_playlist_clear(_playlistHandle);
                            index = 0;
                        }

                        uint32_t addedFilesCount = projectm_playlist_insert_path(_playlistHandle, droppedFilePath, index, true, true);
                        if (addedFilesCount > 0)
                        {
                            std::string toastMessage = "Added " + std::to_string(addedFilesCount) + " presets from " + droppedFilePath;
                            poco_information_f1(_logger, "%s", toastMessage);
                            if (skipToDropped || droppedFolderOverride)
                            {
                                // if skip to dropped is true, or if a folder was dropped and it overrode the playlist, we skip to the next preset
                                projectm_playlist_play_next(_playlistHandle, true);
                            }
                            Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(toastMessage));
                        }
                        else
                        {
                            std::string toastMessage = std::string("No presets found in: ") + droppedFilePath;
                            Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(toastMessage));
                            poco_information_f1(_logger, "%s", toastMessage);
                        }
                    }
                } while (false);

                if (shuffle && skipToDropped)
                {
                    projectm_playlist_set_shuffle(_playlistHandle, true);
                }

                SDL_free(droppedFilePath);
                break;
            }


            case SDL_QUIT:
                _wantsToQuit = true;
                break;
        }
    }
}

void RenderLoop::CheckViewportSize()
{
    int renderWidth;
    int renderHeight;
    _sdlRenderingWindow.GetDrawableSize(renderWidth, renderHeight);

    // Output (window drawable) size changed: update the GUI and the post-processor,
    // which owns the final on-screen resolution.
    if (renderWidth != _renderWidth || renderHeight != _renderHeight)
    {
        _renderWidth = renderWidth;
        _renderHeight = renderHeight;

        _projectMGui.UpdateFontSize();

        if (_visualPostProcessor.Active() &&
            !_visualPostProcessor.Resize(renderWidth, renderHeight, _renderScale))
        {
            _networkControl.Visuals().SetEnabled(false);
        }

        poco_debug_f2(_logger, "Resized rendering canvas to %?dx%?d.", renderWidth, renderHeight);
    }

    // Decks render at the post-processor's internal (scaled) resolution when it is
    // active, otherwise straight at the drawable size. Matching the post-processor's
    // exact dimensions keeps deck textures aligned with the compositor buffers.
    int deckWidth = renderWidth;
    int deckHeight = renderHeight;
    if (_visualPostProcessor.Active())
    {
        deckWidth = _visualPostProcessor.RenderWidth();
        deckHeight = _visualPostProcessor.RenderHeight();
    }
    if (deckWidth != _deckWidth || deckHeight != _deckHeight)
    {
        for (std::size_t deck = 0; deck < _projectMWrapper.DeckCount(); ++deck)
        {
            _projectMWrapper.DeckAt(deck).Resize(deckWidth, deckHeight);
        }
        _deckWidth = deckWidth;
        _deckHeight = deckHeight;
    }
}

void RenderLoop::KeyEvent(const SDL_KeyboardEvent& event, bool down)
{
    auto keyModifier{static_cast<SDL_Keymod>(event.keysym.mod)};
    auto keyCode{event.keysym.sym};
    bool modifierPressed{false};

    if (keyModifier & KMOD_LGUI || keyModifier & KMOD_RGUI || keyModifier & KMOD_LCTRL)
    {
        modifierPressed = true;
    }

    // Handle modifier keys and save state for use in other methods, e.g. mouse events
    switch (keyCode)
    {
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            _keyStates._ctrlPressed = down;
            break;

        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            _keyStates._shiftPressed = down;
            break;

        case SDLK_LALT:
        case SDLK_RALT:
            _keyStates._altPressed = down;
            break;

        case SDLK_LGUI:
        case SDLK_RGUI:
            _keyStates._metaPressed = down;
            break;

        default:
            break;
    }

    if (!down)
    {
        return;
    }

    switch (keyCode)
    {
        case SDLK_ESCAPE:
            _projectMGui.Toggle();
            _sdlRenderingWindow.ShowCursor(_projectMGui.Visible());
            break;

        case SDLK_a: {
            bool aspectCorrectionEnabled = !projectm_get_aspect_correction(_projectMHandle);
            projectm_set_aspect_correction(_projectMHandle, aspectCorrectionEnabled);
        }
        break;

        case SDLK_c:
            if (modifierPressed)
            {
                _projectMWrapper.PresetFileNameToClipboard();
            }
            break;

#ifdef _DEBUG
        case SDLK_d:
            // Write next rendered frame to file
            projectm_write_debug_image_on_next_frame(_projectMHandle, nullptr);
            break;
#endif

        case SDLK_f:
            if (modifierPressed)
            {
                _sdlRenderingWindow.ToggleFullscreen();
            }
            break;

        case SDLK_i:
            if (modifierPressed)
            {
                _audioCapture.NextAudioDevice();
            }
            break;

        case SDLK_m:
            if (modifierPressed)
            {
                _sdlRenderingWindow.NextDisplay();
                break;
            }
            break;

        case SDLK_n:
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::NextPreset, _keyStates._shiftPressed));
            break;

        case SDLK_p:
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::PreviousPreset, _keyStates._shiftPressed));
            break;

        case SDLK_r: {
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::RandomPreset, _keyStates._shiftPressed));
            break;
        }

        case SDLK_q:
            if (modifierPressed)
            {
                _wantsToQuit = true;
            }
            break;

        case SDLK_y:
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::ToggleShuffle));
            break;

        case SDLK_BACKSPACE:
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::LastPreset, _keyStates._shiftPressed));
            break;

        case SDLK_SPACE:
            Poco::NotificationCenter::defaultCenter().postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::TogglePresetLocked));
            break;

        case SDLK_UP:
            // Increase beat sensitivity
            _projectMWrapper.ChangeBeatSensitivity(0.01f);
            break;

        case SDLK_DOWN:
            // Decrease beat sensitivity
            _projectMWrapper.ChangeBeatSensitivity(-0.01f);
            break;
    }
}

void RenderLoop::ScrollEvent(const SDL_MouseWheelEvent& event)
{
    // Wheel up is positive
    if (event.y > 0)
    {
        projectm_playlist_play_next(_playlistHandle, true);
    }
    // Wheel down is negative
    else if (event.y < 0)
    {
        projectm_playlist_play_previous(_playlistHandle, true);
    }
}

void RenderLoop::MouseDownEvent(const SDL_MouseButtonEvent& event)
{
    if (_projectMGui.WantsMouseInput())
    {
        return;
    }

    switch (event.button)
    {
        case SDL_BUTTON_LEFT:
            if (!_mouseDown && _keyStates._shiftPressed)
            {
                // ToDo: Improve this to differentiate between single click (add waveform) and drag (move waveform).
                int x;
                int y;
                int width;
                int height;

                _sdlRenderingWindow.GetDrawableSize(width, height);

                SDL_GetMouseState(&x, &y);

                // Scale those coordinates. libProjectM uses a scale of 0..1 instead of absolute pixel coordinates.
                float scaledX = (static_cast<float>(x) / static_cast<float>(width));
                float scaledY = (static_cast<float>(height - y) / static_cast<float>(height));

                // Add a new waveform.
                projectm_touch(_projectMHandle, scaledX, scaledY, 0, PROJECTM_TOUCH_TYPE_RANDOM);
                poco_debug_f2(_logger, "Added new random waveform at %?d,%?d", x, y);

                _mouseDown = true;
            }
            break;

        case SDL_BUTTON_RIGHT:
            if (!_keyStates.AnyPressed())
            {
                _sdlRenderingWindow.ToggleFullscreen();
            }
            break;

        case SDL_BUTTON_MIDDLE:
            projectm_touch_destroy_all(_projectMHandle);
            poco_debug(_logger, "Cleared all custom waveforms.");
            break;
    }
}

void RenderLoop::MouseUpEvent(const SDL_MouseButtonEvent& event)
{
    if (event.button == SDL_BUTTON_LEFT)
    {
        _mouseDown = false;
    }
}

void RenderLoop::QuitNotificationHandler(const Poco::AutoPtr<QuitNotification>& notification)
{
    _wantsToQuit = true;
}
