#pragma once

#include "network/ConfigLayers.h"
#include "network/ControlCommandQueue.h"
#include "network/JobRegistry.h"
#include "network/PlaybackState.h"
#include "network/PresetRepository.h"
#include "network/TextureStore.h"
#include "network/VisualState.h"

#include <Poco/Net/HTTPServer.h>

#include <cstdint>
#include <memory>
#include <string>

class HttpApiServer
{
public:
    HttpApiServer(ControlCommandQueue& commands, JobRegistry& jobs,
                  PresetRepository& presets, VisualStateStore& visuals,
                  PlaybackStateStore& playback, TextureStore& textures,
                  ConfigLayers configLayers);
    ~HttpApiServer();

    void Start(const std::string& bindAddress, std::uint16_t port);
    void Stop();

    bool Running() const;
    std::uint16_t Port() const;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
    VisualStateStore& _visuals;
    PlaybackStateStore& _playback;
    TextureStore& _textures;
    ConfigLayers _configLayers;
    std::unique_ptr<Poco::Net::HTTPServer> _server;
    std::uint16_t _port{0};
};
