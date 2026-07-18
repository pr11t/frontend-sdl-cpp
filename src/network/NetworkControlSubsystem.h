#pragma once

#include "network/ControlCommandQueue.h"
#include "network/HttpApiServer.h"

#include <Poco/Logger.h>
#include <Poco/Util/Subsystem.h>

class NetworkControlSubsystem : public Poco::Util::Subsystem
{
public:
    NetworkControlSubsystem();

    const char* name() const override;

    ControlCommandQueue& Commands();

protected:
    void initialize(Poco::Util::Application& app) override;
    void uninitialize() override;

private:
    ControlCommandQueue _commands;
    HttpApiServer _server;
    Poco::Logger& _logger{Poco::Logger::get("NetworkControl")};
};
