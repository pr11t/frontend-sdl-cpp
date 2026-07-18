#include "network/ControlCommandQueue.h"
#include "network/HttpApiServer.h"
#include "network/JobRegistry.h"
#include "network/PresetRepository.h"
#include "network/VisualState.h"

#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Path.h>
#include <Poco/StreamCopier.h>
#include <Poco/TemporaryFile.h>
#include <Poco/UUIDGenerator.h>

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
    std::string etag;
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
                    const std::string& body = "",
                    const std::string& ifMatch = "")
{
    Poco::Net::HTTPClientSession session("127.0.0.1", port);
    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.setContentType("application/json");
    request.setContentLength(body.size());
    if (!ifMatch.empty())
    {
        request.set("If-Match", ifMatch);
    }
    auto& output = session.sendRequest(request);
    output << body;

    Poco::Net::HTTPResponse response;
    auto& input = session.receiveResponse(response);
    std::ostringstream responseBody;
    Poco::StreamCopier::copyStream(input, responseBody);

    Poco::JSON::Parser parser;
    auto json = parser.parse(responseBody.str()).extract<Poco::JSON::Object::Ptr>();
    return {response.getStatus(), response.getReason(), json,
            response.get("Allow", ""), response.get("ETag", "")};
}

void RunTests()
{
    ControlCommandQueue queue(2);
    JobRegistry jobs;
    const auto workspace = Poco::Path::temp() + "projectm-api-test-" +
                           Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    Poco::File(workspace).createDirectories();
    const auto bundled = Poco::Path::temp() + "projectm-api-bundled-" +
                         Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    Poco::File(bundled).createDirectories();
    const auto bundledPreset = Poco::Path(bundled).append("read-only.milk").toString();
    {
        Poco::FileOutputStream output(bundledPreset);
        output << "[preset00]\n";
    }
    PresetRepository presets(workspace, {bundled}, 1024 * 1024);
    VisualStateStore visuals;
    HttpApiServer server(queue, jobs, presets, visuals);
    server.Start("127.0.0.1", 0);
    Require(server.Running(), "Server should be running.");
    Require(server.Port() != 0, "Ephemeral server port should be assigned.");

    auto health = Request(server.Port(), "GET", "/api/v1/health");
    Require(health.status == Poco::Net::HTTPResponse::HTTP_OK, "Health should return 200.");
    Require(health.body->getValue<bool>("ok"), "Health response should be successful.");
    Require(health.body->getValue<int>("apiVersion") == 1, "Health API version should be 1.");

    auto visualDisabled = Request(server.Port(), "GET", "/api/v1/visual");
    Require(visualDisabled.status == Poco::Net::HTTPResponse::HTTP_OK,
            "Visual state should be readable when post-processing is disabled.");
    Require(!visualDisabled.body->getValue<bool>("enabled"),
            "Visual state should report post-processing as disabled.");

    auto disabledPatch = Request(server.Port(), "PATCH", "/api/v1/visual",
                                 R"({"mirrorX":true})");
    Require(disabledPatch.status == Poco::Net::HTTPResponse::HTTP_CONFLICT,
            "Visual updates should be rejected when post-processing is disabled.");

    visuals.SetEnabled(true);
    auto visualPatch = Request(
        server.Port(), "PATCH", "/api/v1/visual",
        R"({"mirrorX":true,"rotationDegrees":90,"zoom":1.25})");
    Require(visualPatch.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "A valid visual update should return 202.");
    ControlCommand visualCommand{};
    Require(queue.TryDequeue(visualCommand), "Visual update should be queued.");
    Require(visualCommand.type == ControlCommandType::UpdateVisualState,
            "Visual endpoint should queue a visual update.");
    Require((visualCommand.visualPatch.properties & VisualPropertyMirrorX) != 0U,
            "Visual update should include mirrorX.");
    Require((visualCommand.visualPatch.properties & VisualPropertyRotation) != 0U,
            "Visual update should include rotation.");
    Require((visualCommand.visualPatch.properties & VisualPropertyZoom) != 0U,
            "Visual update should include zoom.");
    visuals.Apply(visualCommand.visualPatch);

    auto visualState = Request(server.Port(), "GET", "/api/v1/visual");
    Require(visualState.body->getValue<bool>("mirrorX"),
            "Applied mirrorX should be observable.");
    Require(visualState.body->getValue<double>("rotationDegrees") == 90.0,
            "Applied rotation should be observable.");
    Require(visualState.body->getValue<double>("zoom") == 1.25,
            "Applied zoom should be observable.");

    auto invalidVisual = Request(server.Port(), "PATCH", "/api/v1/visual",
                                 R"({"zoom":0})");
    Require(invalidVisual.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Out-of-range visual values should return 400.");

    auto resetVisual = Request(server.Port(), "POST", "/api/v1/visual/reset", "{}");
    Require(resetVisual.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "Visual reset should return 202.");
    ControlCommand resetCommand{};
    Require(queue.TryDequeue(resetCommand), "Visual reset should be queued.");
    Require(resetCommand.type == ControlCommandType::ResetVisualState,
            "Visual reset should queue the correct command.");
    visuals.Reset();

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

    auto random = Request(server.Port(), "POST", "/api/v1/playback/random", "{}");
    Require(random.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "Random should return 202.");
    Require(random.body->getValue<std::string>("command") == "random",
            "Random response should identify the command.");
    ControlCommand randomCommand{};
    Require(queue.TryDequeue(randomCommand), "Random command should be queued.");
    Require(randomCommand.type == ControlCommandType::RandomPreset,
            "Random command type should match.");
    Require(!randomCommand.smoothTransition,
            "Random should default to a hard transition.");

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

    const auto source = "[preset00]\\nfDecay=0.98\\n";
    auto create = Request(server.Port(), "POST", "/api/v1/presets",
                          std::string("{\"id\":\"workspace/test.milk\",\"source\":\"") +
                              source + "\"}");
    Require(create.status == Poco::Net::HTTPResponse::HTTP_CREATED,
            "Creating a workspace preset should return 201.");
    Require(!create.etag.empty(), "Created preset should include an ETag.");

    auto list = Request(server.Port(), "GET", "/api/v1/presets?source=workspace");
    Require(list.status == Poco::Net::HTTPResponse::HTTP_OK, "Preset list should return 200.");
    Require(list.body->getValue<Poco::UInt64>("total") == 1, "Preset list should contain the created preset.");

    auto invalidPagination = Request(server.Port(), "GET", "/api/v1/presets?offset=nope");
    Require(invalidPagination.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Invalid pagination should return 400.");

    auto traversal = Request(server.Port(), "POST", "/api/v1/presets",
                             R"({"id":"workspace/../escape.milk","source":"x"})");
    Require(traversal.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Path traversal should return 400.");

    auto encodedTraversal = Request(
        server.Port(), "GET", "/api/v1/presets/workspace/%2e%2e/escape.milk");
    Require(encodedTraversal.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Encoded path traversal should return 400.");

    auto unknownCreateField = Request(
        server.Port(), "POST", "/api/v1/presets",
        R"({"id":"workspace/other.milk","source":"x","unexpected":true})");
    Require(unknownCreateField.status == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
            "Unknown create fields should return 400.");

    auto bundledUpdate = Request(server.Port(), "PUT",
                                 "/api/v1/presets/bundled/read-only.milk",
                                 R"({"source":"changed"})", "anything");
    Require(bundledUpdate.status == Poco::Net::HTTPResponse::HTTP_FORBIDDEN,
            "Bundled presets should be read-only.");

    auto get = Request(server.Port(), "GET", "/api/v1/presets/workspace/test.milk");
    Require(get.status == Poco::Net::HTTPResponse::HTTP_OK, "Preset source should return 200.");

    auto missingEtag = Request(server.Port(), "PUT", "/api/v1/presets/workspace/test.milk",
                               R"({"source":"[preset00]\nfDecay=0.9\n"})");
    Require(missingEtag.status == Poco::Net::HTTPResponse::HTTP_PRECONDITION_REQUIRED,
            "Editing without If-Match should return 428.");

    auto update = Request(server.Port(), "PUT", "/api/v1/presets/workspace/test.milk",
                          R"({"source":"[preset00]\nfDecay=0.9\n"})", get.etag);
    Require(update.status == Poco::Net::HTTPResponse::HTTP_OK,
            "Editing with the current ETag should return 200.");

    auto staleUpdate = Request(server.Port(), "PUT", "/api/v1/presets/workspace/test.milk",
                               R"({"source":"stale"})", get.etag);
    Require(staleUpdate.status == Poco::Net::HTTPResponse::HTTP_PRECONDITION_FAILED,
            "Editing with a stale ETag should return 412.");

    auto tooLarge = Request(
        server.Port(), "POST", "/api/v1/presets",
        std::string("{\"id\":\"workspace/large.milk\",\"source\":\"") +
            std::string(1024 * 1024 + 1, 'x') + "\"}");
    Require(tooLarge.status == Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
            "An oversized preset should return 413.");

    auto load = Request(server.Port(), "POST",
                        "/api/v1/presets/workspace/test.milk/load", "{}");
    Require(load.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "Loading a preset should return 202.");
    ControlCommand loadCommand{};
    Require(queue.TryDequeue(loadCommand), "Load command should be queued.");
    Require(loadCommand.type == ControlCommandType::LoadPresetFile,
            "Load endpoint should queue a file load.");
    jobs.MarkRunning(loadCommand.jobId);
    jobs.Complete(loadCommand.jobId, true);
    auto job = Request(server.Port(), "GET",
                       "/api/v1/jobs/" + std::to_string(loadCommand.jobId));
    Require(job.body->getValue<std::string>("state") == "succeeded",
            "Completed load job should be observable.");

    auto reload = Request(server.Port(), "POST",
                          "/api/v1/presets/current/reload",
                          R"({"transition":"smooth"})");
    Require(reload.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "Reloading the current preset should return 202.");
    ControlCommand reloadCommand{};
    Require(queue.TryDequeue(reloadCommand), "Reload command should be queued.");
    Require(reloadCommand.type == ControlCommandType::ReloadCurrentPreset,
            "Reload endpoint should queue a reload.");
    Require(reloadCommand.smoothTransition,
            "Reload endpoint should preserve the transition choice.");

    auto loadSource = Request(server.Port(), "PUT",
                              "/api/v1/presets/current/source",
                              R"({"source":"[preset00]\n","transition":"hard"})");
    Require(loadSource.status == Poco::Net::HTTPResponse::HTTP_ACCEPTED,
            "Loading in-memory source should return 202.");
    ControlCommand sourceCommand{};
    Require(queue.TryDequeue(sourceCommand), "Source load command should be queued.");
    Require(sourceCommand.type == ControlCommandType::LoadPresetSource,
            "Source endpoint should queue an in-memory load.");
    Require(sourceCommand.payload == "[preset00]\n",
            "Source endpoint should preserve preset source.");

    Require(queue.TryEnqueue({ControlCommandType::NextPreset, false}), "First queue slot should work.");
    Require(queue.TryEnqueue({ControlCommandType::PreviousPreset, false}), "Second queue slot should work.");
    auto fullQueue = Request(server.Port(), "POST", "/api/v1/playback/next");
    Require(fullQueue.status == Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
            "Full queue should return 503.");

    server.Stop();
    Require(!server.Running(), "Server should stop.");
    Poco::File(workspace).remove(true);
    Poco::File(bundled).remove(true);
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
