#include "network/ApiRequestHandler.h"

#include "stb_image.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/NumberParser.h>
#include <Poco/URI.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t controlBodyLimit = 4096;
constexpr std::size_t textureBodyLimit = 16 * 1024 * 1024; //!< 16 MiB cap for texture uploads.

// Decodes JPEG/PNG/BMP bytes into a bottom-row-first RGBA image for projectM's
// texture-load callback. Returns nullptr if the bytes are not a valid image.
DecodedImagePtr DecodeImage(const std::string& body)
{
    stbi_set_flip_vertically_on_load(1); // projectM expects the first row at the bottom.
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(body.data()),
        static_cast<int>(body.size()), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        if (pixels != nullptr)
        {
            stbi_image_free(pixels);
        }
        return nullptr;
    }
    auto image = std::make_shared<DecodedImage>();
    image->width = width;
    image->height = height;
    image->channels = 4;
    image->pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

// A texture name must be a non-empty run of [A-Za-z0-9_.-] so it is safe to use
// in a URL path and as a preset texture reference.
bool ValidTextureName(const std::string& name)
{
    if (name.empty() || name.size() > 128)
    {
        return false;
    }
    for (const char character : name)
    {
        const bool ok = (character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z') ||
                        (character >= '0' && character <= '9') ||
                        character == '_' || character == '-' || character == '.';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

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

void MethodNotAllowed(Poco::Net::HTTPServerResponse& response, const std::string& allowed)
{
    response.set("Allow", allowed);
    WriteError(response, Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED,
               "method_not_allowed", "This endpoint requires " + allowed + ".");
}

bool ReadBody(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response,
              std::size_t limit, std::string& body)
{
    if (request.hasContentLength() && request.getContentLength64() > limit)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
                   "request_too_large", "Request body exceeds the configured limit.");
        return false;
    }
    std::array<char, 8192> buffer{};
    auto& input = request.stream();
    while (input.good())
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0)
        {
            break;
        }
        body.append(buffer.data(), static_cast<std::size_t>(count));
        if (body.size() > limit)
        {
            WriteError(response, Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE,
                       "request_too_large", "Request body exceeds the configured limit.");
            return false;
        }
    }
    return true;
}

Poco::JSON::Object::Ptr ParseObject(const std::string& body)
{
    if (body.empty())
    {
        return new Poco::JSON::Object;
    }
    Poco::JSON::Parser parser;
    return parser.parse(body).extract<Poco::JSON::Object::Ptr>();
}

bool Transition(const Poco::JSON::Object::Ptr& object, bool& smooth,
                Poco::Net::HTTPServerResponse& response)
{
    smooth = false;
    if (!object->has("transition"))
    {
        return true;
    }
    try
    {
        const auto value = object->getValue<std::string>("transition");
        if (value == "hard")
        {
            return true;
        }
        if (value == "smooth")
        {
            smooth = true;
            return true;
        }
    }
    catch (...)
    {
    }
    WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
               "invalid_request", "transition must be either hard or smooth.");
    return false;
}

bool Fields(const Poco::JSON::Object::Ptr& object,
            const std::vector<std::string>& allowed,
            Poco::Net::HTTPServerResponse& response)
{
    for (const auto& name : object->getNames())
    {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                       "invalid_request", "Unknown JSON field: " + name);
            return false;
        }
    }
    return true;
}

Poco::JSON::Object DocumentJson(const PresetDocument& preset, bool source)
{
    Poco::JSON::Object result;
    result.set("id", preset.id);
    result.set("name", preset.name);
    result.set("writable", preset.writable);
    result.set("etag", preset.etag);
    if (source)
    {
        result.set("source", preset.source);
    }
    return result;
}

Poco::JSON::Object VisualJson(const VisualState& visual)
{
    Poco::JSON::Object result;
    result.set("ok", true);
    result.set("available", true);
    result.set("enabled", visual.enabled);
    result.set("mirrorX", visual.mirrorX);
    result.set("mirrorY", visual.mirrorY);
    result.set("rotationDegrees", visual.rotationDegrees);
    result.set("zoom", visual.zoom);
    return result;
}

void PresetErrorResponse(Poco::Net::HTTPServerResponse& response, const PresetError& error)
{
    switch (error.Kind())
    {
        case PresetErrorKind::InvalidId:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "invalid_preset_id", error.what());
            break;
        case PresetErrorKind::NotFound:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND, "preset_not_found", error.what());
            break;
        case PresetErrorKind::ReadOnly:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_FORBIDDEN, "preset_read_only", error.what());
            break;
        case PresetErrorKind::AlreadyExists:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_CONFLICT, "preset_exists", error.what());
            break;
        case PresetErrorKind::Conflict:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_PRECONDITION_FAILED, "preset_conflict", error.what());
            break;
        case PresetErrorKind::TooLarge:
            WriteError(response, Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE, "preset_too_large", error.what());
            break;
    }
}

std::string QueryValue(const Poco::URI& uri, const std::string& key, const std::string& fallback = "")
{
    for (const auto& parameter : uri.getQueryParameters())
    {
        if (parameter.first == key)
        {
            return parameter.second;
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// Live-settable configuration registry.
//
// Single source of truth for the HTTP config API: it drives validation, the
// schema endpoint, and reading of effective values. Every key here is applied
// live by ProjectMWrapper's configuration-change observer.
// ---------------------------------------------------------------------------

enum class ConfigType
{
    Bool,
    Int,
    Double
};

struct ConfigOption
{
    const char* name;        //!< API field name, e.g. "displayDuration".
    const char* key;         //!< Full config key, e.g. "projectM.displayDuration".
    ConfigType type;
    double min;              //!< Inclusive lower bound (numeric types only).
    double max;              //!< Inclusive upper bound (numeric types only).
    double defaultValue;     //!< Fallback if the key is set in no layer (bool: 0/1).
    const char* description;
};

const std::vector<ConfigOption>& ConfigOptions()
{
    static const std::vector<ConfigOption> options = {
        {"displayDuration", "projectM.displayDuration", ConfigType::Double, 0.0, 86400.0, 30.0,
         "Seconds each preset is shown before auto-advancing. 0 disables auto-advance."},
        {"shuffleEnabled", "projectM.shuffleEnabled", ConfigType::Bool, 0.0, 0.0, 1.0,
         "Pick the next preset at random when auto-advancing."},
        {"presetLocked", "projectM.presetLocked", ConfigType::Bool, 0.0, 0.0, 0.0,
         "Lock the current preset so the playlist stops advancing."},
        {"transitionDuration", "projectM.transitionDuration", ConfigType::Double, 0.0, 60.0, 3.0,
         "Soft-cut blend duration in seconds."},
        {"hardCutsEnabled", "projectM.hardCutsEnabled", ConfigType::Bool, 0.0, 0.0, 0.0,
         "Enable beat-triggered hard cuts to the next preset."},
        {"hardCutDuration", "projectM.hardCutDuration", ConfigType::Double, 0.0, 86400.0, 20.0,
         "Minimum seconds between hard cuts."},
        {"hardCutSensitivity", "projectM.hardCutSensitivity", ConfigType::Double, 0.0, 5.0, 1.0,
         "Beat sensitivity for hard cuts (0-5)."},
        {"beatSensitivity", "projectM.beatSensitivity", ConfigType::Double, 0.0, 2.0, 1.0,
         "Overall beat detection sensitivity (0-2)."},
        {"aspectCorrectionEnabled", "projectM.aspectCorrectionEnabled", ConfigType::Bool, 0.0, 0.0, 1.0,
         "Correct preset aspect ratio to the window."},
        {"meshX", "projectM.meshX", ConfigType::Int, 1.0, 512.0, 96.0,
         "Per-pixel mesh width."},
        {"meshY", "projectM.meshY", ConfigType::Int, 1.0, 512.0, 54.0,
         "Per-pixel mesh height."},
        {"fps", "projectM.fps", ConfigType::Int, 1.0, 1000.0, 60.0,
         "Target FPS used for projectM timing. The window frame limiter may need a restart."},
        {"fullscreen", "window.fullscreen", ConfigType::Bool, 0.0, 0.0, 0.0,
         "Switch the window between fullscreen and windowed mode."},
    };
    return options;
}

const ConfigOption* FindConfigOption(const std::string& name)
{
    for (const auto& option : ConfigOptions())
    {
        if (name == option.name)
        {
            return &option;
        }
    }
    return nullptr;
}

std::string ConfigTypeName(ConfigType type)
{
    switch (type)
    {
        case ConfigType::Bool:
            return "boolean";
        case ConfigType::Int:
            return "integer";
        case ConfigType::Double:
            return "number";
    }
    return "string";
}

// Which layer currently supplies the effective value, in precedence order.
std::string ConfigSource(const ConfigLayers& layers, const std::string& key)
{
    if (layers.runtime && layers.runtime->hasProperty(key))
    {
        return "runtime";
    }
    if (layers.commandLine && layers.commandLine->hasProperty(key))
    {
        return "commandLine";
    }
    if (layers.user && layers.user->hasProperty(key))
    {
        return "user";
    }
    return "default";
}

// Reads the current effective value as a typed JSON value.
Poco::Dynamic::Var ConfigEffectiveValue(const ConfigLayers& layers, const ConfigOption& option)
{
    const auto& config = *layers.effective;
    switch (option.type)
    {
        case ConfigType::Bool:
            return config.getBool(option.key, option.defaultValue != 0.0);
        case ConfigType::Int:
            return config.getInt(option.key, static_cast<int>(option.defaultValue));
        case ConfigType::Double:
            return config.getDouble(option.key, option.defaultValue);
    }
    return {};
}

} // namespace

ApiRequestHandler::ApiRequestHandler(ControlCommandQueue& commands, JobRegistry& jobs,
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

void ApiRequestHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                      Poco::Net::HTTPServerResponse& response)
{
    try
    {
        const Poco::URI uri(request.getURI());
        const auto path = uri.getPath();
        const auto& method = request.getMethod();

        if (path == "/api/v1/health")
        {
            if (method != "GET")
            {
                return MethodNotAllowed(response, "GET");
            }
            Poco::JSON::Object body;
            body.set("ok", true);
            body.set("service", "projectMSDL");
            body.set("apiVersion", 1);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, body);
        }

        if (path == "/api/v1/config/schema")
        {
            if (method != "GET")
            {
                return MethodNotAllowed(response, "GET");
            }
            Poco::JSON::Array options;
            for (const auto& option : ConfigOptions())
            {
                Poco::JSON::Object item;
                item.set("name", option.name);
                item.set("key", option.key);
                item.set("type", ConfigTypeName(option.type));
                if (option.type != ConfigType::Bool)
                {
                    item.set("min", option.min);
                    item.set("max", option.max);
                }
                item.set("description", option.description);
                options.add(item);
            }
            Poco::JSON::Object body;
            body.set("ok", true);
            body.set("options", options);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, body);
        }

        if (path == "/api/v1/config")
        {
            if (method == "GET")
            {
                Poco::JSON::Object config;
                for (const auto& option : ConfigOptions())
                {
                    Poco::JSON::Object entry;
                    entry.set("value", ConfigEffectiveValue(_configLayers, option));
                    entry.set("source", ConfigSource(_configLayers, option.key));
                    config.set(option.name, entry);
                }
                Poco::JSON::Object body;
                body.set("ok", true);
                body.set("config", config);
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, body);
            }
            if (method == "DELETE")
            {
                // Clear every runtime override, handing control back to the
                // command-line / user / default layers.
                int cleared = 0;
                for (const auto& option : ConfigOptions())
                {
                    if (!_configLayers.runtime->hasProperty(option.key))
                    {
                        continue;
                    }
                    ControlCommand command;
                    command.type = ControlCommandType::ClearConfig;
                    command.configKey = option.key;
                    if (!_commands.TryEnqueue(command))
                    {
                        return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                          "queue_full", "The remote-control command queue is full.");
                    }
                    ++cleared;
                }
                Poco::JSON::Object result;
                result.set("ok", true);
                result.set("queued", true);
                result.set("cleared", cleared);
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
            }
            if (method != "PATCH")
            {
                return MethodNotAllowed(response, "GET, PATCH, DELETE");
            }

            std::string requestBody;
            if (!ReadBody(request, response, controlBodyLimit, requestBody))
            {
                return;
            }
            const auto object = ParseObject(requestBody);
            if (object->size() == 0)
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                  "invalid_request", "At least one setting is required.");
            }

            // Validate every field up-front so an invalid setting rejects the whole
            // request instead of applying a partial, ambiguous update.
            struct PendingSet
            {
                std::string key;
                std::string value;
            };
            std::vector<PendingSet> pending;
            for (const auto& name : object->getNames())
            {
                const auto* option = FindConfigOption(name);
                if (option == nullptr)
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request", "Unknown setting: " + name);
                }
                const auto value = object->get(name);
                std::string normalized;
                if (option->type == ConfigType::Bool)
                {
                    if (!value.isBoolean())
                    {
                        return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                          "invalid_request",
                                          std::string(option->name) + " must be a boolean.");
                    }
                    normalized = value.convert<bool>() ? "true" : "false";
                }
                else
                {
                    if (!value.isNumeric())
                    {
                        return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                          "invalid_request",
                                          std::string(option->name) + " must be a number.");
                    }
                    const double number = value.convert<double>();
                    if (!std::isfinite(number) || number < option->min || number > option->max)
                    {
                        std::ostringstream message;
                        message << option->name << " must be between " << option->min
                                << " and " << option->max << ".";
                        return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                          "invalid_request", message.str());
                    }
                    if (option->type == ConfigType::Int)
                    {
                        normalized = std::to_string(static_cast<long long>(std::llround(number)));
                    }
                    else
                    {
                        std::ostringstream numberText;
                        numberText << number;
                        normalized = numberText.str();
                    }
                }
                pending.push_back({option->key, normalized});
            }

            for (const auto& item : pending)
            {
                ControlCommand command;
                command.type = ControlCommandType::SetConfig;
                command.configKey = item.key;
                command.configValue = item.value;
                if (!_commands.TryEnqueue(command))
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                      "queue_full", "The remote-control command queue is full.");
                }
            }

            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("queued", true);
            result.set("updated", static_cast<int>(pending.size()));
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
        }

        {
            const std::string configPrefix = "/api/v1/config/";
            if (path.compare(0, configPrefix.size(), configPrefix) == 0)
            {
                // Clear a single runtime override (schema is handled above).
                if (method != "DELETE")
                {
                    return MethodNotAllowed(response, "DELETE");
                }
                const auto name = path.substr(configPrefix.size());
                const auto* option = FindConfigOption(name);
                if (option == nullptr)
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
                                      "not_found", "Unknown setting: " + name);
                }
                ControlCommand command;
                command.type = ControlCommandType::ClearConfig;
                command.configKey = option->key;
                if (!_commands.TryEnqueue(command))
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                      "queue_full", "The remote-control command queue is full.");
                }
                Poco::JSON::Object result;
                result.set("ok", true);
                result.set("queued", true);
                result.set("cleared", option->name);
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
            }
        }

        if (path == "/api/v1/textures")
        {
            // Named in-memory textures served to presets via image=<name>.
            if (method == "GET")
            {
                Poco::JSON::Array items;
                for (const auto& entry : _textures.List())
                {
                    Poco::JSON::Object item;
                    item.set("name", entry.name);
                    item.set("width", entry.width);
                    item.set("height", entry.height);
                    items.add(item);
                }
                Poco::JSON::Object body;
                body.set("ok", true);
                body.set("textures", items);
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, body);
            }
            if (method != "DELETE")
            {
                return MethodNotAllowed(response, "GET, DELETE");
            }
            const auto cleared = _textures.Clear();
            if (cleared > 0)
            {
                ControlCommand command;
                command.type = ControlCommandType::ReloadTextures;
                _commands.TryEnqueue(command);
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("cleared", static_cast<int>(cleared));
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, result);
        }

        {
            const std::string texturePrefix = "/api/v1/textures/";
            if (path.compare(0, texturePrefix.size(), texturePrefix) == 0)
            {
                const auto name = path.substr(texturePrefix.size());
                if (!ValidTextureName(name))
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_texture_name",
                                      "Texture name must be 1-128 characters of [A-Za-z0-9_.-].");
                }

                if (method == "DELETE")
                {
                    const bool existed = _textures.Remove(name);
                    if (!existed)
                    {
                        return WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
                                          "not_found", "No texture named " + name + ".");
                    }
                    ControlCommand command;
                    command.type = ControlCommandType::ReloadTextures;
                    _commands.TryEnqueue(command);
                    Poco::JSON::Object result;
                    result.set("ok", true);
                    result.set("removed", name);
                    return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, result);
                }
                if (method != "PUT")
                {
                    return MethodNotAllowed(response, "PUT, DELETE");
                }

                std::string body;
                if (!ReadBody(request, response, textureBodyLimit, body))
                {
                    return;
                }
                const auto image = DecodeImage(body);
                if (image == nullptr)
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_image",
                                      "Request body is not a supported image (JPEG, PNG, or BMP).");
                }

                _textures.Set(name, image);
                ControlCommand command;
                command.type = ControlCommandType::ReloadTextures;
                if (!_commands.TryEnqueue(command))
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                      "queue_full", "The remote-control command queue is full.");
                }
                Poco::JSON::Object result;
                result.set("ok", true);
                result.set("name", name);
                result.set("width", image->width);
                result.set("height", image->height);
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
            }
        }

        if (path == "/api/v1/visual")
        {
            if (method == "GET")
            {
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK,
                                 VisualJson(_visuals.Get()));
            }
            if (method != "PATCH")
            {
                return MethodNotAllowed(response, "GET, PATCH");
            }
            if (!_visuals.Get().enabled)
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_CONFLICT,
                                  "visual_post_processing_disabled",
                                  "Restart with --enableVisualPostProcessing to use visual controls.");
            }

            std::string body;
            if (!ReadBody(request, response, controlBodyLimit, body))
            {
                return;
            }
            const auto object = ParseObject(body);
            if (!Fields(object, {"mirrorX", "mirrorY", "rotationDegrees", "zoom"}, response))
            {
                return;
            }
            if (object->size() == 0)
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                  "invalid_request", "At least one visual property is required.");
            }

            VisualStatePatch patch;
            for (const auto& property : {std::string("mirrorX"), std::string("mirrorY")})
            {
                if (!object->has(property))
                {
                    continue;
                }
                const auto value = object->get(property);
                if (!value.isBoolean())
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request", property + " must be a boolean.");
                }
                if (property == "mirrorX")
                {
                    patch.properties |= VisualPropertyMirrorX;
                    patch.mirrorX = value.convert<bool>();
                }
                else
                {
                    patch.properties |= VisualPropertyMirrorY;
                    patch.mirrorY = value.convert<bool>();
                }
            }
            if (object->has("rotationDegrees"))
            {
                const auto value = object->get("rotationDegrees");
                if (!value.isNumeric())
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request", "rotationDegrees must be a number.");
                }
                patch.rotationDegrees = value.convert<double>();
                if (!std::isfinite(patch.rotationDegrees) ||
                    patch.rotationDegrees < -360.0 || patch.rotationDegrees > 360.0)
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request",
                                      "rotationDegrees must be between -360 and 360.");
                }
                patch.properties |= VisualPropertyRotation;
            }
            if (object->has("zoom"))
            {
                const auto value = object->get("zoom");
                if (!value.isNumeric())
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request", "zoom must be a number.");
                }
                patch.zoom = value.convert<double>();
                if (!std::isfinite(patch.zoom) || patch.zoom < 0.1 || patch.zoom > 10.0)
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                      "invalid_request", "zoom must be between 0.1 and 10.");
                }
                patch.properties |= VisualPropertyZoom;
            }

            ControlCommand command;
            command.type = ControlCommandType::UpdateVisualState;
            command.visualPatch = patch;
            if (!_commands.TryEnqueue(command))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                  "queue_full", "The remote-control command queue is full.");
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("queued", true);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
        }

        if (path == "/api/v1/visual/reset")
        {
            if (method != "POST")
            {
                return MethodNotAllowed(response, "POST");
            }
            if (!_visuals.Get().enabled)
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_CONFLICT,
                                  "visual_post_processing_disabled",
                                  "Restart with --enableVisualPostProcessing to use visual controls.");
            }
            std::string body;
            if (!ReadBody(request, response, controlBodyLimit, body))
            {
                return;
            }
            const auto object = ParseObject(body);
            if (!Fields(object, {}, response))
            {
                return;
            }
            ControlCommand command;
            command.type = ControlCommandType::ResetVisualState;
            if (!_commands.TryEnqueue(command))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                  "queue_full", "The remote-control command queue is full.");
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("queued", true);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
        }

        if (path == "/api/v1/playback/current")
        {
            if (method != "GET")
            {
                return MethodNotAllowed(response, "GET");
            }
            const auto playback = _playback.Get();
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("name", playback.presetName);
            result.set("fileBacked", playback.fileBacked);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, result);
        }

        if (path == "/api/v1/playback/next" ||
            path == "/api/v1/playback/previous" ||
            path == "/api/v1/playback/random")
        {
            if (method != "POST")
            {
                return MethodNotAllowed(response, "POST");
            }
            std::string body;
            if (!ReadBody(request, response, controlBodyLimit, body))
            {
                return;
            }
            const auto object = ParseObject(body);
            if (!Fields(object, {"transition"}, response))
            {
                return;
            }
            bool smooth = false;
            if (!Transition(object, smooth, response))
            {
                return;
            }
            auto type = ControlCommandType::NextPreset;
            std::string commandName{"next"};
            if (path == "/api/v1/playback/previous")
            {
                type = ControlCommandType::PreviousPreset;
                commandName = "previous";
            }
            else if (path == "/api/v1/playback/random")
            {
                type = ControlCommandType::RandomPreset;
                commandName = "random";
            }
            if (!_commands.TryEnqueue({type, smooth, 0, ""}))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                  "queue_full", "The remote-control command queue is full.");
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("command", commandName);
            result.set("queued", true);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
        }

        if (path.compare(0, 13, "/api/v1/jobs/") == 0)
        {
            if (method != "GET")
            {
                return MethodNotAllowed(response, "GET");
            }
            std::uint64_t id = 0;
            if (!Poco::NumberParser::tryParseUnsigned64(path.substr(13), id))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                  "invalid_job_id", "Job ID must be an unsigned integer.");
            }
            Job job;
            if (!_jobs.Get(id, job))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
                                  "job_not_found", "Job was not found.");
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("id", job.id);
            result.set("operation", job.operation);
            result.set("state", JobStateName(job.state));
            if (!job.error.empty())
            {
                result.set("error", job.error);
            }
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, result);
        }

        if (path == "/api/v1/presets" && method == "GET")
        {
            unsigned int offset = 0;
            unsigned int limit = 50;
            if (!Poco::NumberParser::tryParseUnsigned(QueryValue(uri, "offset", "0"), offset) ||
                !Poco::NumberParser::tryParseUnsigned(QueryValue(uri, "limit", "50"), limit))
            {
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                                  "invalid_pagination", "offset and limit must be unsigned integers.");
            }
            limit = std::min(limit, 100U);
            const auto list = _presets.List(offset, limit, QueryValue(uri, "source"), QueryValue(uri, "query"));
            Poco::JSON::Array items;
            for (const auto& preset : list.items)
            {
                items.add(DocumentJson(preset, false));
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("offset", offset);
            result.set("limit", limit);
            result.set("total", static_cast<Poco::UInt64>(list.total));
            result.set("items", items);
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, result);
        }

        if (path == "/api/v1/presets" && method == "POST")
        {
            std::string body;
            if (!ReadBody(request, response, _presets.MaxPresetBytes() + controlBodyLimit, body))
            {
                return;
            }
            const auto object = ParseObject(body);
            if (!Fields(object, {"id", "source"}, response))
            {
                return;
            }
            const auto created = _presets.Create(object->getValue<std::string>("id"),
                                                  object->getValue<std::string>("source"));
            response.set("Location", "/api/v1/presets/" + created.id);
            response.set("ETag", "\"" + created.etag + "\"");
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_CREATED, DocumentJson(created, true));
        }

        if (path == "/api/v1/presets/current/reload" ||
            path == "/api/v1/presets/current/source")
        {
            const auto expectedMethod = path.back() == 'd' ? "POST" : "PUT";
            if (method != expectedMethod)
            {
                return MethodNotAllowed(response, expectedMethod);
            }
            std::string body;
            if (!ReadBody(request, response, _presets.MaxPresetBytes() + controlBodyLimit, body))
            {
                return;
            }
            const auto object = ParseObject(body);
            const auto allowedFields = path.back() == 'd'
                                           ? std::vector<std::string>{"transition"}
                                           : std::vector<std::string>{"source", "transition"};
            if (!Fields(object, allowedFields, response))
            {
                return;
            }
            bool smooth = false;
            if (!Transition(object, smooth, response))
            {
                return;
            }
            const auto type = path.back() == 'd' ? ControlCommandType::ReloadCurrentPreset
                                                 : ControlCommandType::LoadPresetSource;
            const auto operation = type == ControlCommandType::ReloadCurrentPreset ? "reload_preset"
                                                                                    : "load_preset_source";
            const auto payload = type == ControlCommandType::LoadPresetSource
                                     ? object->getValue<std::string>("source")
                                     : std::string();
            const auto jobId = _jobs.Create(operation);
            if (!_commands.TryEnqueue({type, smooth, jobId, payload}))
            {
                _jobs.Complete(jobId, false, "The remote-control command queue is full.");
                return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                  "queue_full", "The remote-control command queue is full.");
            }
            Poco::JSON::Object result;
            result.set("ok", true);
            result.set("jobId", jobId);
            result.set("state", "queued");
            return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
        }

        const std::string presetPrefix = "/api/v1/presets/";
        if (path.compare(0, presetPrefix.size(), presetPrefix) == 0)
        {
            auto id = path.substr(presetPrefix.size());
            bool load = false;
            const std::string loadSuffix = "/load";
            if (id.size() > loadSuffix.size() &&
                id.compare(id.size() - loadSuffix.size(), loadSuffix.size(), loadSuffix) == 0)
            {
                load = true;
                id.erase(id.size() - loadSuffix.size());
            }

            if (load)
            {
                if (method != "POST")
                {
                    return MethodNotAllowed(response, "POST");
                }
                std::string body;
                if (!ReadBody(request, response, controlBodyLimit, body))
                {
                    return;
                }
                const auto object = ParseObject(body);
                if (!Fields(object, {"transition"}, response))
                {
                    return;
                }
                bool smooth = false;
                if (!Transition(object, smooth, response))
                {
                    return;
                }
                const auto jobId = _jobs.Create("load_preset");
                if (!_commands.TryEnqueue(
                        {ControlCommandType::LoadPresetFile, smooth, jobId, _presets.ResolvePath(id)}))
                {
                    _jobs.Complete(jobId, false, "The remote-control command queue is full.");
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE,
                                      "queue_full", "The remote-control command queue is full.");
                }
                Poco::JSON::Object result;
                result.set("ok", true);
                result.set("jobId", jobId);
                result.set("state", "queued");
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_ACCEPTED, result);
            }

            if (method == "GET")
            {
                const auto preset = _presets.Get(id);
                response.set("ETag", "\"" + preset.etag + "\"");
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, DocumentJson(preset, true));
            }
            if (method == "PUT")
            {
                std::string body;
                if (!ReadBody(request, response, _presets.MaxPresetBytes() + controlBodyLimit, body))
                {
                    return;
                }
                const auto object = ParseObject(body);
                if (!Fields(object, {"source"}, response))
                {
                    return;
                }
                auto etag = request.get("If-Match", "");
                if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"')
                {
                    etag = etag.substr(1, etag.size() - 2);
                }
                if (etag.empty())
                {
                    return WriteError(response, Poco::Net::HTTPResponse::HTTP_PRECONDITION_REQUIRED,
                                      "etag_required", "If-Match is required when editing a preset.");
                }
                const auto updated = _presets.Update(id, object->getValue<std::string>("source"), etag);
                response.set("ETag", "\"" + updated.etag + "\"");
                return WriteJson(response, Poco::Net::HTTPResponse::HTTP_OK, DocumentJson(updated, true));
            }
            return MethodNotAllowed(response, "GET, PUT");
        }

        WriteError(response, Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
                   "not_found", "The requested API endpoint does not exist.");
    }
    catch (const PresetError& error)
    {
        PresetErrorResponse(response, error);
    }
    catch (const Poco::Exception&)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                   "invalid_json", "Request body must be a valid JSON object with required fields.");
    }
    catch (const std::exception& error)
    {
        WriteError(response, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
                   "invalid_request", error.what());
    }
}

ApiRequestHandlerFactory::ApiRequestHandlerFactory(ControlCommandQueue& commands, JobRegistry& jobs,
                                                   PresetRepository& presets,
                                                   VisualStateStore& visuals,
                                                   PlaybackStateStore& playback,
                                                   TextureStore& textures,
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

Poco::Net::HTTPRequestHandler* ApiRequestHandlerFactory::createRequestHandler(
    const Poco::Net::HTTPServerRequest&)
{
    return new ApiRequestHandler(_commands, _jobs, _presets, _visuals,
                                 _playback, _textures, _configLayers);
}
