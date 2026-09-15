#version 460

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

layout(set = 2, binding = 0) uniform texture2D OpaqueSceneColor;
layout(set = 2, binding = 1) uniform texture2D SceneDepth;
layout(set = 2, binding = 2) uniform texture2D SkyViewLut;
layout(set = 2, binding = 3) uniform texture3D AerialInscatter;
layout(set = 2, binding = 4) uniform texture3D AerialTransmittance;
layout(set = 2, binding = 5) uniform texture2D AtmosphereTransmittance;
layout(set = 2, binding = 6) uniform sampler LinearClampToEdgeSampler;
layout(set = 2, binding = 7) uniform sampler SkyViewSampler;

layout(push_constant) uniform SkyCompositePushConstants
{
    vec4 AerialMaxDistanceAndDepthClear;
    vec4 PlanetCenterRelative;
    vec4 AtmosphereRadiiAndPadding;
    vec4 SunDirectionAndRadius;
    vec4 SunRadianceAndAvailability;
}
Push;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 OutColor;

float distance_from_depth(vec2 uv, float depth)
{
    vec4  world  = Camera.InvViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    float safe_w = abs(world.w) > 1.0e-6 ? world.w : (world.w < 0.0 ? -1.0e-6 : 1.0e-6);
    return length(world.xyz / safe_w - Camera.Position.xyz) / max(Push.AerialMaxDistanceAndDepthClear.w, 1.0e-3);
}

vec3 view_direction(vec2 uv)
{
    const vec2  ndc            = uv * 2.0 - 1.0;
    const float projection_x   = Camera.Projection[0][0];
    const float projection_y   = Camera.Projection[1][1];
    const float safe_x         = abs(projection_x) > 1.0e-6 ? projection_x : (projection_x < 0.0 ? -1.0e-6 : 1.0e-6);
    const float safe_y         = abs(projection_y) > 1.0e-6 ? projection_y : (projection_y < 0.0 ? -1.0e-6 : 1.0e-6);
    const vec3  camera_ray     = vec3(ndc.x / safe_x, ndc.y / safe_y, -1.0);
    const vec3  world_ray      = transpose(mat3(Camera.View)) * camera_ray;
    const float length_squared = dot(world_ray, world_ray);
    return length_squared > 1.0e-12 ? world_ray * inversesqrt(length_squared) : vec3(0.0, 1.0, 0.0);
}

vec3 make_tangent_reference(vec3 up, vec3 sun_direction)
{
    vec3 reference = sun_direction - up * dot(sun_direction, up);
    if (dot(reference, reference) < 1.0e-8)
    {
        const vec3 fallback = abs(up.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        reference           = cross(fallback, up);
    }
    return normalize(reference);
}

vec2 sky_view_uv(vec3 direction, vec3 up, vec3 sun_direction)
{
    const vec3  tangent    = make_tangent_reference(up, sun_direction);
    const vec3  bitangent  = normalize(cross(up, tangent));
    const float azimuth    = atan(dot(direction, bitangent), dot(direction, tangent));
    const float mu         = clamp(dot(direction, up), -1.0, 1.0);
    const float encoded_mu = sign(mu) * sqrt(abs(mu));
    return vec2(azimuth / (2.0 * 3.14159265358979323846) + 0.5, clamp(encoded_mu * 0.5 + 0.5, 0.0, 1.0));
}

bool ray_hits_ground(vec3 origin, vec3 direction, float planet_radius)
{
    const float projection  = dot(origin, direction);
    const float determinant = projection * projection - dot(origin, origin) + planet_radius * planet_radius;
    if (determinant < 0.0)
        return false;
    return -projection - sqrt(determinant) > 1.0e-4;
}

vec3 sun_disc_radiance(vec3 camera_position, vec3 up, vec3 direction, vec3 sun_direction)
{
    const float sun_radius = clamp(Push.SunDirectionAndRadius.w, 0.0, 0.25);
    if (Push.SunRadianceAndAvailability.w < 0.5 || sun_radius <= 0.0 || ray_hits_ground(camera_position, sun_direction, Push.AtmosphereRadiiAndPadding.x))
        return vec3(0.0);

    const float disc = smoothstep(cos(sun_radius), 1.0, dot(direction, sun_direction));
    if (disc <= 0.0)
        return vec3(0.0);

    const float altitude      = max(length(camera_position) - Push.AtmosphereRadiiAndPadding.x, 0.0);
    const float altitude_uv   = clamp(altitude / max(Push.AtmosphereRadiiAndPadding.y - Push.AtmosphereRadiiAndPadding.x, 1.0e-3), 0.0, 1.0);
    const float sun_mu        = clamp(dot(up, sun_direction), -1.0, 1.0);
    const vec3  transmittance = texture(sampler2D(AtmosphereTransmittance, LinearClampToEdgeSampler), vec2(sun_mu * 0.5 + 0.5, altitude_uv)).rgb;
    return transmittance * Push.SunRadianceAndAvailability.rgb * disc;
}

void main()
{
    const vec3  to_planet       = Push.PlanetCenterRelative.xyz;
    const float center_distance = length(to_planet);
    const vec3  camera_position = -to_planet;
    const vec3  up              = center_distance > 1.0e-3 ? camera_position / center_distance : vec3(0.0, 1.0, 0.0);
    const float sun_length      = length(Push.SunDirectionAndRadius.xyz);
    const vec3  sun_direction   = sun_length > 1.0e-6 ? Push.SunDirectionAndRadius.xyz / sun_length : vec3(0.0, 1.0, 0.0);
    const vec3  direction       = view_direction(TexCoord);
    const vec2  sky_uv          = sky_view_uv(direction, up, sun_direction);
    float       depth           = texture(sampler2D(SceneDepth, LinearClampToEdgeSampler), TexCoord).r;
    bool        is_background   = abs(depth - Push.AerialMaxDistanceAndDepthClear.y) <= Push.AerialMaxDistanceAndDepthClear.z;
    if (is_background)
    {
        vec3 sky  = texture(sampler2D(SkyViewLut, SkyViewSampler), sky_uv).rgb;
        sky      += sun_disc_radiance(camera_position, up, direction, sun_direction);
        OutColor  = vec4(sky, 1.0);
        return;
    }

    float distance       = distance_from_depth(TexCoord, depth);
    float max_distance   = max(Push.AerialMaxDistanceAndDepthClear.x, 1.0e-3);
    float distance_slice = clamp(log2(1.0 + distance) / log2(1.0 + max_distance), 0.0, 1.0);
    vec3  transmission   = texture(sampler3D(AerialTransmittance, LinearClampToEdgeSampler), vec3(sky_uv, distance_slice)).rgb;
    vec3  inscatter      = texture(sampler3D(AerialInscatter, LinearClampToEdgeSampler), vec3(sky_uv, distance_slice)).rgb;
    vec3  opaque         = texture(sampler2D(OpaqueSceneColor, LinearClampToEdgeSampler), TexCoord).rgb;
    OutColor             = vec4(opaque * transmission + inscatter, 1.0);
}
