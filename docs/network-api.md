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

## Decks

A *deck* is an independent projectM instance with its own preset and playlist.
The frontend runs `--decks N` (1-4, default 1) decks. Deck 0 is always the
visible base output. When post-processing is enabled
(`--enableVisualPostProcessing`), the extra decks are exposed to the shader
chain as named textures `deck1`, `deck2`, … so a pass can composite them (see
[Compositing decks](#compositing-decks)). With post-processing disabled only
deck 0 is rendered, so extra decks are created but never shown — the frontend
logs a warning at startup in that case.

Every instance-scoped playback and preset endpoint accepts an optional
`?deck=N` query parameter (default `0`). Requesting a deck index that does not
exist returns `404 deck_not_found`; a non-numeric value returns
`400 invalid_deck`. With the default single deck, omitting `?deck` behaves
exactly as before.

### List decks

```http
GET /api/v1/decks
```

Returns the deck count and each deck's current preset:

```json
{
  "ok": true,
  "count": 2,
  "decks": [
    {"index": 0, "current": {"name": "Artist - Base.milk", "id": "bundled/pack/Artist - Base.milk", "fileBacked": true}},
    {"index": 1, "current": {"name": "Artist - Overlay.milk", "id": "bundled/pack/Artist - Overlay.milk", "fileBacked": true}}
  ]
}
```

```sh
curl -s http://127.0.0.1:8080/api/v1/decks
```

### Performance tuning

Each deck is a full projectM instance, so two decks cost roughly twice the GPU of
one, and everything renders at the window's drawable size — which is **2× the
logical size on a high-DPI/Retina display**. Three launch-time levers reduce that:

- **`--renderScale <0..1>`** (config `visual.renderScale`, default `1.0`): under
  post-processing, decks and the offscreen compositor buffers render at
  `drawable × scale` and the final pass upscales to the window. `0.5` quarters the
  pixel work — the single biggest lever on Retina — at the cost of some sharpness.
  Only applies when post-processing is enabled.
- **Per-deck mesh** (config `projectM.deck<i>.meshX` / `.meshY`): overrides the
  global `projectM.meshX/meshY` for one deck. A preset that does no per-pixel warp
  (a spectrum analyzer, a static overlay) gains nothing from a fine mesh — drop it
  to e.g. `8×8` to slash per-pixel work with no visible change, while another deck
  keeps a full mesh.
- **Global mesh** (`projectM.meshX/meshY`, also live via `PATCH /api/v1/config`):
  lower it if no deck needs fine per-pixel motion.

Example — two decks at half resolution:

```sh
projectMSDL --decks 2 --enableVisualPostProcessing --renderScale 0.5
```

Per-deck mesh is set in the config file (`projectMSDL.properties`), since it is
per-instance rather than a command-line flag:

```properties
projectM.meshX = 96
projectM.meshY = 54
projectM.deck0.meshX = 8   # analyzer deck: no per-pixel warp, tiny mesh
projectM.deck0.meshY = 8
```

When the compositor's transform (mirror/rotation/zoom) is left at its defaults, the
built-in transform pass is skipped automatically, so an identity setup costs only
your own chain passes.

## Playback

### Current preset

```http
GET /api/v1/playback/current[?deck=N]
```

Returns the active preset's file name without exposing its filesystem path:

```json
{
  "ok": true,
  "deck": 0,
  "name": "Artist - Example.milk",
  "id": "bundled/pack/Artist - Example.milk",
  "fileBacked": true
}
```

`id` is the logical preset ID used by [List presets](#list-presets) and
[Load a saved preset](#load-a-saved-preset) (`bundled/<pack>/<name>.milk` or
`workspace/...`), i.e. the reverse mapping of the resolved file path.

`name` and `id` are empty and `fileBacked` is `false` before a preset is active
or when the current preset was loaded from unsaved in-memory source. `id` is
also empty when the active file cannot be mapped back to a bundled or workspace
preset.

```sh
curl -s http://127.0.0.1:8080/api/v1/playback/current
```

### Next preset

```http
POST /api/v1/playback/next[?deck=N]
Content-Type: application/json

{}
```

### Previous preset

```http
POST /api/v1/playback/previous[?deck=N]
Content-Type: application/json

{}
```

### Random preset

```http
POST /api/v1/playback/random[?deck=N]
Content-Type: application/json

{}
```

Random selection temporarily enables playlist shuffle for the selection and
then restores the previous shuffle setting.

All three endpoints advance the deck named by `?deck` (default `0`) and accept
an optional transition:

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

Load operations execute on the render thread and return a job ID. Each accepts
an optional `?deck=N` (default `0`) to target a specific deck.

### Load a saved preset

```http
POST /api/v1/presets/{presetId}/load[?deck=N]
Content-Type: application/json

{
  "transition": "hard"
}
```

### Reload the current preset file

```http
POST /api/v1/presets/current/reload[?deck=N]
Content-Type: application/json

{}
```

Reload rereads the current preset file from disk. It fails if the current
preset was loaded from unsaved in-memory source and therefore has no backing
file.

### Load source without saving

```http
PUT /api/v1/presets/current/source[?deck=N]
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

## In-memory textures

Presets can display images (e.g. the current album cover, a logo, a backdrop)
that are provided **in memory** — no file is written to disk. You upload an
image under a name of your choice, and a preset references it by that name with
a textured custom shape:

```text
shapecode_0_textured=1
shapecode_0_image=cover
```

When projectM needs a texture it asks the application, which serves a matching
uploaded image via a texture-load callback. If no texture with that name has
been uploaded, projectM falls back to the filesystem (and, if not found there,
draws a placeholder). Names are matched case-insensitively.

You can store several textures at once and reference each by name from any
preset.

### Upload a texture

```http
PUT /api/v1/textures/{name}
Content-Type: image/jpeg
```

The name must be 1–128 characters of `[A-Za-z0-9_.-]`. The body is the raw image
bytes (JPEG, PNG, or BMP, up to 16 MiB). Uploading a name that already exists
replaces it. The texture cache is reloaded so presets pick up the change
immediately.

```sh
curl -s -X PUT \
  -H 'Content-Type: image/jpeg' \
  --data-binary @cover.jpg \
  http://127.0.0.1:8080/api/v1/textures/cover
```

```json
{
  "ok": true,
  "queued": true,
  "name": "cover",
  "width": 500,
  "height": 500
}
```

Invalid names return `400 Bad Request` with code `invalid_texture_name`;
unsupported image data returns `400` with code `invalid_image`.

### List textures

```http
GET /api/v1/textures
```

```json
{
  "ok": true,
  "textures": [
    { "name": "cover", "width": 500, "height": 500 }
  ]
}
```

### Remove textures

```http
DELETE /api/v1/textures/{name}
```

Removes a single texture (`404 Not Found` if it does not exist).

```http
DELETE /api/v1/textures
```

Removes all textures and reports how many were `cleared`.

> [!NOTE]
> Textures live only in memory and are dropped on restart or when replaced —
> they are never written to disk. Each change reloads all textures, so this is
> intended for occasional updates (e.g. once per track), not per-frame updates.

## Text overlay (toast)

Displays a short text message over the visualization using the application's
built-in on-screen text renderer (the same toast mechanism used for internal
notifications). Useful for "now playing" text, status messages, etc.

```http
POST /api/v1/toast
Content-Type: application/json
```

| Field | Type and range |
| --- | --- |
| `text` | String, 1–500 characters (required) |
| `durationSeconds` | Number from `0.5` through `60`; default `3` |
| `position` | `center` (default), `top`, `bottom`, `left`, `right`, `top-left`, `top-right`, `bottom-left`, `bottom-right` |
| `color` | Hex string `#RRGGBB` or `#RRGGBBAA`; default white |
| `size` | Font size multiplier, `0.25`–`8`; default `1` |
| `animation` | `fade` (default), `scroll` (horizontal ticker), `slide` (slides in from the left) |

```sh
curl -s -X POST \
  -H 'Content-Type: application/json' \
  -d '{"text":"Now Playing: Artist - Title","durationSeconds":5}' \
  http://127.0.0.1:8080/api/v1/toast
```

A scrolling "now playing" ticker along the bottom:

```sh
curl -s -X POST \
  -H 'Content-Type: application/json' \
  -d '{"text":"♪ Artist — Song Title ♪","animation":"scroll","position":"bottom","color":"#ffd000","durationSeconds":8}' \
  http://127.0.0.1:8080/api/v1/toast
```

The toast is queued and rendered on the next frame. Text rendering happens in
the application, so the request carries a plain string — no image is needed.
With `scroll`, the text travels across the screen over the display time and the
`position` sets the vertical placement only.

> [!NOTE]
> Toasts respect the `projectM.displayToasts` setting; if it is disabled, the
> message is accepted but not shown.

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

## Configuration

Live playback and window settings (preset duration, shuffle, transitions, hard
cuts, sensitivities, mesh size, FPS, and fullscreen) can be read and changed at
runtime.

Changes are written to a dedicated runtime override layer that has **higher
precedence than command-line flags and the user configuration**. A value set
through this API therefore takes effect immediately and wins over everything,
including launch flags. Overrides are held in memory only and are not persisted
to the user configuration file; clear them to hand control back to the lower
layers. Updates are queued and applied on the render thread.

### Settings

| Name | Type and range | Description |
| --- | --- | --- |
| `displayDuration` | Number `0`–`86400` | Seconds each preset is shown before auto-advancing. `0` disables auto-advance. |
| `shuffleEnabled` | Boolean | Pick the next preset at random when auto-advancing. |
| `presetLocked` | Boolean | Lock the current preset so the playlist stops advancing. |
| `transitionDuration` | Number `0`–`60` | Soft-cut blend duration in seconds. |
| `hardCutsEnabled` | Boolean | Enable beat-triggered hard cuts to the next preset. |
| `hardCutDuration` | Number `0`–`86400` | Minimum seconds between hard cuts. |
| `hardCutSensitivity` | Number `0`–`5` | Beat sensitivity for hard cuts. |
| `beatSensitivity` | Number `0`–`2` | Overall beat detection sensitivity. |
| `aspectCorrectionEnabled` | Boolean | Correct preset aspect ratio to the window. |
| `meshX` | Integer `1`–`512` | Per-pixel mesh width. |
| `meshY` | Integer `1`–`512` | Per-pixel mesh height. |
| `fps` | Integer `1`–`1000` | Target FPS used for projectM timing. The window frame limiter may still require a restart. |
| `fullscreen` | Boolean | Switch the window between fullscreen and windowed mode. |

### Read the schema

```http
GET /api/v1/config/schema
```

Returns the settable options with their type, range, and description:

```json
{
  "ok": true,
  "options": [
    {
      "name": "displayDuration",
      "key": "projectM.displayDuration",
      "type": "number",
      "min": 0,
      "max": 86400,
      "description": "Seconds each preset is shown before auto-advancing. 0 disables auto-advance."
    }
  ]
}
```

### Read current values

```http
GET /api/v1/config
```

Each setting reports its effective value and the layer it comes from
(`runtime`, `commandLine`, `user`, or `default`):

```json
{
  "ok": true,
  "config": {
    "displayDuration": { "value": 30, "source": "default" },
    "shuffleEnabled": { "value": false, "source": "runtime" }
  }
}
```

```sh
curl -s http://127.0.0.1:8080/api/v1/config
```

### Update settings

```http
PATCH /api/v1/config
Content-Type: application/json
```

Provide one or more settings. The request is atomic: if any field is unknown,
the wrong type, or out of range, the whole request is rejected with
`400 Bad Request` and nothing is applied.

```json
{
  "displayDuration": 0,
  "shuffleEnabled": false,
  "presetLocked": true
}
```

```sh
curl -s -X PATCH \
  -H 'Content-Type: application/json' \
  -d '{"displayDuration":0,"shuffleEnabled":false}' \
  http://127.0.0.1:8080/api/v1/config
```

A successful request returns `202 Accepted` with the number of settings queued:

```json
{
  "ok": true,
  "queued": true,
  "updated": 2
}
```

To pin the current preset indefinitely, disable auto-advance and lock it:

```sh
curl -s -X PATCH \
  -H 'Content-Type: application/json' \
  -d '{"displayDuration":0,"presetLocked":true}' \
  http://127.0.0.1:8080/api/v1/config
```

### Clear overrides

Removing an override reverts the setting to the next layer down (command-line,
user, or default value).

Clear a single setting:

```http
DELETE /api/v1/config/{name}
```

```sh
curl -s -X DELETE http://127.0.0.1:8080/api/v1/config/shuffleEnabled
```

An unknown setting name returns `404 Not Found`.

Clear every override at once:

```http
DELETE /api/v1/config
```

```json
{
  "ok": true,
  "queued": true,
  "cleared": 2
}
```

## Post-processing shader chain

When visual post-processing is enabled (`--enableVisualPostProcessing`), projectM
is rendered into an offscreen buffer and then run through the built-in transform
(mirror/rotation/zoom, see [Visual controls](#visual-controls)) followed by a
configurable chain of **uploaded fragment shaders**. Each pass reads the previous
pass's output, an optional named [texture](#in-memory-textures), and standard
uniforms, and writes the result to the next pass; the last pass renders to the
screen.

### Writing a shader

Upload the body of a fragment shader that defines:

```glsl
vec4 effect(vec2 uv)
{
    vec4 src = texture(uInput, uv);   // previous pass / projectM output
    return src;
}
```

The following are provided (declare extra `uniform float <name>;` for custom
parameters):

| Uniform | Meaning |
| --- | --- |
| `sampler2D uInput` | Output of the previous pass (the image so far) |
| `sampler2D uTexture` | The pass's named texture, or 1×1 black if none |
| `vec2 uResolution` | Output size in pixels |
| `float uTime` | Seconds since start |

Do **not** include a `#version` line or `main()` — those are added for you.

### Upload / list / remove shaders

```http
PUT    /api/v1/shaders/{name}     Content-Type: text/plain   (body = GLSL source)
GET    /api/v1/shaders
DELETE /api/v1/shaders/{name}
```

Names are 1–128 characters of `[A-Za-z0-9_.-]`. Uploading returns `202`;
compilation happens on the render thread when the shader is used in the chain.
`GET /api/v1/shaders` reports each shader's compile status:

```json
{
  "ok": true,
  "shaders": [
    { "name": "grayscale", "compiled": true },
    { "name": "broken", "compiled": false, "error": "Shader compilation failed: ..." }
  ]
}
```

A shader that fails to compile is skipped (the rest of the chain still runs).

### Configure the chain

```http
GET /api/v1/postprocess
PUT /api/v1/postprocess
Content-Type: application/json
```

The chain is an ordered array of passes. Each pass names a shader, and may bind a
texture (as `uTexture`) and set custom float uniforms:

```json
{
  "chain": [
    { "shader": "grayscale" },
    { "shader": "tint", "params": { "amount": 0.7 } },
    { "shader": "albumCorner", "texture": "cover" }
  ]
}
```

A pass's `texture` may name an [in-memory texture](#in-memory-textures) or a
**deck texture** — `deck1`, `deck2`, … for the live output of decks 1..N-1 (see
[Decks](#decks)). Deck textures bind directly to the pass's `uTexture` sampler,
so a shader composites decks the same way it composites any other texture.

`GET` returns the current chain plus `available` (whether post-processing is
active). Setting an empty `chain` disables all user passes.

Example — a grayscale effect:

```sh
curl -s -X PUT --data-binary \
  'vec4 effect(vec2 uv){vec4 c=texture(uInput,uv);float g=dot(c.rgb,vec3(0.3,0.59,0.11));return vec4(vec3(g),1.0);}' \
  http://127.0.0.1:8080/api/v1/shaders/grayscale

curl -s -X PUT -H 'Content-Type: application/json' \
  -d '{"chain":[{"shader":"grayscale"}]}' \
  http://127.0.0.1:8080/api/v1/postprocess
```

### Tune parameters live

```http
PATCH /api/v1/postprocess/params
Content-Type: application/json
```

Updates the float `params` of one or more passes **without recompiling** the
chain — suitable for real-time knobs. Each entry targets a pass by its `index`
in the chain and merges the given params (unlisted params are left unchanged):

```json
{
  "passes": [
    { "index": 0, "params": { "amount": 0.8 } }
  ]
}
```

An out-of-range `index` returns `400 Bad Request`. Changing params via
`PUT /api/v1/postprocess` also works but recompiles the chain, so prefer this
endpoint for live tuning.

```sh
curl -s -X PATCH -H 'Content-Type: application/json' \
  -d '{"passes":[{"index":0,"params":{"amount":0.95}}]}' \
  http://127.0.0.1:8080/api/v1/postprocess/params
```

### Compositing decks

With two or more [decks](#decks) running, deck 0 is the chain base (`uInput`)
and each extra deck is available as the named texture `deck1`, `deck2`, …. A
single-pass shader can crossfade deck 0 with deck 1:

```glsl
// crossfade: blend the base output (uInput) with deck 1 (uTexture).
uniform float uMix; // 0 = only deck 0, 1 = only deck 1
vec4 effect(vec2 uv)
{
    return mix(texture(uInput, uv), texture(uTexture, uv), uMix);
}
```

Start the app with two decks and post-processing, then wire it up:

```sh
projectMSDL --decks 2 --enableVisualPostProcessing

# Upload the shader
curl -s -X PUT --data-binary \
  'uniform float uMix; vec4 effect(vec2 uv){return mix(texture(uInput,uv),texture(uTexture,uv),uMix);}' \
  http://127.0.0.1:8080/api/v1/shaders/crossfade

# Bind deck 1 as the pass texture and set the blend
curl -s -X PUT -H 'Content-Type: application/json' \
  -d '{"chain":[{"shader":"crossfade","texture":"deck1","params":{"uMix":0.5}}]}' \
  http://127.0.0.1:8080/api/v1/postprocess

# Sweep the blend live (no recompile)
curl -s -X PATCH -H 'Content-Type: application/json' \
  -d '{"passes":[{"index":0,"params":{"uMix":0.8}}]}' \
  http://127.0.0.1:8080/api/v1/postprocess/params
```

Load a different preset onto deck 1 to change what is blended in:

```sh
curl -s -X POST -H 'Content-Type: application/json' -d '{}' \
  'http://127.0.0.1:8080/api/v1/playback/random?deck=1'
```

### Render once, view twice (single-deck mirror)

Two decks make sense for two **different** presets. If you instead want the *same*
preset shown twice — e.g. one copy rising, a mirrored copy descending — do **not**
use a second deck: two independent projectM instances each run their own audio
auto-gain and frame timing, so identical audio yields subtly different visuals,
and you pay for a second full render. Render **one** deck and derive the mirror in
the shader, sampling the base with a flipped Y. It is guaranteed identical and
half the cost:

```glsl
// vertical-mirror: overlay the base with a top-to-bottom flipped copy of itself.
vec4 effect(vec2 uv)
{
    vec4 base     = texture(uInput, uv);
    vec4 mirrored = texture(uInput, vec2(uv.x, 1.0 - uv.y));
    return max(base, mirrored);
}
```

```sh
projectMSDL --decks 1 --enableVisualPostProcessing

curl -s -X PUT --data-binary \
  'vec4 effect(vec2 uv){return max(texture(uInput,uv),texture(uInput,vec2(uv.x,1.0-uv.y)));}' \
  http://127.0.0.1:8080/api/v1/shaders/vertical-mirror

curl -s -X PUT -H 'Content-Type: application/json' \
  -d '{"chain":[{"shader":"vertical-mirror"}]}' \
  http://127.0.0.1:8080/api/v1/postprocess
```

Rule of thumb: **two decks for two different presets; one deck plus a shader view
when both layers are the same source.**

> [!NOTE]
> The chain only renders when post-processing is enabled at launch
> (`--enableVisualPostProcessing`). Shaders can be uploaded either way, but the
> chain has no effect until it is active.
