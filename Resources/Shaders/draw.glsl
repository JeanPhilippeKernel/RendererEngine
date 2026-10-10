// Pull-render helpers — assembles per-vertex data from the global geometry buffers.
// Uses geometry_bindings.glsl only — safe for all vertex stages including depth pre-pass.
#include "geometry_bindings.glsl"

DrawDataView GetDrawDataView()
{
    DrawDataView dataView;

    DrawData     dd     = DrawDataBuffer.Data[gl_BaseInstance];
    uint         refIdx = dd.IndexOffset + gl_VertexIndex;
    uint         verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex   v      = VertexBuffer.Data[verIdx];

    dataView.Vertex     = vec4(v.x, v.y, v.z, 1.0);
    dataView.Normal     = vec3(v.nx, v.ny, v.nz);
    dataView.TexCoord   = vec2(v.u, v.v);
    dataView.Transform  = TransformBuffer.Data[dd.TransformIndex];
    dataView.MaterialId = dd.MaterialIndex;
    return dataView;
}

DrawData FetchDrawData()
{
    return DrawDataBuffer.Data[gl_BaseInstance];
}

DrawVertex FetchVertexData()
{
    DrawData dd     = FetchDrawData();
    uint     refIdx = dd.IndexOffset + gl_VertexIndex;
    uint     verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    return VertexBuffer.Data[verIdx];
}

mat4 FetchTransform()
{
    return TransformBuffer.Data[FetchDrawData().TransformIndex];
}
