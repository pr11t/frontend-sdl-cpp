#pragma once

#include "network/ControlCommandQueue.h"
#include "network/HttpApiServer.h"
#include "network/JobRegistry.h"
#include "network/PresetRepository.h"
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

protected:
    void initialize(Poco::Util::Application& app) override;
    void uninitialize() override;

private:
    ControlCommandQueue _commands;
    JobRegistry _jobs;
    VisualStateStore _visuals;
    std::unique_ptr<PresetRepository> _presets;
    std::unique_ptr<HttpApiServer> _server;
    Poco::Logger& _logger{Poco::Logger::get("NetworkControl")};
};
