#version 460

layout(push_constant) uniform SkySpherePushConstants
{
    vec4  HorizonColor;
    vec4  ZenithColor;
    vec4  GroundColor;
    vec4  SunDirection;
    float SunDiscAngularRadiusRadians;
    float SunDiscIntensity;
    float HorizonSharpness;
    float ShowSunDisc;
}
Sky;

layout(location = 0) in vec3 ViewDirection;
layout(location = 0) out vec4 OutColor;

vec3 gradient_radiance(vec3 direction)
{
    const float elevation     = clamp(direction.y, -1.0, 1.0);
    const float sharpness     = max(Sky.HorizonSharpness, 1.0e-4);
    const float sky_weight    = pow(max(elevation, 0.0), sharpness);
    const float ground_weight = pow(max(-elevation, 0.0), sharpness);
    return elevation >= 0.0 ? mix(Sky.HorizonColor.rgb, Sky.ZenithColor.rgb, sky_weight) : mix(Sky.HorizonColor.rgb, Sky.GroundColor.rgb, ground_weight);
}

float sun_disc(vec3 direction)
{
    if (Sky.ShowSunDisc < 0.5 || Sky.SunDiscAngularRadiusRadians <= 0.0)
        return 0.0;

    const float direction_length_squared = dot(Sky.SunDirection.xyz, Sky.SunDirection.xyz);
    if (direction_length_squared <= 1.0e-12)
        return 0.0;

    const float cosine_to_sun = dot(direction, Sky.SunDirection.xyz * inversesqrt(direction_length_squared));
    const float edge_width    = max(fwidth(cosine_to_sun), 1.0e-5);
    const float threshold     = cos(min(Sky.SunDiscAngularRadiusRadians, 0.25));
    return smoothstep(threshold - edge_width, threshold + edge_width, cosine_to_sun);
}

void main()
{
    const vec3 direction = normalize(ViewDirection);
    const vec3 sky       = gradient_radiance(direction);
    const vec3 sun       = vec3(max(Sky.SunDiscIntensity, 0.0) * sun_disc(direction));
    OutColor             = vec4(sky + sun, 1.0);
}
