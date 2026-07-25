#pragma once

#include "network/ControlCommandQueue.h"
#include "network/HttpApiServer.h"
#include "network/JobRegistry.h"
#include "network/PerformanceMetrics.h"
#include "network/PlaybackState.h"
#include "network/PresetRepository.h"
#include "network/ShaderChainStore.h"
#include "network/TextureStore.h"
#include "network/VideoStore.h"
#include "network/VisualState.h"

#include <Poco/Logger.h>
#include <Poco/Util/Subsystem.h>

#include <memory>

class NetworkControlSubsystem : public Poco::Util::Subsystem
{
public:
    NetworkControlSubsystem();

    const char* name() const override;

    ControlCommandQueue& Commands();
    JobRegistry& Jobs();
    VisualStateStore& Visuals();
    PlaybackStateStore& Playback();
    TextureStore& Textures();
    VideoStore& Videos();
    ShaderChainStore& Shaders();
    PerformanceMetricsStore& Performance();

protected:
    void initialize(Poco::Util::Application& app) override;
    void uninitialize() override;

private:
    ControlCommandQueue _commands;
    JobRegistry _jobs;
    VisualStateStore _visuals;
    PlaybackStateStore _playback;
    TextureStore _textures;
    VideoStore _videos;
    ShaderChainStore _shaders;
    PerformanceMetricsStore _performance;
    std::unique_ptr<PresetRepository> _presets;
    std::unique_ptr<HttpApiServer> _server;
    Poco::Logger& _logger{Poco::Logger::get("NetworkControl")};
};
