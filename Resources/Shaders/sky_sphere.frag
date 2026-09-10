#version 460

layout(location = 0) in  vec3 v_ray_dir;
layout(location = 0) out vec4 o_color;

layout(push_constant) uniform SkySpherePush
{
    vec4  HorizonColor;
    vec4  ZenithColor;
    vec4  GroundColor;
    vec4  SunDirection;    // w unused
    float SunDiscSize;
    float SunDiscIntensity;
    float HorizonSharpness;
    float ShowSunDisc;
}
pc;

void main()
{
    vec3  dir = normalize(v_ray_dir);
    float h   = dir.y; // -1 = nadir, 0 = horizon, +1 = zenith

    vec3 sky;
    if (h >= 0.0)
        sky = mix(pc.HorizonColor.rgb, pc.ZenithColor.rgb, pow(h, 1.0 / max(pc.HorizonSharpness, 0.001)));
    else
        sky = mix(pc.HorizonColor.rgb, pc.GroundColor.rgb, pow(-h, 1.0 / max(pc.HorizonSharpness, 0.001)));

    if (pc.ShowSunDisc > 0.5)
    {
        vec3  sun_dir   = normalize(pc.SunDirection.xyz);
        float cos_angle = dot(dir, sun_dir);
        float cos_disc  = cos(pc.SunDiscSize);
        if (cos_angle > cos_disc)
        {
            float t = (cos_angle - cos_disc) / (1.0 - cos_disc);
            sky += vec3(1.0) * pc.SunDiscIntensity * smoothstep(0.0, 1.0, t);
        }
    }

    o_color = vec4(sky, 1.0);
}
