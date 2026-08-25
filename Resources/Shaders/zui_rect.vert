#version 460 core

// Instanced rect renderer — RAD Debugger SDF approach.
// 6 vertices per instance (TRIANGLE_LIST): indices 0-5 map to 2 triangles.
// Corner order: 0=TL  1=TR  2=BL  3=BR
// Triangle 0: verts 0,1,2 → corners TL,TR,BR
// Triangle 1: verts 3,4,5 → corners TL,BR,BL

// Per-instance attributes (VK_VERTEX_INPUT_RATE_INSTANCE)
layout(location = 0) in vec4 i_dst;           // x0,y0,x1,y1  screen pixels
layout(location = 1) in vec4 i_src;           // u0,v0,u1,v1  UV coords
layout(location = 2) in vec4 i_color0;        // TL corner RGBA
layout(location = 3) in vec4 i_color1;        // TR corner RGBA
layout(location = 4) in vec4 i_color2;        // BL corner RGBA
layout(location = 5) in vec4 i_color3;        // BR corner RGBA
layout(location = 6) in vec4 i_corner_radii;  // per-corner radius (TL,TR,BL,BR)
layout(location = 7) in vec4 i_style;         // x=border_thickness y=edge_softness z=tex_index w=shear

layout(push_constant) uniform PC {
    vec2 scale;     // (2/fb_w, 2/fb_h)
    vec2 translate; // (-1, -1)
};

layout(location = 0) out vec2  v_sdf_pos;       // position relative to rect centre (pixels)
layout(location = 1) out vec2  v_half_size;      // rect half-size (pixels)
layout(location = 2) out vec2  v_uv;             // texture UV
layout(location = 3) out vec4  v_color;          // per-corner tint
layout(location = 4) out float v_corner_radius;  // this corner's radius
layout(location = 5) out float v_border;
layout(location = 6) out float v_softness;
layout(location = 7) out float v_tex_index;      // float-cast bindless index

void main()
{
    // 4 corner positions in screen pixels
    vec2 pos[4] = vec2[](
        vec2(i_dst.x, i_dst.y),   // 0: TL
        vec2(i_dst.z, i_dst.y),   // 1: TR
        vec2(i_dst.x, i_dst.w),   // 2: BL
        vec2(i_dst.z, i_dst.w)    // 3: BR
    );

    // UV for each corner
    vec2 uvs[4] = vec2[](
        vec2(i_src.x, i_src.y),   // TL
        vec2(i_src.z, i_src.y),   // TR
        vec2(i_src.x, i_src.w),   // BL
        vec2(i_src.z, i_src.w)    // BR
    );

    // Per-corner tints
    vec4 colors[4] = vec4[](i_color0, i_color1, i_color2, i_color3);

    // Per-corner radii
    float radii[4] = float[](
        i_corner_radii.x,  // TL
        i_corner_radii.y,  // TR
        i_corner_radii.z,  // BL
        i_corner_radii.w   // BR
    );

    // Map 6 vertex indices → 4 corners (two triangles)
    // tri0: 0→TL(0), 1→TR(1), 2→BR(3)
    // tri1: 3→TL(0), 4→BR(3), 5→BL(2)
    int corner_map[6] = int[](0, 1, 3,  0, 3, 2);
    int corner = corner_map[gl_VertexIndex];

    vec2 screen_pos = pos[corner];

    // Apply shear to bottom corners (BL=2, BR=3)
    if (corner >= 2) { screen_pos.x += i_style.w; }

    // SDF helper: position relative to rect centre
    vec2 half_size      = (i_dst.zw - i_dst.xy) * 0.5;
    vec2 ndc_corners[4] = vec2[](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0),
        vec2(-1.0,  1.0), vec2( 1.0,  1.0)
    );
    v_sdf_pos      = ndc_corners[corner] * half_size;
    v_half_size    = half_size;
    v_uv           = uvs[corner];
    v_color        = colors[corner];
    v_corner_radius= radii[corner];
    v_border       = i_style.x;
    v_softness     = i_style.y;
    v_tex_index    = i_style.z;

    // NDC: screen (0,0)=top-left maps to Vulkan NDC (-1,-1)=top-left via scale+translate.
    // No Y-flip needed — our screen Y increases downward, same as Vulkan framebuffer Y.
    gl_Position = vec4(screen_pos * scale + translate, 0.0, 1.0);
}
