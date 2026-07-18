#include "network/ApiRequestHandler.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/URI.h>

#include <array>
#include <exception>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

namespace {

constexpr std::size_t maxRequestBodyBytes = 4096;

void WriteJson(Poco::Net::HTTPServerResponse& response,
               Poco::Net::HTTPResponse::HTTPStatus status,
               const Poco::JSON::Object& body)
{
    response.setStatusAndReason(status);
    response.setContentType("application/json; charset=utf-8");
    response.setChunkedTransferEncoding(false);

    std::ostringstream output;
    body.stringify(output);
    const auto json = output.str();
    response.setContentLength(json.size());
    response.send() << json;
}

void WriteError(Poco::Net::HTTPServerResponse& response,
                Poco::Net::HTTPResponse::HTTPStatus status,
                const std::string& code,
                const std::string& message)
{
    Poco::JSON::Object error;
    error.set("code", code);
    error.set("message", message);

    Poco::JSON::Object body;
    body.set("ok", false);
    body.set("error", error);
    WriteJson(response, status, body);
}

bool ReadRequestBody(Poco::Net::HTTPServerRequest& request,
                     std::string& body,
                     Poco::Net::HTTPServerResponse& response)
{
    if (request.hasContentLength() && request.getContentLength64() > maxRequestBodyBytes)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
                   "request_too_large", "Request body exceeds 4096 bytes.");
        return false;
    }

    std::array<char, 1024> buffer{};
    auto& input = request.stream();
    while (input.good())
    {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count <= 0)
        {
            break;
        }

        body.append(buffer.data(), static_cast<std::size_t>(count));
        if (body.size() > maxRequestBodyBytes)
        {
            WriteError(response, Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
                       "request_too_large", "Request body exceeds 4096 bytes.");
            return false;
        }
    }
    return true;
}

bool ParseTransition(const std::string& body,
                     bool& smoothTransition,
                     Poco::Net::HTTPServerResponse& response)
{
    smoothTransition = false;
    if (body.empty())
    {
        return true;
    }

    try
    {
        Poco::JSON::Parser parser;
        auto value = parser.parse(body);
        auto object = value.extract<Poco::JSON::Object::Ptr>();

        for (const auto& name : object->getNames())
        {
            if (name != "transition")
            {
                WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                           "invalid_request", "Unknown JSON field: " + name);
                return false;
            }
        }

        if (!object->has("transition"))
        {
            return true;
        }

        const auto transition = object->getValue<std::string>("transition");
        if (transition == "hard")
        {
            return true;
        }
        if (transition == "smooth")
        {
            smoothTransition = true;
            return true;
        }

        WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                   "invalid_request", "transition must be either hard or smooth.");
        return false;
    }
    catch (const Poco::Exception&)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                   "invalid_json", "Request body must be a JSON object.");
        return false;
    }
    catch (const std::exception&)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                   "invalid_json", "Request body must be a JSON object.");
        return false;
    }
}

void WriteMethodNotAllowed(Poco::Net::HTTPServerResponse& response, const std::string& allowedMethod)
{
    response.set("Allow", allowedMethod);
    WriteError(response, Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED,
               "method_not_allowed", "This endpoint requires " + allowedMethod + ".");
}

} // namespace

ApiRequestHandler::ApiRequestHandler(ControlCommandQueue& commands)
    : _commands(commands)
{
}

void ApiRequestHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                      Poco::Net::HTTPServerResponse& response)
{
    const auto path = Poco::URI(request.getURI()).getPath();
    const auto& method = request.getMethod();

    if (path == "/api/v1/health")
    {
        if (method != Poco::Net::HTTPRequest::HTTP_GET)
        {
            WriteMethodNotAllowed(response, Poco::Net::HTTPRequest::HTTP_GET);
            return;
        }

        Poco::JSON::Object body;
        body.set("ok", true);
        body.set("service", "projectMSDL");
        body.set("apiVersion", 1);
        WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, body);
        return;
    }

    ControlCommandType commandType;
    std::string commandName;
    if (path == "/api/v1/playback/next")
    {
        commandType = ControlCommandType::NextPreset;
        commandName = "next";
    }
    else if (path == "/api/v1/playback/previous")
    {
        commandType = ControlCommandType::PreviousPreset;
        commandName = "previous";
    }
    else
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
                   "not_found", "The requested API endpoint does not exist.");
        return;
    }

    if (method != Poco::Net::HTTPRequest::HTTP_POST)
    {
        WriteMethodNotAllowed(response, Poco::Net::HTTPRequest::HTTP_POST);
        return;
    }

    std::string requestBody;
    if (!ReadRequestBody(request, requestBody, response))
    {
        return;
    }

    bool smoothTransition = false;
    if (!ParseTransition(requestBody, smoothTransition, response))
    {
        return;
    }

    if (!_commands.TryEnqueue({commandType, smoothTransition}))
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                   "queue_full", "The remote-control command queue is full.");
        return;
    }

    Poco::JSON::Object body;
    body.set("ok", true);
    body.set("command", commandName);
    body.set("queued", true);
    WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, body);
}

ApiRequestHandlerFactory::ApiRequestHandlerFactory(ControlCommandQueue& commands)
    : _commands(commands)
{
}

Poco::Net::HTTPRequestHandler* ApiRequestHandlerFactory::createRequestHandler(
    const Poco::Net::HTTPServerRequest&)
{
    return new ApiRequestHandler(_commands);
}
