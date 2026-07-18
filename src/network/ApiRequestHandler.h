#pragma once

#include "network/ControlCommandQueue.h"

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>

class ApiRequestHandler : public Poco::Net::HTTPRequestHandler
{
public:
    explicit ApiRequestHandler(ControlCommandQueue& commands);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    ControlCommandQueue& _commands;
};

class ApiRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    explicit ApiRequestHandlerFactory(ControlCommandQueue& commands);

    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;

private:
    ControlCommandQueue& _commands;
};
