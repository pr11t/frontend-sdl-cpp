#pragma once

#include "network/ControlCommandQueue.h"
#include "network/JobRegistry.h"
#include "network/PresetRepository.h"

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>

class ApiRequestHandler : public Poco::Net::HTTPRequestHandler
{
public:
    ApiRequestHandler(ControlCommandQueue& commands, JobRegistry& jobs,
                      PresetRepository& presets);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
};

class ApiRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    ApiRequestHandlerFactory(ControlCommandQueue& commands, JobRegistry& jobs,
                             PresetRepository& presets);

    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;

private:
    ControlCommandQueue& _commands;
    JobRegistry& _jobs;
    PresetRepository& _presets;
};
