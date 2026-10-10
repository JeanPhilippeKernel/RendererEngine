// Shared geometry and optical helpers for the analytic atmosphere shaders.

const float SKY_ATMOSPHERE_PI                    = 3.14159265358979323846;
const float SKY_ATMOSPHERE_ISOTROPIC_PHASE       = 0.07957747154594766788;
const uint  SKY_ATMOSPHERE_RAY_INTEGRATION_STEPS = 32u;

struct SkyAtmosphereParameters
{
    vec4 RadiiAndScaleHeights;
    vec4 RayleighScatteringAndGroundAlbedoR;
    vec4 MieAndOzone;
    vec4 OzoneAbsorption;
    vec4 GroundAlbedoAndAmbient;
};

bool sky_ray_sphere_intersection(vec3 origin, vec3 direction, float radius, out float near_distance, out float far_distance)
{
    const float projection  = dot(origin, direction);
    const float determinant = projection * projection - dot(origin, origin) + radius * radius;
    if (determinant < 0.0)
        return false;

    const float root = sqrt(determinant);
    near_distance    = -projection - root;
    far_distance     = -projection + root;
    return far_distance >= 0.0;
}

bool sky_ground_intersection(vec3 position, vec3 direction, float planet_radius, out float distance)
{
    const float position_radius = length(position);
    if (position_radius <= planet_radius + 1.0e-4 && dot(position, direction) < 0.0)
    {
        distance = 0.0;
        return true;
    }

    float near_distance = 0.0;
    float far_distance  = 0.0;
    if (!sky_ray_sphere_intersection(position, direction, planet_radius, near_distance, far_distance) || near_distance <= 1.0e-4)
        return false;

    distance = near_distance;
    return true;
}

float sky_rayleigh_phase(float cosine)
{
    return 3.0 * (1.0 + cosine * cosine) / (16.0 * SKY_ATMOSPHERE_PI);
}

float sky_mie_phase(float cosine, float anisotropy)
{
    const float g2          = anisotropy * anisotropy;
    const float denominator = max(1.0 + g2 - 2.0 * anisotropy * cosine, 1.0e-4);
    return (1.0 - g2) * (1.0 + cosine * cosine) / (8.0 * SKY_ATMOSPHERE_PI * denominator * sqrt(denominator));
}

float sky_ozone_density(float height, vec4 mie_and_ozone)
{
    return max(1.0 - abs(height - mie_and_ozone.z) / max(mie_and_ozone.w, 1.0e-3), 0.0);
}

vec2 sky_transmittance_uv(vec3 position, vec3 direction, vec4 radii_and_scale_heights)
{
    const float planet_radius     = max(radii_and_scale_heights.x, 1.0e-3);
    const float atmosphere_radius = max(radii_and_scale_heights.y, planet_radius + 1.0e-3);
    const float altitude          = clamp(length(position) - planet_radius, 0.0, atmosphere_radius - planet_radius);
    const float mu                = clamp(dot(normalize(position), direction), -1.0, 1.0);
    return vec2(mu * 0.5 + 0.5, altitude / max(atmosphere_radius - planet_radius, 1.0e-3));
}

vec3 sky_scattering_coefficient(SkyAtmosphereParameters parameters, float rayleigh_density, float mie_density)
{
    return parameters.RayleighScatteringAndGroundAlbedoR.rgb * rayleigh_density + vec3(parameters.MieAndOzone.x) * mie_density;
}

vec3 sky_extinction_coefficient(SkyAtmosphereParameters parameters, float rayleigh_density, float mie_density, float ozone)
{
    return parameters.RayleighScatteringAndGroundAlbedoR.rgb * rayleigh_density + vec3(parameters.MieAndOzone.x + parameters.MieAndOzone.y) * mie_density + parameters.OzoneAbsorption.rgb * ozone;
}
