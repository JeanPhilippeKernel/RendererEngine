#version 460
layout(location = 0) noperspective in vec3 Local;
layout(location = 1) flat in vec4 Shape;
layout(location = 2) flat in vec4 Style;
layout(location = 3) flat in vec4 FillColor;
layout(location = 4) flat in vec4 OutlineColor;
layout(location = 5) flat in vec3 StartJoinPlane;
layout(location = 6) flat in vec3 EndJoinPlane;
layout(location = 0) out vec4 OutputColor;

float coverage(float distancePx)
{
    return clamp(0.5 - distancePx, 0, 1);
}
float boxDistance(vec2 point, vec2 halfSize)
{
    vec2 d = abs(point) - halfSize;
    return length(max(d, vec2(0))) + min(max(d.x, d.y), 0);
}

void main()
{
    int   kind  = int(Style.w);
    vec4  color = FillColor;
    float alpha = 1;
    if (kind == 0)
    {
        vec3 width = max(fwidth(Local), vec3(1e-7));
        vec3 edges = smoothstep(vec3(0), width, Local);
        int  mask  = int(Style.x);
        if ((mask & 1) == 0)
            edges.x = 1;
        if ((mask & 2) == 0)
            edges.y = 1;
        if ((mask & 4) == 0)
            edges.z = 1;
        alpha = min(edges.x, min(edges.y, edges.z));
    }
    else if (kind == 1)
    {
        int  flags = int(Style.y);
        // Shared endpoints use one identical screen-space partition. Exactly
        // one segment owns each sample, including translucent strokes/glows.
        vec3 pixel = vec3(gl_FragCoord.xy, 1);
        if (((flags & 2) != 0 && dot(StartJoinPlane, pixel) < 0) || ((flags & 4) != 0 && dot(EndJoinPlane, pixel) >= 0))
            discard;
        float distancePx;
        if ((flags & 1) != 0)
            distancePx = length(vec2(Local.x - clamp(Local.x, 0, Shape.x), Local.y)) - Shape.y;
        else
            distancePx = boxDistance(Local.xy - vec2(Shape.x * 0.5, 0), vec2(Shape.x * 0.5, Shape.y));
        alpha = coverage(distancePx);
        if (Shape.z > 0 && Shape.w > 0)
        {
            float cycle         = Shape.z + Shape.w;
            float phase         = mod(Local.x + Style.x, cycle);
            float dashDistance  = abs(mod(phase - Shape.z * 0.5 + cycle * 0.5, cycle) - cycle * 0.5) - Shape.z * 0.5;
            alpha              *= coverage(dashDistance);
        }
    }
    else
    {
        float distancePx  = Shape.z > 0.5 ? length(Local.xy) - Shape.x : boxDistance(Local.xy, vec2(Shape.x));
        float outer       = coverage(distancePx);
        float inner       = coverage(distancePx + Shape.y);
        float fillAlpha   = FillColor.a * inner;
        float borderAlpha = OutlineColor.a * max(outer - inner, 0);
        float combined    = fillAlpha + borderAlpha;
        color             = vec4(combined > 0 ? (FillColor.rgb * fillAlpha + OutlineColor.rgb * borderAlpha) / combined : vec3(0), combined);
    }
    OutputColor = vec4(color.rgb, color.a * alpha);
}
