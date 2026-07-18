#include "network/NetworkControlSubsystem.h"

#include <Poco/Exception.h>
#include <Poco/Util/Application.h>

#include <limits>
#include <stdexcept>

NetworkControlSubsystem::NetworkControlSubsystem()
    : _commands(256)
    , _server(_commands)
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

void NetworkControlSubsystem::initialize(Poco::Util::Application& app)
{
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
        _server.Start(bindAddress, static_cast<std::uint16_t>(configuredPort));
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
    _server.Stop();
}
