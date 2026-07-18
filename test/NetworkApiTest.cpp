#include "network/ControlCommandQueue.h"
#include "network/HttpApiServer.h"

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct ApiResponse
{
    Poco::Net::HTTPResponse::HTTPStatus status;
    std::string reason;
    Poco::JSON::Object::Ptr body;
    std::string allow;
};

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

ApiResponse Request(std::uint16_t port,
                    const std::string& method,
                    const std::string& path,
                    const std::string& body = "")
{
    Poco::Net::HTTPClientSession session("127.0.0.1", port);
    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.setContentType("application/json");
    request.setContentLength(body.size());
    auto& output = session.sendRequest(request);
    output << body;

    Poco::Net::HTTPResponse response;
    auto& input = session.receiveResponse(response);
    std::ostringstream responseBody;
    Poco::StreamCopier::copyStream(input, responseBody);

    Poco::JSON::Parser parser;
    auto json = parser.parse(responseBody.str()).extract<Poco::JSON::Object::Ptr>();
    return {response.getStatus(), response.getReason(), json, response.get("Allow", "")};
}

void RunTests()
{
    ControlCommandQueue queue(2);
    HttpApiServer server(queue);
    server.Start("127.0.0.1", 0);
    Require(server.Running(), "Server should be running.");
    Require(server.Port() != 0, "Ephemeral server port should be assigned.");

    auto health = Request(server.Port(), "GET", "/api/v1/health");
    Require(health.status == Poco::Net::HTTPResponse::HTTP_OK, "Health should return 200.");
    Require(health.body->getValue<bool>("ok"), "Health response should be successful.");
    Require(health.body->getValue<int>("apiVersion") == 1, "Health API version should be 1.");

    auto next = Request(server.Port(), "POST", "/api/v1/playback/next", "{}");
    Require(next.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED, "Next should return 202.");
    Require(next.reason == "Accepted", "Next should use the standard Accepted reason.");
    ControlCommand nextCommand{};
    Require(queue.TryDequeue(nextCommand), "Next command should be queued.");
    Require(nextCommand.type == ControlCommandType::NextPreset, "Next command type should match.");
    Require(!nextCommand.smoothTransition, "Next should default to a hard transition.");

    auto previous = Request(server.Port(), "POST", "/api/v1/playback/previous",
                            R"({"transition":"smooth"})");
    Require(previous.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED, "Previous should return 202.");
    ControlCommand previousCommand{};
    Require(queue.TryDequeue(previousCommand), "Previous command should be queued.");
    Require(previousCommand.type == ControlCommandType::PreviousPreset,
            "Previous command type should match.");
    Require(previousCommand.smoothTransition, "Previous should request a smooth transition.");

    auto invalidJson = Request(server.Port(), "POST", "/api/v1/playback/next", "{");
    Require(invalidJson.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Invalid JSON should return 400.");

    auto invalidTransition = Request(server.Port(), "POST", "/api/v1/playback/next",
                                     R"({"transition":"instant"})");
    Require(invalidTransition.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Invalid transition should return 400.");

    auto unknownField = Request(server.Port(), "POST", "/api/v1/playback/next",
                                R"({"unexpected":true})");
    Require(unknownField.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Unknown fields should return 400.");

    auto oversized = Request(server.Port(), "POST", "/api/v1/playback/next",
                             std::string(4097, 'x'));
    Require(oversized.status == Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
            "Oversized request bodies should return 413.");

    auto wrongMethod = Request(server.Port(), "GET", "/api/v1/playback/next");
    Require(wrongMethod.status == Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED,
            "Wrong method should return 405.");
    Require(wrongMethod.allow == "POST", "Wrong method should include Allow header.");

    auto missing = Request(server.Port(), "GET", "/api/v1/missing");
    Require(missing.status == Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
            "Unknown endpoint should return 404.");

    Require(queue.TryEnqueue({ControlCommandType::NextPreset, false}), "First queue slot should work.");
    Require(queue.TryEnqueue({ControlCommandType::PreviousPreset, false}), "Second queue slot should work.");
    auto fullQueue = Request(server.Port(), "POST", "/api/v1/playback/next");
    Require(fullQueue.status == Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
            "Full queue should return 503.");

    server.Stop();
    Require(!server.Running(), "Server should stop.");
}

} // namespace

int main()
{
    try
    {
        RunTests();
        std::cout << "ProjectMSDL network API tests passed." << std::endl;
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "ProjectMSDL network API test failure: " << exception.what() << std::endl;
        return EXIT_FAILURE;
    }
}
