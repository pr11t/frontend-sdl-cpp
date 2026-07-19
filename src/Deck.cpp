#include "Deck.h"

#include "notifications/UpdateWindowTitleNotification.h"

#include <Poco/File.h>
#include <Poco/NotificationCenter.h>

#ifdef USE_GLES
#include <SDL2/SDL_opengles2.h>
#elif defined(USE_GLEW)
#include <GL/glew.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#endif

#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

// projectM treats a preset duration of 0 as "advance almost immediately". Map any
// non-positive configured duration to an effectively infinite one so that
// displayDuration = 0 means "never auto-advance".
double EffectivePresetDuration(double configured)
{
    return configured > 0.0 ? configured : 1.0e9;
}

struct PresetLoadFailure
{
    bool failed{false};
    std::string message;
};

} // namespace

Deck::Deck(std::size_t index, Poco::AutoPtr<Poco::Util::AbstractConfiguration> configView)
    : _index(index)
    , _config(std::move(configView))
{
}

Deck::~Deck()
{
    Shutdown();
}

void Deck::Initialize(int width, int height,
                      const std::vector<std::string>& presetPaths,
                      const std::vector<std::string>& texturePaths)
{
    _projectM = projectm_create();
    if (!_projectM)
    {
        poco_error(_logger, "Failed to initialize projectM. Possible reasons are a lack of required OpenGL features or GPU resources.");
        throw std::runtime_error("projectM initialization failed");
    }

    int fps = _config->getInt("fps", 60);
    if (fps <= 0)
    {
        fps = 60;
    }

    // Resolve the per-pixel mesh once. A deck may override the global mesh with
    // "projectM.deck<index>.meshX/Y" -- useful to give a non-warping preset (e.g.
    // a spectrum analyzer) a tiny mesh while another deck keeps a full one.
    const int globalMeshX = _config->getInt("meshX", 48);
    const int globalMeshY = _config->getInt("meshY", 32);
    const std::string deckPrefix = "deck" + std::to_string(_index) + ".";
    _meshX = _config->getInt(deckPrefix + "meshX", globalMeshX);
    _meshY = _config->getInt(deckPrefix + "meshY", globalMeshY);
    _meshOverridden = _config->has(deckPrefix + "meshX") || _config->has(deckPrefix + "meshY");

    projectm_set_window_size(_projectM, width, height);
    projectm_set_fps(_projectM, fps);
    projectm_set_mesh_size(_projectM, _meshX, _meshY);
    projectm_set_aspect_correction(_projectM, _config->getBool("aspectCorrectionEnabled", true));
    projectm_set_preset_locked(_projectM, _config->getBool("presetLocked", false));

    projectm_set_preset_duration(_projectM, EffectivePresetDuration(_config->getDouble("displayDuration", 30.0)));
    projectm_set_soft_cut_duration(_projectM, _config->getDouble("transitionDuration", 3.0));
    projectm_set_hard_cut_enabled(_projectM, _config->getBool("hardCutsEnabled", false));
    projectm_set_hard_cut_duration(_projectM, _config->getDouble("hardCutDuration", 20.0));
    projectm_set_hard_cut_sensitivity(_projectM, static_cast<float>(_config->getDouble("hardCutSensitivity", 1.0)));
    projectm_set_beat_sensitivity(_projectM, static_cast<float>(_config->getDouble("beatSensitivity", 1.0)));

    if (!texturePaths.empty())
    {
        std::vector<const char*> texturePathList;
        texturePathList.reserve(texturePaths.size());
        for (const auto& texturePath : texturePaths)
        {
            texturePathList.push_back(texturePath.data());
        }
        projectm_set_texture_search_paths(_projectM, texturePathList.data(), texturePaths.size());
    }

    _playlist = projectm_playlist_create(_projectM);
    if (!_playlist)
    {
        poco_error(_logger, "Failed to create the projectM preset playlist manager instance.");
        throw std::runtime_error("Playlist initialization failed");
    }

    projectm_playlist_set_shuffle(_playlist, _config->getBool("shuffleEnabled", true));

    for (const auto& presetPath : presetPaths)
    {
        Poco::File file(presetPath);
        if (file.exists() && file.isFile())
        {
            projectm_playlist_add_preset(_playlist, presetPath.c_str(), false);
        }
        else
        {
            projectm_playlist_add_path(_playlist, presetPath.c_str(), true, false);
        }
    }
    projectm_playlist_sort(_playlist, 0, projectm_playlist_size(_playlist), SORT_PREDICATE_FILENAME_ONLY, SORT_ORDER_ASCENDING);

    projectm_playlist_set_preset_switched_event_callback(_playlist, &Deck::PresetSwitchedEvent, static_cast<void*>(this));

    if (!CreateRenderTarget(width, height))
    {
        throw std::runtime_error("Failed to create deck render target.");
    }
}

void Deck::Shutdown()
{
    DestroyRenderTarget();
    if (_playlist)
    {
        projectm_playlist_destroy(_playlist);
        _playlist = nullptr;
    }
    if (_projectM)
    {
        projectm_destroy(_projectM);
        _projectM = nullptr;
    }
}

void Deck::Resize(int width, int height)
{
    if (width == _width && height == _height)
    {
        return;
    }
    if (_projectM)
    {
        projectm_set_window_size(_projectM, width, height);
    }
    DestroyRenderTarget();
    CreateRenderTarget(width, height);
}

std::uint32_t Deck::RenderToTexture()
{
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    projectm_opengl_render_frame_fbo(_projectM, _framebuffer);
    return _texture;
}

void Deck::RenderToFramebuffer(std::uint32_t framebuffer)
{
    // The caller has already bound/cleared the target framebuffer and set the
    // viewport (see VisualPostProcessor::Render). projectm_opengl_render_frame_fbo
    // is the only supported way to composite into a caller-owned FBO.
    projectm_opengl_render_frame_fbo(_projectM, framebuffer);
}

void Deck::RenderToScreen()
{
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    size_t currentMeshX{0};
    size_t currentMeshY{0};
    projectm_get_mesh_size(_projectM, &currentMeshX, &currentMeshY);
    if (currentMeshX != static_cast<size_t>(_meshX) ||
        currentMeshY != static_cast<size_t>(_meshY))
    {
        projectm_set_mesh_size(_projectM, _meshX, _meshY);
    }

    projectm_opengl_render_frame(_projectM);
}

bool Deck::LoadPresetFile(const std::string& filename, bool smoothTransition, std::string& error)
{
    PresetLoadFailure failure;
    projectm_set_preset_switch_failed_event_callback(_projectM, &Deck::CapturePresetLoadFailure, &failure);
    projectm_load_preset_file(_projectM, filename.c_str(), smoothTransition);
    projectm_playlist_connect(_playlist, _projectM);
    if (failure.failed)
    {
        error = failure.message;
        return false;
    }
    _currentPresetFile = filename;
    if (_index == 0)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
    }
    return true;
}

bool Deck::LoadPresetSource(const std::string& source, bool smoothTransition, std::string& error)
{
    PresetLoadFailure failure;
    projectm_set_preset_switch_failed_event_callback(_projectM, &Deck::CapturePresetLoadFailure, &failure);
    projectm_load_preset_data(_projectM, source.c_str(), smoothTransition);
    projectm_playlist_connect(_playlist, _projectM);
    if (failure.failed)
    {
        error = failure.message;
        return false;
    }
    _currentPresetFile.clear();
    if (_index == 0)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
    }
    return true;
}

bool Deck::ReloadCurrentPreset(bool smoothTransition, std::string& error)
{
    if (_currentPresetFile.empty())
    {
        error = "The current preset was loaded from memory and has no file to reload.";
        return false;
    }
    return LoadPresetFile(_currentPresetFile, smoothTransition, error);
}

void Deck::NextPreset(bool smoothTransition)
{
    const bool shuffleEnabled = projectm_playlist_get_shuffle(_playlist);
    projectm_playlist_set_shuffle(_playlist, false);
    projectm_playlist_play_next(_playlist, !smoothTransition);
    projectm_playlist_set_shuffle(_playlist, shuffleEnabled);
}

void Deck::PreviousPreset(bool smoothTransition)
{
    const bool shuffleEnabled = projectm_playlist_get_shuffle(_playlist);
    projectm_playlist_set_shuffle(_playlist, false);
    projectm_playlist_play_previous(_playlist, !smoothTransition);
    projectm_playlist_set_shuffle(_playlist, shuffleEnabled);
}

void Deck::RandomPreset(bool smoothTransition)
{
    const bool shuffleEnabled = projectm_playlist_get_shuffle(_playlist);
    projectm_playlist_set_shuffle(_playlist, true);
    projectm_playlist_play_next(_playlist, !smoothTransition);
    projectm_playlist_set_shuffle(_playlist, shuffleEnabled);
}

void Deck::DisplayInitialPreset()
{
    if (!_config->getBool("enableSplash", true))
    {
        if (_config->getBool("shuffleEnabled", true))
        {
            projectm_playlist_play_next(_playlist, true);
        }
        else
        {
            projectm_playlist_set_position(_playlist, 0, true);
        }
    }
}

int Deck::TargetFPS() const
{
    return _config->getInt("fps", 60);
}

void Deck::UpdateRealFPS(float fps)
{
    projectm_set_fps(_projectM, static_cast<uint32_t>(std::round(fps)));
}

void Deck::ApplyConfigKey(const std::string& key)
{
    if (_projectM == nullptr || _playlist == nullptr)
    {
        return;
    }

    if (key == "projectM.presetLocked")
    {
        projectm_set_preset_locked(_projectM, _config->getBool("presetLocked", false));
        if (_index == 0)
        {
            Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
        }
    }
    if (key == "projectM.shuffleEnabled")
    {
        projectm_playlist_set_shuffle(_playlist, _config->getBool("shuffleEnabled", true));
    }
    if (key == "projectM.aspectCorrectionEnabled")
    {
        projectm_set_aspect_correction(_projectM, _config->getBool("aspectCorrectionEnabled", true));
    }
    if (key == "projectM.displayDuration")
    {
        projectm_set_preset_duration(_projectM, EffectivePresetDuration(_config->getDouble("displayDuration", 30.0)));
    }
    if (key == "projectM.transitionDuration")
    {
        projectm_set_soft_cut_duration(_projectM, _config->getDouble("transitionDuration", 3.0));
    }
    if (key == "projectM.hardCutsEnabled")
    {
        projectm_set_hard_cut_enabled(_projectM, _config->getBool("hardCutsEnabled", false));
    }
    if (key == "projectM.hardCutDuration")
    {
        projectm_set_hard_cut_duration(_projectM, _config->getDouble("hardCutDuration", 20.0));
    }
    if (key == "projectM.hardCutSensitivity")
    {
        projectm_set_hard_cut_sensitivity(_projectM, static_cast<float>(_config->getDouble("hardCutSensitivity", 1.0)));
    }
    if (key == "projectM.beatSensitivity")
    {
        projectm_set_beat_sensitivity(_projectM, static_cast<float>(_config->getDouble("beatSensitivity", 1.0)));
    }
    if (key == "projectM.fps")
    {
        int fps = _config->getInt("fps", 60);
        if (fps <= 0)
        {
            fps = 60;
        }
        projectm_set_fps(_projectM, fps);
    }
    if (key == "projectM.meshX" || key == "projectM.meshY")
    {
        // A deck with its own mesh override ignores global mesh changes.
        if (!_meshOverridden)
        {
            _meshX = _config->getInt("meshX", 48);
            _meshY = _config->getInt("meshY", 32);
            projectm_set_mesh_size(_projectM, _meshX, _meshY);
        }
    }
}

void Deck::PresetSwitchedEvent(bool /*isHardCut*/, unsigned int index, void* context)
{
    auto* deck = reinterpret_cast<Deck*>(context);
    auto* presetName = projectm_playlist_item(deck->_playlist, index);
    poco_information_f2(deck->_logger, "Deck %z displaying preset: %s",
                        deck->_index, std::string(presetName));
    deck->_currentPresetFile = presetName;
    projectm_playlist_free_string(presetName);

    if (deck->_index == 0)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
    }
}

void Deck::CapturePresetLoadFailure(const char*, const char* message, void* context)
{
    auto* failure = static_cast<PresetLoadFailure*>(context);
    failure->failed = true;
    failure->message = message ? message : "Preset loading failed.";
}

bool Deck::CreateRenderTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    glGenTextures(1, reinterpret_cast<GLuint*>(&_texture));
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer));
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!complete)
    {
        DestroyRenderTarget();
        return false;
    }
    _width = width;
    _height = height;
    return true;
}

void Deck::DestroyRenderTarget()
{
    if (_framebuffer != 0)
    {
        glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer));
        _framebuffer = 0;
    }
    if (_texture != 0)
    {
        glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture));
        _texture = 0;
    }
    _width = 0;
    _height = 0;
}
