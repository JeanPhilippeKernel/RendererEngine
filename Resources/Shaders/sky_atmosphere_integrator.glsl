// Shared radiance integration for the atmosphere source cubemap and view LUTs.
//
// M_ms is the RGB angular integral of higher-order incident radiance divided by
// direct solar radiance. The local scattering coefficient and 1 / (4 pi)
// isotropic phase factor are applied below, exactly once.

#include "sky_atmosphere_common.glsl"

vec3 sky_sample_sun_transmittance(vec3 position, vec3 sun_direction, vec4 radii_and_scale_heights)
{
    float ground_distance = 0.0;
    if (sky_ground_intersection(position, sun_direction, radii_and_scale_heights.x, ground_distance))
        return vec3(0.0);
    return texture(SKY_ATMOSPHERE_TRANSMITTANCE_LUT, sky_transmittance_uv(position, sun_direction, radii_and_scale_heights)).rgb;
}

vec3 sky_ground_radiance(SkyAtmosphereParameters parameters, vec3 ground_position, vec3 sun_direction, float sun_radiance)
{
    const vec3  normal              = normalize(ground_position);
    const float direct_illumination = max(dot(normal, sun_direction), 0.0) * sun_radiance;
    const vec3  direct              = sky_sample_sun_transmittance(ground_position + normal * 1.0e-3, sun_direction, parameters.RadiiAndScaleHeights) * direct_illumination;
    const vec3  irradiance          = direct + vec3(max(parameters.GroundAlbedoAndAmbient.w, 0.0));
    return max(parameters.GroundAlbedoAndAmbient.rgb, vec3(0.0)) * irradiance / SKY_ATMOSPHERE_PI;
}

vec3 sky_integrate_radiance_segment(SkyAtmosphereParameters parameters, vec3 camera_position, vec3 direction, vec3 sun_direction, float sun_radiance, float maximum_distance, out vec3 segment_transmittance)
{
    segment_transmittance         = vec3(1.0);
    const float planet_radius     = max(parameters.RadiiAndScaleHeights.x, 1.0e-3);
    const float atmosphere_radius = max(parameters.RadiiAndScaleHeights.y, planet_radius + 1.0e-3);
    float       atmosphere_near   = 0.0;
    float       atmosphere_far    = 0.0;
    if (!sky_ray_sphere_intersection(camera_position, direction, atmosphere_radius, atmosphere_near, atmosphere_far))
        return vec3(0.0);

    float ray_length = max(atmosphere_far, 0.0);
    if (maximum_distance >= 0.0)
        ray_length = min(ray_length, maximum_distance);
    float      ground_distance = 0.0;
    const bool hits_ground     = sky_ground_intersection(camera_position, direction, planet_radius, ground_distance);
    const bool reaches_ground  = hits_ground && ground_distance <= ray_length;
    if (reaches_ground)
        ray_length = min(ray_length, ground_distance);
    if (ray_length <= 1.0e-4)
        return reaches_ground ? sky_ground_radiance(parameters, camera_position, sun_direction, sun_radiance) : vec3(0.0);

    const float view_sun_cosine         = dot(direction, sun_direction);
    const float rayleigh_phase          = sky_rayleigh_phase(view_sun_cosine);
    const float mie_phase               = sky_mie_phase(view_sun_cosine, clamp(parameters.OzoneAbsorption.a, -0.95, 0.95));
    vec3        transmittance_to_viewer = vec3(1.0);
    vec3        radiance                = vec3(0.0);
    float       previous_distance       = 0.0;
    for (uint sample_index = 0u; sample_index < SKY_ATMOSPHERE_RAY_INTEGRATION_STEPS; ++sample_index)
    {
        const float sample_fraction   = (float(sample_index) + 0.5) / float(SKY_ATMOSPHERE_RAY_INTEGRATION_STEPS);
        const float sample_distance   = ray_length * sample_fraction * sample_fraction;
        const float step_length       = sample_distance - previous_distance;
        previous_distance             = sample_distance;
        const vec3  sample_position   = camera_position + direction * sample_distance;
        const float sample_height     = max(length(sample_position) - planet_radius, 0.0);
        const float rayleigh_density  = exp(-sample_height / max(parameters.RadiiAndScaleHeights.z, 1.0e-3));
        const float mie_density       = exp(-sample_height / max(parameters.RadiiAndScaleHeights.w, 1.0e-3));
        const float ozone             = sky_ozone_density(sample_height, parameters.MieAndOzone);
        const vec3  scattering        = sky_scattering_coefficient(parameters, rayleigh_density, mie_density);
        const vec3  extinction        = sky_extinction_coefficient(parameters, rayleigh_density, mie_density, ozone);
        const vec3  direct_source     = (parameters.RayleighScatteringAndGroundAlbedoR.rgb * rayleigh_density * rayleigh_phase + vec3(parameters.MieAndOzone.x) * mie_density * mie_phase) * sky_sample_sun_transmittance(sample_position, sun_direction, parameters.RadiiAndScaleHeights) * sun_radiance;
        const vec3  multiscattering   = texture(SKY_ATMOSPHERE_MULTISCATTERING_LUT, sky_transmittance_uv(sample_position, sun_direction, parameters.RadiiAndScaleHeights)).rgb;
        const vec3  multiple_source   = scattering * (multiscattering * SKY_ATMOSPHERE_ISOTROPIC_PHASE) * sun_radiance;

        radiance                     += transmittance_to_viewer * (direct_source + multiple_source) * step_length;
        transmittance_to_viewer      *= exp(clamp(-extinction * step_length, vec3(-50.0), vec3(0.0)));
    }
    segment_transmittance = transmittance_to_viewer;
    if (reaches_ground)
        radiance += transmittance_to_viewer * sky_ground_radiance(parameters, camera_position + direction * ground_distance, sun_direction, sun_radiance);
    return radiance;
}
