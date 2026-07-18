#include "network/HttpApiServer.h"

#include "network/ApiRequestHandler.h"

#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Timespan.h>

HttpApiServer::HttpApiServer(ControlCommandQueue& commands, JobRegistry& jobs,
                             PresetRepository& presets, VisualStateStore& visuals,
                             PlaybackStateStore& playback, TextureStore& textures,
                             ConfigLayers configLayers)
    : _commands(commands)
    , _jobs(jobs)
    , _presets(presets)
    , _visuals(visuals)
    , _playback(playback)
    , _textures(textures)
    , _configLayers(std::move(configLayers))
{
}

HttpApiServer::~HttpApiServer()
{
    Stop();
}

void HttpApiServer::Start(const std::string& bindAddress, std::uint16_t port)
{
    if (_server)
    {
        return;
    }

    Poco::Net::ServerSocket socket(Poco::Net::SocketAddress(bindAddress, port));
    _port = socket.address().port();

    Poco::Net::HTTPServerParams::Ptr parameters = new Poco::Net::HTTPServerParams;
    parameters->setSoftwareVersion("projectMSDL/1");
    parameters->setMaxThreads(4);
    parameters->setMaxQueued(16);
    parameters->setKeepAlive(true);
    parameters->setMaxKeepAliveRequests(32);
    parameters->setTimeout(Poco::Timespan(10, 0));

    _server = std::make_unique<Poco::Net::HTTPServer>(
        new ApiRequestHandlerFactory(_commands, _jobs, _presets, _visuals,
                                     _playback, _textures, _configLayers),
        socket, parameters);
    _server->start();
}

void HttpApiServer::Stop()
{
    if (!_server)
    {
        return;
    }

    _server->stopAll(true);
    _server.reset();
    _port = 0;
}

bool HttpApiServer::Running() const
{
    return _server != nullptr;
}

std::uint16_t HttpApiServer::Port() const
{
    return _port;
}
