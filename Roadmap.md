
This document outlines our vision for the next steps and the features we want to ship in this engine.

# Core engine
* [ ] Owned implementation of containers (vector, map, span, array, etc) and allocators (Arena)
* [ ] Owned implementation of Math library
* [ ] Support of RHI abstraction for DirectX, Metal, vulkan, OpenGL
* [ ] Support of Scene runtime actions (Play/Pause/Stop)
* [x] Support of Scene serialization / deserialization
* [x] Support of imported model (as today OBJ model) serialization / deserialization
* [ ] Support of Animation board
* [ ] Support of Scene picking object

# Rendering
* [x] Support of Render Graph
* [x] Support of Gizmo
* [ ] Support of ktx Textures
* [ ] Support of PBR material
* [ ] Support of shadow mapping
* [ ] Support of Global Illumination (Ray Tracing, Ambient Occlusion, Screen-Space Global Illumination)
* [ ] Support of Motion Blur
* [ ] Support of Shader editing and Hot reloading
* [ ] Support of Async Compute
* [ ] Support of Multithreaded Rendering
* [ ] Support of Memory aliasing from Render Graph
* [ ] Support of GLTF model import
* [ ] Support of deferred rendering
* [ ] Support of Scene Occlusion culling
* [ ] Support of LOD (level of Detail)

# Scripting
* [ ] Support of object scripting with C# and Lua
