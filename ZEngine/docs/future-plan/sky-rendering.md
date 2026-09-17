# Sky Rendering System

**Relates to:** render-graph integration, render-graph redesign, GPU allocator rearchitecture, per-frame upload heap, scene-serialization.md, editor-undo-redo.md
**Legacy status:** the pre-SkyEnvironment `SkyboxPass` was retired in #827.
**Status:** Partial renderer implementation plus production design. The
implementation-status correction below takes precedence over later target-state
language.
**Scope:** scene-owned sky configuration, HDRI and analytic-sky presentation, atmosphere rendering, and the environment lighting resources consumed by the renderer.

---

> ## Current implementation correction
>
> The renderer already owns a live `Scenes::SkyConfig` in `RenderScene`, a
> revision counter, fallback cubemap/IBL resources, atmosphere/HDRI bake and
> presentation callbacks, capability fallback, and the persistent
> environment-lighting memory gate. `GraphicRenderer` registers those callbacks
> at initialization and declares their resources through the current render-graph
> callback contract.
>
> It does **not** yet have the document/EditorSession boundary specified below:
> `YAMLSceneSerializer` does not serialize the scene sky, entity identity is not
> durable across a current load, and `RenderFrameState` still borrows a mutable
> `RenderScene*` even though it carries copied sky values. The scene serializer,
> transaction system, and immutable render snapshot described in the editor docs
> are prerequisites for calling scene-owned sky authoring production ready.
>
> `skyDefaults` below is a proposed project-template shape, not a currently
> parsed project configuration section. The implemented generated-project keys
> are `rendering.environment_lighting_quality` and
> `rendering.environment_lighting_budget_mb`; their value is renderer/project
> policy, not serialized scene data. Do not edit generated `project.json` merely
> to change the 384 MiB default.

## 1. Goals and boundaries

The sky system has two distinct responsibilities:

1. Present background radiance and atmospheric effects for a particular render view.
2. Produce stable image-based-lighting (IBL) inputs for the scene.

They share inputs, but they do not have the same lifetime or update frequency. Keeping them separate is essential for multiple viewports, asynchronous baking, and a renderer that never binds invalid textures.

| Visual mode | Background source | Environment lighting |
|---|---|---|
| Atmosphere | Analytic atmosphere | Captured sky-radiance cubemap, then baked IBL |
| HDRI | Cooked HDR cubemap | The same cooked cubemap, then baked IBL |
| SkySphere | Analytic gradient and optional sun disc | Engine-provided neutral fallback environment |

SkySphere intentionally does not generate a physically accurate environment capture. It must nevertheless leave lighting with valid fallback descriptors. No mode may produce an unbound descriptor, a null texture, or a black editor viewport solely because an asset is loading or failed.

Version 1 covers the three modes above, a robust IBL lifecycle, and correct HDR composition. Volumetric clouds, weather, stars, moon phases, local reflection probes, sky blending, and atmosphere volume shadows are follow-up features. The design leaves extension points for them without making them prerequisites.

### 1.1 Post-version-1 extension points

Version 1 deliberately has one global environment per scene. Future work may add:

- separate background, diffuse-IBL, specular-IBL, reflection-capture, and ray-visibility controls;
- cross-fading between two ready snapshots for HDRI changes, weather, or time-of-day transitions, with an explicit doubled-memory budget;
- local reflection probes, environment volumes, and baked indirect-lighting integration for interiors;
- clouds, fog volumes, precipitation, stars, moon, and planet/terrain shadows layered into both presentation and relevant lighting captures;
- stereo/XR multiview and high-resolution offline/cinematic capture policies.

These extensions consume the same immutable snapshot, RenderView, graph-declaration, and timeline-retirement contracts defined below. They must not introduce special global state or bypass the environment lifetime model.

---

## 2. Ownership, configuration, and units

Sky configuration is owned by the scene or render world. A project may supply defaults, but it must not be the sole owner: different scenes can legitimately use different skies. The serialized scene representation contains a stable environment asset reference, never a raw retained character pointer or an absolute working-space path.

**Target state:** project defaults are a template for a newly created scene
only. The proposed optional `project.json` section is shown below; it is not
currently parsed. An existing scene must retain the `SkyConfig` stored in its
scene file once serialization lands:

~~~json
"skyDefaults": {
  "mode": "atmosphere",
  "environmentMap": "optional-asset-uuid",
  "environmentIntensity": 1.0,
  "environmentTint": [1.0, 1.0, 1.0, 1.0],
  "environmentYawRadians": 0.0
}
~~~

The nested atmosphere and SkySphere settings begin with the engine's validated defaults. A project must not store generated environment-cache paths, live Vulkan data, or editor viewport preview state in this section.

Sky serialization is not implemented in the current YAML scene serializer. The
target schema must introduce a versioned migration policy rather than silently
discard or replace previously authored data.

The exact engine type follows the asset-manager API, but the conceptual configuration is:

~~~cpp
enum class SkyMode : uint8_t
{
    Atmosphere,
    HDRI,
    SkySphere,
};

struct SkyConfig
{
    SkyMode        Mode = SkyMode::Atmosphere;
    AssetReference EnvironmentMap = {}; // Stable asset UUID/handle, not cstring.

    // Shared artistic controls. These affect the visual and IBL source consistently.
    float          EnvironmentIntensity = 1.0f;
    Vec4f          EnvironmentTint = {1.0f, 1.0f, 1.0f, 1.0f};
    float          EnvironmentYawRadians = 0.0f;
    EntityID       PrimaryCelestialLight = {}; // Optional runtime resolution of an authored UUID reference; invalid means no direct sun.

    AtmosphereSettings Atmosphere = {};
    SkySphereSettings  Sphere = {};
};
~~~

The sky system distinguishes two change domains:

- The environment bake key contains source content, atmosphere physical parameters, resolved sun state, shader version, and quality tier. A change creates a new SkyEnvironment revision.
- The presentation state contains dynamic values that can be applied at sampling time, such as HDRI yaw, a linear intensity multiplier, tint, and SkySphere colours. It combines SkyConfig with RenderView camera/post-processing inputs and is refreshed per frame without rebuilding cubemaps.

HDRI yaw transforms the lookup direction for both background and IBL sampling; tint and intensity multiply both paths in linear colour. These operations are rotationally/equivariantly valid and do not require a rebake. Display exposure remains a camera/post-processing control. Asset completion and hot reload increment the bake key only when the completed asset is still the requested asset. This makes stale asynchronous completions harmless.

For the analytic atmosphere, the editor exposes all serialized authoring fields in Planet, Molecular, Aerosols, Ozone, Sun, and Ground groups. Planet placement and world-unit scale are camera-local inputs: changing either refreshes the per-view atmosphere path without regenerating planet-centred resources. Rayleigh, Mie, and ozone properties regenerate the static LUTs as well as the source and IBL resources. The resolved sun and its radiometric settings, ground albedo, and ground ambient irradiance regenerate the source-radiance cubemap and IBL while reusing compatible static LUTs.

Ground albedo and ambient irradiance are never presentation-only overrides. The implicit Lambertian ground is captured into the atmosphere source cubemap, so a ground edit remains on the pending revision until its source, diffuse IBL, and specular IBL are complete. Per-view sky and aerial passes read the ground values from that same published snapshot. This avoids a new visible lower hemisphere being combined with old environment lighting.

### 2.1 Configuration validity and migration

SkyConfig is data, not a live GPU resource. Game/editor code submits an immutable copy to the renderer; the renderer validates it before it becomes an environment revision.

- All numeric values must be finite. Radii and scale heights are positive, atmosphere radius exceeds planet radius, scattering/absorption values are non-negative, and anisotropy is constrained away from its singular limits.
- A missing primary celestial light is a documented no-sun state: there is no sun disc or direct-sun contribution. It does not select an arbitrary scene light.
- An unknown mode, an unresolved asset reference, an invalid HDR image, or an unsupported device feature selects the safe fallback environment and produces one actionable diagnostic.
- Repeated edits within a frame coalesce to the newest revision. Undo/redo uses the same revision path and does not mutate an already-published GPU snapshot.

Newly saved scenes write only the typed mode and stable asset reference. Generated cooked-cache paths are never serialized as scene identity.

### 2.2 Coordinate convention

Atmosphere shaders use kilometres internally. Scattering coefficients are uploaded in inverse kilometres; authoring data expressed in inverse metres is converted once during upload. Planet radii, scale heights, ozone-layer values, and camera altitude use kilometres.

The scene supplies:

- a planet centre in world space;
- the scene world-unit-to-metre scale;
- a camera position relative to that centre, converted to kilometres with a numerically stable large-world/floating-origin path.

The default one-world-unit-per-metre configuration places the planet centre at `(0, -6,360,000, 0)`. World origin is consequently sea level, so ordinary editor scenes are never accidentally interpreted as being inside the planet.

Rays that reach the analytic planet boundary are closed against the scene's Lambertian atmosphere-ground albedo and diffuse fill irradiance. This prevents a no-terrain editor viewport from exposing a black lower hemisphere. A terrain renderer remains responsible for replacing that implicit surface with real scene geometry.

Every atmosphere RenderView is classified from that same camera-relative kilometre conversion before graph declaration: `BelowGround` (at or inside the planet radius), `InsideAtmosphere`, `OutsideAtmosphere`, or `Invalid`. Only the latter two run per-view sky and aerial integration. A below-ground or invalid editor view uses a fixed blue environment-background fallback, so opaque content, grid, and gizmos remain usable without sending an invalid ray to the atmosphere kernels. The fallback does not sample the baked cubemap: its appearance is therefore independent of camera position and orientation. This is an editor fallback, not physical underground rendering: gameplay terrain and underground materials remain responsible for occlusion. The editor may opt into a spherical ground constraint derived from the active atmosphere's configured centre and radius; it is disabled by default and never applies to game cameras or SkySphere/HDRI modes.

The public contract also defines coordinate handedness, cubemap face orientation, the sign of the light direction, and whether a directional light points toward or away from its source. The same primary celestial light drives the sun disc, atmosphere, direct lighting, and sun shadows. Selecting the first active directional light is not deterministic enough for this role. Solar angular radius and illuminance use documented physical units; any artistic multiplier is named and applied consistently to background radiance and environment lighting.

---

## 3. Resource model and lifetime

The implementation separates persistent environment resources from transient per-view resources.

~~~text
Scene SkyConfig
       |
       v
SkyEnvironment revision
       |---- persistent atmosphere LUTs
       |---- persistent source-radiance cubemap
       |---- persistent irradiance + specular IBL cubemaps
       |
       +---- SkyEnvironmentSnapshot (fallback | baking | ready | failed)

RenderView
       |---- transient sky-view LUT
       |---- transient aerial-perspective LUT
       +---- final sky composition
~~~

### 3.1 SkyEnvironment

A SkyEnvironment belongs to one scene/render world. It owns the persistent source radiance, static atmosphere LUTs where applicable, and a published IBL snapshot. The BRDF integration LUT is engine-global, keyed by shader and quality version, because it is independent of the scene sky.

Environment baking writes a next snapshot. The renderer continues reading the previous ready snapshot, or the engine fallback snapshot, until the next one is complete. Publication happens only after the GPU work has completed according to its timeline value. Replaced snapshots remain pinned until all submitted users have retired; only then can their textures be freed.

The state machine is:

~~~text
fallback -> baking(revision N) -> ready(revision N)
                       |
                       +--> failed(revision N), keep previous ready or fallback
~~~

A boolean dirty flag is not sufficient: it can lose a newer revision while an older bake is in flight.

### 3.2 SkyViewResources

Sky-view and aerial-perspective LUTs are per RenderView because camera altitude, near/far range, and projection differ between the editor, game view, scene capture, reflection capture, and multiple viewports. They are transient graph resources and are recreated or resized with the render view, not with the swapchain globally.

Static transmittance and multiscattering LUTs may be shared by views using the same environment revision. They must not be overwritten while a prior frame still samples them.

### 3.3 Frame and thread ownership

Only the render thread creates, updates, publishes, pins, or retires SkyEnvironment GPU resources. Editor/game threads send immutable configuration revisions through the renderer mailbox. Import workers may decode and cook assets, but completion is returned as a revision-tagged message; a worker never writes Vulkan descriptors or environment state directly.

At frame start, the renderer acquires one ready snapshot and pins it for that frame. Every lighting and sky pass in that frame binds that same snapshot. The pin is released only after the frame submission receives its timeline value. This prevents mixed old/new IBL bindings within one frame and prevents a hot-reloaded resource from being freed while a command buffer still references it.

Descriptor writes are scoped to a reusable frame slot only after that slot's prior submission has retired. Publishing a new snapshot updates future frame slots; it never overwrites descriptors still visible to an in-flight command buffer.

An obsolete bake that has not been submitted may be cancelled. A submitted obsolete bake is allowed to finish, but its result is discarded unless its revision is still current. The bake scheduler has a bounded queue and a configurable time budget; it never accumulates one expensive full bake for every slider edit. Dynamic primary-celestial updates are additionally coalesced: the current implementation accepts at most one new atmosphere source bake per eight observed sky revisions, retaining the newest direction for the next accepted request. An availability change or a rotation of at least five degrees bypasses the budget, so a deliberate editor edit responds immediately. Physical atmosphere, ground, quality, and mode changes remain immediate because they are not animation-budgeted edits.

Shutdown first unregisters asset-completion listeners and stops accepting bake work. It then retires pinned snapshots through the normal device-timeline path before the device allocator is destroyed. Worker completion messages received during shutdown are discarded without dereferencing scene or renderer state.

---

## 4. Render-graph contract

The render graph owns frame-pass instantiation, resource versions, barriers, and scheduling. SkySystem owns configuration, environment revisions, and GPU-resource snapshots. It does not mutate graph internals and does not call Compile during a frame.

All sky callbacks are registered as persistent callbacks at renderer initialization. On each graph frame, their Register method either declares the exact resources it uses or returns false to omit itself. Mode switching is therefore a normal per-frame declaration change, not a graph rebuild driven by registration order.

Persistent environment textures are imported into the graph with their established layout. A bake callback declares writes to the next snapshot; lighting declares reads from the exact published snapshot. The graph is responsible for the resulting image layout transitions, queue ownership transfers, and synchronization. A callback having no framebuffer attachment does not exempt it from resource declarations.

EnvironmentBake is a logical subgraph, not one opaque callback containing every dispatch. Each producer/consumer boundary is a graph pass: static atmosphere LUT generation, atmosphere source-radiance capture, HDRI source preparation when needed, BRDF generation, diffuse convolution, and specular prefilter. Sky-view generation, aerial in-scattering, aerial transmittance, and final composition are likewise separate passes. This lets the graph derive barriers between stages; only dispatches with no graph-visible dependency remain within one callback.

The last writer exports each next-snapshot resource in its intended read layout. Publication records that layout with the snapshot, so the next frame imports a known state rather than guessing from the previous mode. A streamed HDR source is imported through the graph's streaming-acquire path before a source-preparation or IBL pass reads it; the bake scheduler never bypasses the upload ownership transfer.

Environment-bake passes request asynchronous compute only when the graph can schedule it safely for the device queue topology. On a device without useful asynchronous compute, they run in graphics-queue order with identical resource declarations and correctness. Timestamps are reported only where the device exposes a comparable clock domain.

### 4.1 Frame topology

The logical frame flow is:

~~~text
Depth / G-buffer
        |
        +--> optional EnvironmentBake subgraph -> next persistent snapshot
        |
published IBL snapshot --> Deferred Lighting -> HDR opaque scene
                                           |
per-view sky LUTs --------------------------+
                                           v
                           Sky composite -> HDR scene with atmosphere
                                           |
                           Forward transparencies and overlays
                                           |
                           Exposure / bloom / tone mapping
~~~

An environment bake may overlap the frame, but lighting reads only a completed snapshot. The initial frame reads an engine fallback snapshot rather than stalling.

For atmosphere mode, the bake path has a required producer chain:

~~~text
transmittance + multiscattering LUTs
                 -> atmosphere source-radiance cubemap
                 -> irradiance and specular prefilter
                 -> published IBL snapshot
~~~

A sky-view LUT is camera dependent and is not an IBL source. IBL always reads the explicitly produced source-radiance cubemap.

### 4.2 Composition contract

Sky composition consumes the depth result and opaque HDR scene colour. For a geometry pixel at camera distance d:

~~~text
outRadiance = opaqueRadiance * transmittance(d) + inScatteredRadiance(d)
~~~

For a background pixel, it writes sky radiance directly. The pass defines the depth-clear test for both standard and reverse-Z, reconstructs position/distance using the active projection convention, and keeps sun/background visibility correctly occluded by geometry.

Composition writes a distinct HDR resource version, for example HDRSceneComposited. It must not sample and render to the same HDR image unless an explicitly supported attachment-feedback-loop design is adopted. A separate output is the portable baseline.

If opaque rendering uses multisampled depth, composition reads the resolved depth that matches the opaque HDR scene. HDR scene alpha has one renderer-wide meaning and is never repurposed to store atmospheric transmittance.

Forward transparent materials render after the background is present. They use the same atmospheric transmittance/in-scattering convention when atmospheric perspective is enabled; otherwise transparent objects would remain visually detached from the opaque scene.

### 4.3 Render-view sizing and temporal stability

The render-view extent, not the swapchain extent, sizes per-view sky resources. A zero-width or zero-height view declares no sky work and preserves the last valid viewport output. A viewport resize invalidates only that RenderView's transient LUTs and composition target; it never invalidates the shared source cubemap or IBL snapshot.

Fixed-resolution LUT quality tiers are permitted, but their sampling transform must be derived from the current view projection and aspect ratio. The final composite always matches the active render-view extent. If a future quality tier uses temporal accumulation, it defines a stable sampling sequence, reprojection validity, camera-cut reset, dynamic-resolution reset, and deterministic test mode. Temporal history is never reused across different views or sky revisions.

When temporal anti-aliasing is enabled, the sky integration supplies an explicit infinite-depth/background classification and a rotational motion-vector or reactive-mask policy. It prevents camera jitter, a newly visible sun disc, and HDRI rotation from producing ghost trails.

---

## 5. Descriptor and pipeline contract

Descriptor layouts are pipeline-owned and shader-reflection validated. This document defines stable logical resource names and types, not contradictory independent set-number tables. The shader source, reflection data, and C++ binding code for each pipeline must agree exactly.

The engine already reserves descriptor set 1 for the bindless texture array and global samplers. Sky-specific UBO, sampled-image, sampler, and storage-image declarations must not redefine that set or its bindings. Dedicated sky descriptor sets are allocated after every engine-reserved set, and reflection validation rejects a collision before a pipeline can bake.

| Logical group | Required resources | Lifetime |
|---|---|---|
| PerView | Camera UBO / dynamic offset | Per RenderView, per frame |
| AtmosphereParameters | Atmosphere UBO / dynamic offset | Per environment revision or frame |
| SkyInputs | Transmittance, multiscatter, sky-view, aerial LUTs, or source cubemap | Imported/transient as declared |
| SkyStorage | Storage views for the exact LUT/cubemap mip and layer being written | Bake or per-view compute |
| LightingIBL | Diffuse irradiance, specular environment, BRDF LUT | Published snapshot |

Every pipeline has a single reflected layout. No shader may reuse one binding for incompatible descriptor types within that layout. Required bindings are verified before commands are recorded; an invalid binding must produce a clear validation/error path, not a silent no-op.

Cubemaps are sampled through cube views. Compute writes use a compatible 2D-array storage view for the selected faces and mip level, then the result is sampled through the cube view. Layer and mip subresource ranges are declared explicitly to the graph.

### 5.1 PSO and binding lifetime

Each sky graphics or compute pipeline is a normal renderer PSO. Its immutable key contains the shader identity/interface, attachment formats, sample count, raster/depth/blend state, and static specialization values. It does not contain a SkyConfig revision, a texture handle, a dynamic UBO offset, a viewport extent, or a per-frame resource.

The PSO cache owns pipeline creation and invalidation. SkyEnvironment owns textures and data snapshots. Render-graph callbacks declare resource use and bind the chosen snapshot in Prepare. This separation avoids per-frame PSO creation while allowing any environment revision to reuse the same compatible pipeline.

Shader hot reload validates the reflected interface before descriptor replay. If the interface no longer matches the callback's required logical bindings, the pass is rebuilt or safely omitted while the fallback sky remains usable. A failed pipeline bake or missing descriptor is surfaced through diagnostics; it must not leave command recording in a silent no-op state.

The renderer foundation provides one graphics/compute descriptor binder for dynamic uniform buffers, sampled images, samplers, storage images, and storage buffers. It records bindings for shader-interface replay and requires complete descriptor bindings before dispatch. RenderGraph routes declared sampled and storage-image resources through this generic binder. Sky passes remain responsible for declaring their exact bindings and subresource ranges; this support alone does not schedule, allocate, or bake sky resources.

### 5.2 Code boundaries and allocation policy

SkySystem is a scene-environment coordinator. SkyEnvironment owns persistent resource snapshots and revision state. Individual graph callbacks own only pass declaration, descriptor preparation, and command recording. RenderGraph remains generic: it knows imported resources, transient resources, subresource ranges, and synchronization, but contains no sky-specific state or scheduling rules.

EnvironmentMapImporter owns source validation and cooked-artifact creation. The asset manager owns source identity and hot-reload notifications. GraphicRenderer selects a snapshot at frame start but does not decode assets or own the environment-resource lifetime.

Persistent texture allocations occur only for fallback initialization, a new environment revision, or a selected quality tier. Per-frame work uses the graph frame arena and existing per-frame descriptor storage; it must not introduce a general heap allocation in view updates, command recording, or descriptor binding.

The initial component boundary is:

~~~text
SkySystem / SkyEnvironment             configuration, revisions, snapshots
AtmosphereStaticLutPass                transmittance and multiscattering
SkySourceRadiancePass                  atmosphere capture or prepared HDRI source
EnvironmentIBLBakePass                 BRDF, diffuse irradiance, specular prefilter
SkyViewLutPass / AerialPerspectivePass per-RenderView lookup textures
SkyCompositePass                       depth-aware HDR scene composition
SkySpherePass                          analytic background mode
EnvironmentMapImporter                 cooked source artifact
~~~

---

## 6. SkySphere presentation

SkySphere is a fullscreen background pass. It reconstructs a view ray using the active projection convention, ignores camera translation, and writes linear scene radiance. The gradient uses the ray elevation; its optional sun disc uses a dot-product threshold against the primary celestial direction rather than an expensive inverse cosine.

The push-constant block contains four Vec4 values and four scalar values, for exactly 80 bytes. It has no trailing pad:

~~~cpp
struct SkySpherePush
{
    Vec4f HorizonColor;
    Vec4f ZenithColor;
    Vec4f GroundColor;
    Vec4f SunDirection;
    float SunDiscAngularRadiusRadians;
    float SunDiscIntensity;
    float HorizonSharpness;
    float ShowSunDisc;
};
static_assert(sizeof(SkySpherePush) == 80);
~~~

The implementation also checks the device push-constant limit during capability initialization. SkySphere presentation does not alter the fallback IBL snapshot used by lighting.

Selecting SkySphere immediately selects the engine fallback snapshot for lighting and retires any previously published non-fallback environment after its frame pins complete. It never queues asset decode, cubemap generation, or IBL baking. A directional light is optional: without one, the gradient remains valid and the sun disc is omitted. The unused `SunDirection.w` lane carries the far clip depth (`1` for standard depth, `0` for reverse-Z), preserving the 80-byte push-constant contract while allowing the fullscreen triangle to depth-test only where the depth buffer is clear.

---

## 7. Atmosphere rendering

The atmosphere implementation follows the Bruneton/Hillaire family of techniques, but its numerical and renderer contracts are explicit.

| Resource | Resolution | Preferred format | Update cadence | Ownership |
|---|---:|---|---|---|
| Transmittance | 256 x 64 | R11G11B10_UFLOAT when storage-supported; otherwise RGBA16F | Atmosphere revision | SkyEnvironment |
| Multiscattering | 32 x 32 | RGBA16F | Atmosphere revision | SkyEnvironment |
| Source-radiance cubemap | Quality tier, initially 512 per face | RGBA16F | Atmosphere or sun revision | SkyEnvironment |
| Sky-view LUT | Quality tier, initially 192 x 108 | RGBA16F | Per RenderView | Transient graph resource |
| Aerial in-scattering | Quality tier, initially 32 x 32 x 32 | RGBA16F | Per RenderView | Transient graph resource |
| Aerial transmittance | Quality tier, initially 32 x 32 x 32 | RGBA16F | Per RenderView | Transient graph resource |

All generated LUTs are GPU-written storage images. They are not uploaded through the staging ring; staging is reserved for external asset data. The graph declares storage writes and sampled reads so the required barriers are generated rather than hand-maintained inside unrelated passes.

At the initial 32 x 32 x 32 aerial resolution, an 8 x 8 x 4 kernel dispatches 4 x 4 x 8 workgroups. Implementations derive those counts with ceiling division from the selected quality tier rather than relying on hard-coded dimensions.

Sun changes invalidate the source-radiance cubemap and its IBL output. Static transmittance and multiscattering LUTs need not be regenerated for a direction-only sun change. A continuously animated day/night cycle uses a quality budget or progressive bake policy so it cannot hitch the editor.

The atmosphere UBO is std140-aligned and checked with size and offset assertions on the C++ side, plus reflection validation in the shader build. It includes only canonical-unit values, camera-relative planet coordinates, and the resolved primary celestial-light direction. `SunIlluminanceLux` remains a photometric authored value; the renderer maps 6 klux to one scene-radiance unit before baking, so the default 120-klux clear-sky noon sun maps to 20 units. This unit bridge is shared by atmosphere source capture and IBL, while display exposure remains a per-view post-processing control. Wavelength-independent Mie scattering and absorption authoring values are scalar, but are packed into their RGB UBO fields as Vec4f(x, x, x, 0).

### 7.1 Numerical and payload contract

The two aerial textures have an explicit payload: aerial in-scattering stores scene-linear RGB radiance and aerial transmittance stores RGB transmission. Alpha is unused or reserved with a documented value; it never ambiguously represents both radiance and coloured transmission. The initial two-texture representation costs little memory and avoids a colour-shifting scalar-alpha approximation. Any later packed representation requires matching HDR reference images before it replaces this baseline.

The static multiscattering LUT has one shared spectral contract. Its coordinates are `u = 0.5 * (muSun + 1)` and `v = altitude / (atmosphereRadius - planetRadius)`, where `muSun` is the local-up cosine toward the sun. A texel stores `Mms = integral(Lms(omega) d omega) / Lsun`: the RGB angular integral of higher-order incident radiance, normalized by direct solar radiance. `Mms` consequently has steradian units. It excludes the local scattering coefficient, local extinction, all phase normalization, presentation tint, and solar radiance. It is not itself outgoing sky radiance and must never be added directly to a cubemap or a view LUT.

The initial implementation uses a bounded isotropic-shell closure rather than claiming a full iterative solve. At the LUT sample point it computes the spectral scattering and extinction coefficients `sigmaS` and `sigmaT`, the direct-light scattering fraction `f = clamp((sigmaS / sigmaT) * (1 - TSun), 0, 1)`, and the retained-light probability `q = min(0.5 * f, 0.95)`. It stores `Mms = 4pi * f * q / (1 - q)`. The `0.5` represents the deliberately documented isotropic-shell return fraction; the `0.95` cap bounds the geometric series near opaque conditions. Because `sigmaS / sigmaT` is RGB, Rayleigh wavelength dependence is retained through both baking and consumption.

Every ray integrator uses the same source terms at each sample, in kilometres:

~~~text
singleSource = (betaRayleigh * rhoRayleigh * RayleighPhase
              + betaMie * rhoMie * MiePhase) * TSun * Lsun
multipleSource = sigmaS * (Mms / (4pi)) * Lsun
~~~

`1 / (4pi)` is applied exactly once in `multipleSource`, as the isotropic phase normalization. The source-radiance cubemap, sky-view LUT, and aerial-perspective LUT call the same shader helper for this integration. The cubemap capture point is sea level (`0 km` above the planet radius) and uses the same implicit Lambertian ground closure as a view ray; it is therefore a true scene-linear radiance capture rather than a coefficient lookup. A future iterative Bruneton/Hillaire solve may replace the LUT producer only if it preserves this payload and consumer equation, or versions both together.

Regression/reference checks for this contract include: zero scattering produces a zero `Mms` and zero multiple-scattering source; an isolated red/green/blue scattering coefficient produces an isolated matching source channel; and multiplying a stored `4pi` angular integral by the consumer's `1 / (4pi)` recovers the unnormalized isotropic source. Shader compilation covers the common helper from all three ray-integration entry points. HDR reference captures cover noon, sunset, below-horizon sun, elevated camera, and the ground boundary before numerical changes are accepted.

All atmosphere shader paths handle zero-length rays, horizon tangents, cameras below the ground radius, cameras above the atmosphere radius, and sun directions below the horizon without generating NaN or infinity. Inputs are validated on the CPU, but shaders still guard divisions, square roots, phase-function denominators, and exponential ranges. Sun angular radius is converted to radians before upload.

The captured source-radiance cubemap has a complete mip chain. Prefiltering selects source LOD from sample solid angle/PDF and source texel solid angle, avoiding rough-surface aliasing and fireflies. If hardware cannot linearly filter or generate the required HDR mip chain, the capability service chooses a shader downsample path or disables the affected quality tier.

LUT samplers use normalized coordinates and linear filtering. The sky-view LUT is an azimuth/elevation map, so its azimuth axis wraps and its elevation axis clamps; all other LUT axes clamp to edge. Cubemap samplers use the engine's canonical face orientation, clamp-to-edge addressing, and linear mip filtering. None of the radiance, transmittance, or IBL resources use an sRGB image view.

---

## 8. HDRI asset path

HDRI import uses the existing environment-map asset pipeline:

~~~text
authored .hdr
     -> importer
     -> cooked project cache artifact (.zenvmap or its successor)
     -> AssetReference in scene
     -> asynchronous texture upload
     -> SkyEnvironment source-radiance cubemap
~~~

Version 1 supports HDR input and extends the current importer that cooks it into a cubemap cache artifact. It must not also introduce a second runtime raw-equirectangular conversion pipeline. A future GPU-based cooker can replace the conversion implementation behind the same asset contract.

Radiance `.hdr` and flat, single-part `.exr` input are supported. HDR uses stb_image and EXR uses TinyEXR; both decode to the same linear RGBA32F conversion path. Deep, multipart, and other EXR workflows that cannot be represented as one flat image are intentionally not accepted as environment sources.

The current `.zenvmap` artifact is version 2. Its fixed header records source hash, importer version, RGBA32F payload contract, linear-scene colour, renderer-canonical cubemap orientation, exposure, face dimensions/layers, and the full GPU-generated mip policy. The runtime validates that complete contract and the exact payload length before it allocates or uploads; a header whose source hash does not match the registry is stale and is never used.

Raw HDR equirectangular images are never decoded on the render thread or retained in resident VRAM for backdrop rendering. Version 2 uses an RGBA32F base-level cubemap payload (up to 1024 pixels per face) and generates the required full mip chain on the GPU before IBL convolution. A future half-precision or compressed artifact must receive a new artifact version and preserve the same validation and orientation contract. Heap pressure may request eviction or a lower already-cooked tier; it must not silently downscale authored assets at runtime.

Cooked environment artifacts are derived cache data, not source content-browser items. The source asset remains the user-visible item and is the only identity serialized by the scene. Cache eviction or regeneration therefore never changes content-browser structure or source-control state.

If an HDRI is pending or fails, the system retains the last ready snapshot or uses the engine fallback environment and visibly reports the failure once in editor diagnostics. It does not replace the viewport with a solid black backdrop.

HDRI rotation/orientation, tint, and lighting intensity are common source-radiance inputs. Display exposure is a camera/post-processing control and must not accidentally make background brightness diverge from lighting brightness.

### 8.1 Import validation and colour contract

The importer accepts Radiance `.hdr` and flat, single-part `.exr`. It validates an equirectangular 2:1 source aspect ratio, finite non-negative RGBA pixels, width divisible by four, and a maximum 1024-pixel cubemap face before allocating conversion buffers. HDR source pixels are interpreted as linear scene radiance, never as sRGB. Invalid NaN/infinite or negative radiance fails import with a diagnostic.

The cooker writes atomically and preserves the prior valid artifact if recooking fails. It converts directly into the established six-face canonical order (covered against the prior vertical-cross conversion), records the complete GPU-generated mip policy, and stores the source/import hash used for stale-artifact detection. The runtime uses only completed artifacts and never samples a partially written cache file. Registry source hash and artifact readiness are part of the immutable HDRI bake key, so a reimport with the same scene UUID invalidates pending or active work while an unchanged unavailable artifact produces no retry loop.

---

## 9. Image-based lighting

Each published SkyEnvironment snapshot contains:

| Resource | Initial quality | Format | Notes |
|---|---:|---|---|
| Diffuse irradiance cubemap | 32 per face | RGBA16F | Cosine-convolved source radiance |
| Specular environment cubemap | 128 per face, full mip chain | RGBA16F | GGX-prefiltered; roughness-to-LOD convention is documented |
| BRDF integration LUT | 512 x 512 | RG16F | Engine-global; versioned by shader and quality |

The specular map has a full mip chain through 1 x 1. Roughness 0 and 1 map to documented LOD limits, and all cubemap filtering follows the same face orientation and seam policy as source capture.

IBL rebuild triggers are:

- a completed HDRI asset revision;
- entering HDRI or atmosphere mode without a matching ready snapshot;
- an atmosphere physical-parameter change;
- a resolved primary-sun change that changes atmosphere source radiance;
- a deliberate IBL quality-tier or shader-version change.

Camera movement, SkySphere-only changes, HDRI rotation, linear tint, linear intensity, and display exposure do not invalidate shared IBL. The source, diffuse, and specular maps are baked through configurable sample-count tiers with GPU timestamps. Expensive prefilter work may be progressive or amortized, but partial results are never published as a supposedly complete snapshot.

### 9.1 Lighting convention

Source radiance and IBL are stored in absolute linear scene units. If the renderer uses pre-exposure, the per-frame lighting and sky-composition shaders apply the same exposure transform at use time; an exposure change never forces an IBL rebake. Diffuse irradiance, specular radiance, the BRDF LUT, normal orientation, roughness-to-LOD mapping, and ambient/specular occlusion use one documented deferred-lighting convention.

The fallback snapshot contains valid diffuse, specular, and BRDF textures with deterministic neutral values. It is created before the first scene can render and remains available during device-supported mode changes, asset failures, shader rebuilds, and memory-pressure eviction. A fallback is a valid lighting state, not a null-handle special case.

### 9.2 Quality policy

Sky quality is renderer/platform policy rather than serialized artistic scene data. A tier specifies source-cubemap face resolution and mip policy, sky-view and aerial resolutions, IBL sample budgets, and optional format fallback. The active tier is part of the environment resource key.

A quality change creates a new environment revision and follows the same next-snapshot publication path as an asset change. It never resizes a published cubemap or changes the number of descriptors in place. The editor may preview a lower tier while authoring, but bake completion, memory use, and diagnostics always report the selected tier explicitly.

The HDRI IBL implementation reads the project-level `rendering.environment_lighting_quality` key. It accepts `low`, `standard` (the default), and `high`; scene files do not serialize the value. The initial budgets are:

| Tier | Diffuse cube | Diffuse samples | Specular cube | Specular samples |
|---|---:|---:|---:|---:|
| Low | 16 per face | 16 | 64 per face, full mip chain | 64 |
| Standard | 32 per face | 32 | 128 per face, full mip chain | 128 |
| High | 64 per face | 64 | 256 per face, full mip chain | 256 |

Persistent environment texture memory is independently capped by the project-level `rendering.environment_lighting_budget_mb` key. It accepts a positive integer number of MiB and defaults to 384 MiB when the key is missing or invalid.

---

## 10. HDR, formats, and memory budget

The renderer must provide a floating-point linear scene-colour target before this system is enabled. Lighting, sky composition, bloom, and exposure operate in linear HDR; tone mapping and output-gamut conversion occur later. An R8G8B8A8_UNORM intermediate cannot preserve physically based sky radiance.

Device startup validates all required image usage combinations. In particular:

- R11G11B10_UFLOAT is used for a storage LUT only if the device reports the required storage-image and sampled-image features; otherwise use RGBA16F.
- 3D RGBA16F storage images, sampled images, and the required image views are checked before atmosphere mode is exposed.
- cubemap/array image views, mip and layer transitions, and storage writes are capability-tested.
- maximum 2D/3D/cubemap dimensions, array layers, storage-image descriptor counts, and push-constant limits are checked against the selected quality tier.
- unsupported optional modes remain unavailable with a readable reason; the fallback sky remains available.

TextureSpecification and the render graph support a true depth extent, 3D image type/view creation, and 3D resource compatibility. A 32 x 32 x 32 LUT is a volume texture, not a 2D texture with a layer count. The sky implementation still performs the capability checks above before it enables atmosphere mode.

All values below use binary units and exclude transient command/descriptors:

| Resource | Allocation | Memory |
|---|---|---:|
| Transmittance + multiscattering | 256 x 64 + 32 x 32 RGBA16F | about 136 KiB |
| One 192 x 108 sky-view + two 32 cubed aerial LUTs | RGBA16F | about 674 KiB per RenderView |
| 512 cubemap source radiance, base level | 6 faces RGBA16F | 12 MiB |
| The same source with a complete mip chain | RGBA16F | about 16 MiB |
| Diffuse + full-chain 128 specular IBL | RGBA16F | about 1.05 MiB per SkyEnvironment |
| 512 x 512 BRDF LUT | RGBA16F | 2 MiB engine-global |

Version 1 uses the complete source mip chain: an atmosphere environment is therefore about 17.2 MiB plus 674 KiB per view; with the shared BRDF LUT initialized, the first such environment is about 19.9 MiB. A 512 cubemap is 12 MiB at its base level and about 16 MiB with its full chain; the BRDF LUT is 2 MiB, not 512 KiB.

HDRI memory depends on the selected cooked cubemap quality tier. The old 32 MiB/128 MiB raw 2K/4K equirectangular estimates are useful import-memory warnings, but are not the desired steady-state resident runtime budget.

The persistent-environment budget reserves the update peak, not only steady state: current snapshot, next bake snapshot, all in-flight pinned snapshots, and the global fallback. The shared BRDF LUT is counted once with the fallback. Static atmosphere LUTs are conservatively counted by every snapshot that references them, so sharing can defer a bake early but never undercount its peak. The bake is cancelled before this reservation would exceed `rendering.environment_lighting_budget_mb`; it never evicts the snapshot currently selected by a frame. Per-RenderView transient resources remain managed by the render-graph transient pool rather than this persistent-resource gate.

### 10.1 Colour, exposure, and output

All sky, LUT, and IBL images use linear scene colour. The renderer composes lighting and sky into an RGBA16F scene-colour target, then converts it to the display-compatible RenderView texture through a single ACES-fitted tone-mapping pass. Editor UI is composited afterward in display space. Optional pre-exposure is applied consistently to direct and environment lighting; bloom and automatic exposure metering remain follow-up work before tone mapping and output-gamut conversion.

Exposure metering defines how the bright sun disc and HDRI highlights influence the histogram, and it resets or smoothly adapts on a sky revision according to the camera-cut policy. Reference tests use fixed exposure. This prevents apparent lighting discontinuities from being hidden by unstable automatic exposure.

The editor viewport and ordinary window presentation consume the post-tone-mapped, display-compatible RenderView output. They do not sample the raw/pre-exposed HDR composition texture. HDR screenshots or offline capture request an explicitly named linear HDR export; UI composition must neither tone-map the image again nor apply a second sRGB conversion.

---

## 11. Diagnostics and validation

The editor exposes a sky diagnostic panel and render-graph debug names for:

- active mode, requested and published environment revisions, and bake state;
- asset-load error and fallback reason;
- source cubemap, each atmosphere LUT, irradiance, specular mips, and BRDF LUT;
- GPU duration and sample tier for each bake stage;
- per-view LUT resolution and transient-memory cost;
- active format fallbacks and capability failures.

Automated validation includes:

- shader reflection, descriptor-layout, UBO-size, and subresource-range checks;
- graphics and compute descriptor-binding/replay tests for every sky resource kind;
- render-graph declaration tests for exact environment versions, read/write dependencies, and imported-resource barriers;
- image/reference tests for noon, sunset, night, no celestial light, camera altitude changes, and reverse-Z;
- HDRI orientation, rotation, failed-load, hot-reload, and stale-completion tests;
- multi-viewport/editor/game/capture tests with distinct camera positions;
- resize and dynamic-resolution tests;
- in-flight rebake, resource retirement, low-memory fallback, and device-capability fallback tests;
- IBL roughness/LOD and cubemap-seam tests.

The visual test suite uses deterministic scene data, fixed exposure, and tolerance-based HDR image comparison.

---

## 12. Production release gates

The feature is ready for a supported quality tier only when all of the following are true:

- validation layers and the graph declaration validator report no descriptor, layout, subresource, lifetime, or synchronization errors;
- the fallback environment renders valid background and IBL on the first frame, while an HDRI loads, after load failure, and during a shader-interface rebuild;
- each supported platform/device tier passes the HDR reference suite, including resize, multiple views, reverse-Z, and hot-reload cases;
- peak memory for active, baking, fallback, and in-flight snapshots remains inside the configured environment budget;
- CPU frame allocation, descriptor binding, and command recording remain allocation-free outside the graph frame arena;
- bake timing stays within the selected tier budget, and cancelled/stale work cannot cause editor hitches or publish stale lighting;
- a graphics debugger capture verifies the source-cubemap to IBL dependency chain and the final HDR composition.

Unsupported tiers are not release failures when capability detection clearly disables them and the fallback sky remains correct.

---

## 13. Delivery order

| Step | Deliverable | Depends on |
|---:|---|---|
| 0 | HDR scene-colour pipeline, post-processing ordering, 3D texture/render-graph support, graphics/compute image-descriptor binder, reserved-set validation, format-capability service | Renderer foundations (3D resource and descriptor-binder support in progress) |
| 1 | Breaking SkyConfig schema, stable asset reference, project defaults, engine fallback environment | Asset and scene systems |
| 2 | Persistent SkyEnvironment snapshots, timeline-safe publish/retirement, graph-import contract, diagnostic state | Step 1 |
| 3 | Engine-global BRDF LUT and fallback IBL; LightingPass descriptor integration | Step 2 |
| 4 | SkySphere visual pass and correct background composition | Steps 0-3 |
| 5 | HDRI cooked-asset route, quality tiers, source cubemap, asynchronous rebuild | Steps 1-4 |
| 6 | HDRI irradiance/specular bake, timestamps, quality policy, hot reload | Step 5 |
| 7 | Static atmosphere LUTs and atmosphere source-radiance cubemap | Steps 0-3 |
| 8 | Per-view sky/aerial LUTs and opaque/transparent atmospheric composition | Step 7 |
| 9 | Atmosphere IBL baking, dynamic celestial-light budget, full validation matrix | Steps 6-8 |
| 10 | Retire the pre-SkyEnvironment background implementation after visual, graph, and fallback parity is proven (completed in #827) | All prior steps |

This order ships a useful, safe HDRI/SkySphere baseline before the more expensive atmosphere system, while preserving one resource and lifetime model for all modes.
