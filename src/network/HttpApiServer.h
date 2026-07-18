#pragma once

#include "network/ControlCommandQueue.h"

#include <Poco/Net/HTTPServer.h>

#include <cstdint>
#include <memory>
#include <string>

class HttpApiServer
{
public:
    explicit HttpApiServer(ControlCommandQueue& commands);
    ~HttpApiServer();

    void Start(const std::string& bindAddress, std::uint16_t port);
    void Stop();

    bool Running() const;
    std::uint16_t Port() const;

private:
    ControlCommandQueue& _commands;
    std::unique_ptr<Poco::Net::HTTPServer> _server;
    std::uint16_t _port{0};
};
