#pragma once

#include "network/ControlCommandQueue.h"
#include "network/JobRegistry.h"
#include "network/PresetRepository.h"
#include "network/VisualState.h"

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>

class ApiRequestHandler : public Poco::Net::HTTPRequestHandler
{
public:
    ApiRequestHandler(ControlCommandQueue& commands, JobRegistry& jobs,
                      PresetRepository& presets, VisualStateStore& visuals);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
    VisualStateStore& _visuals;
};

class ApiRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    ApiRequestHandlerFactory(ControlCommandQueue& commands, JobRegistry& jobs,
                             PresetRepository& presets, VisualStateStore& visuals);

    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
    VisualStateStore& _visuals;
};
