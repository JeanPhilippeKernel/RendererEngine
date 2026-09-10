#version 460
#extension GL_GOOGLE_include_directive : require
#include "texture_bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_ray_dir;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 1) uniform UBAtmosphere
{
    vec4  RayleighScattering;
    vec4  MieScattering;
    vec4  MieAbsorption;
    vec4  OzoneAbsorption;
    float PlanetRadius;
    float AtmosphereRadius;
    float RayleighScaleHeight;
    float MieScaleHeight;
    float MieAnisotropy;
    float OzoneLayerCentre;
    float OzoneLayerWidth;
    float SunIlluminanceScale;
    vec4  SunDirection;
    float SunAngularRadius;
    float _pad0[3];
    vec4  CameraPositionKm;
    uint  TransmittanceLUTIndex;
    uint  MultiscatterLUTIndex;
    uint  SkyviewLUTIndex;
    float _pad1;
}
Atmo;

const float PI = 3.14159265;

void        main()
{
    vec3  dir       = normalize(v_ray_dir);
    float lat       = asin(dir.y);
    float azimuth   = atan(dir.x, dir.z);
    float v         = sign(lat) * sqrt(abs(lat) / (PI * 0.5));
    vec2  sky_uv    = clamp(vec2(azimuth / (2.0 * PI) + 0.5, v * 0.5 + 0.5), 0.0, 1.0);

    vec3  sky       = texture(sampler2D(TextureArray[Atmo.SkyviewLUTIndex], LinearClampSampler), sky_uv).rgb;

    float cos_angle = dot(dir, normalize(Atmo.SunDirection.xyz));
    float cos_disc  = cos(Atmo.SunAngularRadius * PI / 180.0);
    if (cos_angle > cos_disc)
    {
        float t  = (cos_angle - cos_disc) / (1.0 - cos_disc);
        sky     += vec3(Atmo.SunIlluminanceScale) * smoothstep(0.0, 1.0, t);
    }
    o_color = vec4(sky, 1.0);
}
