# Network API

This fork includes an HTTP/JSON API for remote preset control, live preset
development, and optional final-image transformations.

> [!WARNING]
> The API has no authentication, authorization, or TLS. By default it listens
> on all interfaces. Anyone who can reach the port can control the application,
> read bundled preset source, and create or modify files inside the configured
> preset workspace. Use it only on a trusted network, bind it to `127.0.0.1`,
> or restrict the port with a firewall.

## Starting the API

The API is enabled by default on port `8080`:

```sh
projectM \
  --networkBindAddress=127.0.0.1 \
  --networkPort=8080
```

Relevant command-line options:

| Option | Purpose |
| --- | --- |
| `--networkEnabled=<0/1>` | Enable or disable the HTTP API. |
| `--networkBindAddress=<address>` | Set the listening address. |
| `--networkPort=<port>` | Set the listening TCP port. |
| `--presetWorkspace=<path>` | Set the writable preset workspace. |
| `--enableVisualPostProcessing` | Enable mirror, rotation, and zoom controls at startup. |

Equivalent configuration properties:

```properties
network.enabled = true
network.bindAddress = 0.0.0.0
network.port = 8080
network.presetWorkspace = ${system.configHomeDir}/projectM/presets
network.maxPresetBytes = 1048576
visual.postProcessingEnabled = false
```

The visual post-processing path is normally enabled with the command-line
switch. There are no separate command-line flags for individual effects.

## Conventions

- Base path: `/api/v1`
- Request and response format: JSON
- Successful queued commands: `202 Accepted`
- Invalid JSON, fields, or values: `400 Bad Request`
- Missing object or route: `404 Not Found`
- Wrong HTTP method: `405 Method Not Allowed`
- ETag mismatch: `412 Precondition Failed`
- Disabled visual post-processing: `409 Conflict`
- Full render command queue: `503 Service Unavailable`

Errors use this shape:

```json
{
  "ok": false,
  "error": {
    "code": "invalid_request",
    "message": "Description of the problem."
  }
}
```

## Health

```http
GET /api/v1/health
```

Example:

```sh
curl -s http://127.0.0.1:8080/api/v1/health
```

```json
{
  "ok": true,
  "service": "projectMSDL",
  "apiVersion": 1
}
```

## Playback

### Next preset

```http
POST /api/v1/playback/next
Content-Type: application/json

{}
```

### Previous preset

```http
POST /api/v1/playback/previous
Content-Type: application/json

{}
```

### Random preset

```http
POST /api/v1/playback/random
Content-Type: application/json

{}
```

Random selection temporarily enables playlist shuffle for the selection and
then restores the previous shuffle setting.

All three endpoints accept an optional transition:

```json
{
  "transition": "smooth"
}
```

Valid values are:

- `hard`: immediate change and the default
- `smooth`: use projectM's configured transition duration

Example:

```sh
curl -s -X POST \
  -H 'Content-Type: application/json' \
  -d '{"transition":"smooth"}' \
  http://127.0.0.1:8080/api/v1/playback/next
```

Example random selection:

```sh
curl -s -X POST \
  -H 'Content-Type: application/json' \
  -d '{}' \
  http://127.0.0.1:8080/api/v1/playback/random
```

## Preset catalogue and storage

The API never accepts arbitrary filesystem paths. Presets use logical IDs:

```text
bundled/author/example.milk
workspace/experiments/example.milk
```

- `bundled/` presets come from the configured projectM preset paths and are
  read-only through the API.
- `workspace/` presets are stored below `network.presetWorkspace` and are
  readable and writable.
- IDs must end in `.milk`.
- Absolute paths, traversal segments, and symbolic-link escapes are rejected.
- Preset source is limited by `network.maxPresetBytes`.

### List presets

```http
GET /api/v1/presets
```

Query parameters:

| Parameter | Description |
| --- | --- |
| `source` | `workspace` or `bundled` |
| `query` | Case-insensitive substring search over logical IDs |
| `offset` | Zero-based result offset; default `0` |
| `limit` | Page size; default `50`, maximum `100` |

Example:

```sh
curl -s \
  'http://127.0.0.1:8080/api/v1/presets?source=workspace&query=wave&limit=20'
```

### Retrieve preset source

```http
GET /api/v1/presets/{presetId}
```

The response includes the source and an `ETag` header used for safe updates.

```sh
curl -i \
  http://127.0.0.1:8080/api/v1/presets/workspace/example.milk
```

### Create a workspace preset

```http
POST /api/v1/presets
Content-Type: application/json
```

```json
{
  "id": "workspace/experiments/example.milk",
  "source": "[preset00]\nfDecay=0.98\n"
}
```

Creation returns `201 Created`, a `Location` header, and an `ETag`. Existing
files are not overwritten.

### Update a workspace preset

```http
PUT /api/v1/presets/{presetId}
Content-Type: application/json
If-Match: "<etag>"
```

```json
{
  "source": "[preset00]\nfDecay=0.95\n"
}
```

`If-Match` is required. Retrieve the preset again if the API returns
`412 Precondition Failed`. Updates use a temporary file and atomic replacement.

Bundled presets cannot be updated.

## Loading presets

Load operations execute on the render thread and return a job ID.

### Load a saved preset

```http
POST /api/v1/presets/{presetId}/load
Content-Type: application/json

{
  "transition": "hard"
}
```

### Reload the current preset file

```http
POST /api/v1/presets/current/reload
Content-Type: application/json

{}
```

Reload rereads the current preset file from disk. It fails if the current
preset was loaded from unsaved in-memory source and therefore has no backing
file.

### Load source without saving

```http
PUT /api/v1/presets/current/source
Content-Type: application/json
```

```json
{
  "source": "[preset00]\nfDecay=0.98\n",
  "transition": "hard"
}
```

This is useful for rapid development. It parses and activates the supplied
source without creating a file.

### Poll a load job

Queued load responses contain:

```json
{
  "ok": true,
  "jobId": 12,
  "state": "queued"
}
```

Poll the job:

```http
GET /api/v1/jobs/12
```

Possible states are `queued`, `running`, `succeeded`, and `failed`. A failed
job includes the parser or loading error when libprojectM provides one:

```json
{
  "ok": true,
  "id": 12,
  "operation": "load_preset_source",
  "state": "failed",
  "error": "Preset parsing error"
}
```

The server retains a bounded set of recent jobs.

## Visual controls

Start the application with post-processing enabled:

```sh
projectM --enableVisualPostProcessing
```

Without this switch, state can still be read, but mutation requests return
`409 Conflict` and the original direct-rendering path remains active.

### Read visual state

```http
GET /api/v1/visual
```

```json
{
  "ok": true,
  "available": true,
  "enabled": true,
  "mirrorX": false,
  "mirrorY": false,
  "rotationDegrees": 0,
  "zoom": 1
}
```

### Update visual state

```http
PATCH /api/v1/visual
Content-Type: application/json
```

All fields are optional, but at least one must be provided:

| Field | Type and range |
| --- | --- |
| `mirrorX` | Boolean |
| `mirrorY` | Boolean |
| `rotationDegrees` | Number from `-360` through `360` |
| `zoom` | Number from `0.1` through `10.0` |

```sh
curl -s -X PATCH \
  -H 'Content-Type: application/json' \
  -d '{"mirrorX":true,"rotationDegrees":90,"zoom":1.2}' \
  http://127.0.0.1:8080/api/v1/visual
```

Updates are queued and applied together at a render-frame boundary.

### Reset visual state

```http
POST /api/v1/visual/reset
Content-Type: application/json

{}
```

Reset disables both mirrors and restores rotation `0` and zoom `1`.
