#pragma once

#include "network/ConfigLayers.h"
#include "network/ControlCommandQueue.h"
#include "network/JobRegistry.h"
#include "network/PlaybackState.h"
#include "network/PresetRepository.h"
#include "network/VisualState.h"

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>

class ApiRequestHandler : public Poco::Net::HTTPRequestHandler
{
public:
    ApiRequestHandler(ControlCommandQueue& commands, JobRegistry& jobs,
                      PresetRepository& presets, VisualStateStore& visuals,
                      PlaybackStateStore& playback, ConfigLayers configLayers);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
    VisualStateStore& _visuals;
    PlaybackStateStore& _playback;
    ConfigLayers _configLayers;
};

class ApiRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    ApiRequestHandlerFactory(ControlCommandQueue& commands, JobRegistry& jobs,
                             PresetRepository& presets, VisualStateStore& visuals,
                             PlaybackStateStore& playback, ConfigLayers configLayers);

    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
    VisualStateStore& _visuals;
    PlaybackStateStore& _playback;
    ConfigLayers _configLayers;
};
