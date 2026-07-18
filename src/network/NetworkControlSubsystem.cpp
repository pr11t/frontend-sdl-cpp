#include "network/NetworkControlSubsystem.h"

#include <Poco/Exception.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Application.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

NetworkControlSubsystem::NetworkControlSubsystem()
    : _commands(256)
    , _jobs(256)
{
}

const char* NetworkControlSubsystem::name() const
{
    return "Network Control API";
}

ControlCommandQueue& NetworkControlSubsystem::Commands()
{
    return _commands;
}

JobRegistry& NetworkControlSubsystem::Jobs()
{
    return _jobs;
}

VisualStateStore& NetworkControlSubsystem::Visuals()
{
    return _visuals;
}

void NetworkControlSubsystem::initialize(Poco::Util::Application& app)
{
    _visuals.SetEnabled(app.config().getBool("visual.postProcessingEnabled", false));

    if (!app.config().getBool("network.enabled", true))
    {
        poco_information(_logger, "HTTP remote-control API is disabled.");
        return;
    }

    const auto bindAddress = app.config().getString("network.bindAddress", "0.0.0.0");
    const auto configuredPort = app.config().getInt("network.port", 8080);
    if (configuredPort < 1 || configuredPort > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::invalid_argument("network.port must be between 1 and 65535.");
    }

    try
    {
        std::vector<std::string> bundledRoots;
        const auto projectMConfig = app.config().createView("projectM");
        const auto defaultPresetPath = projectMConfig->getString("presetPath", "");
        if (!defaultPresetPath.empty())
        {
            bundledRoots.push_back(defaultPresetPath);
        }
        Poco::Util::AbstractConfiguration::Keys pathKeys;
        projectMConfig->keys("presetPath", pathKeys);
        for (const auto& key : pathKeys)
        {
            const auto path = projectMConfig->getString("presetPath." + key, "");
            if (!path.empty())
            {
                bundledRoots.push_back(path);
            }
        }

        const auto workspace = app.config().getString(
            "network.presetWorkspace",
            app.config().getString("system.configHomeDir") + "/projectM/presets");
        const auto maxPresetBytes = app.config().getUInt64("network.maxPresetBytes", 1048576);
        _presets = std::make_unique<PresetRepository>(
            workspace, bundledRoots, static_cast<std::size_t>(maxPresetBytes));
        _server = std::make_unique<HttpApiServer>(_commands, _jobs, *_presets, _visuals);
        _server->Start(bindAddress, static_cast<std::uint16_t>(configuredPort));
        poco_information_f2(_logger, "Unauthenticated HTTP remote-control API listening on %s:%?d.",
                            bindAddress, configuredPort);
    }
    catch (const Poco::Exception& exception)
    {
        poco_error_f1(_logger, "Failed to start HTTP remote-control API: %s", exception.displayText());
        throw;
    }
}

void NetworkControlSubsystem::uninitialize()
{
    if (_server)
    {
        _server->Stop();
        _server.reset();
    }
    _presets.reset();
}
