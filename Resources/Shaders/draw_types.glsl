// Vertex and draw-call data structures used by the pull-render pipeline.
// No descriptor bindings — include this wherever the structs are needed.

struct DrawVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct DrawData
{
    uint VertexOffset;
    uint VertexCount;
    uint IndexOffset;
    uint IndexCount;
    uint AllocationCount;
    uint InstanceCount;
    uint TransformIndex;
    uint MaterialIndex;
};

struct DrawDataView
{
    uint MaterialId;
    mat4 Transform;
    vec4 Vertex;
    vec3 Normal;
    vec2 TexCoord;
};
