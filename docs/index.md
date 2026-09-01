# Zodiac Engine (ZEngine) — Documentation

Welcome to the Zodiac Engine documentation — the reference for ZEngine's architecture, systems, and design decisions.

## Pages

| Page | Description |
|---|---|
| [Engine Architecture](engine-architecture.md) | Thread model, per-frame main/render loop, cross-thread communication channels, ECS, asset pipeline, VFS, init/shutdown order |
| [Memory Management](memory-management.md) | Arena allocator design, allocation macros, scratch arenas, memory budget, GPU memory domains, Vulkan object teardown rules |
| [Rendering Domain](rendering-domain.md) | GPU rendering pipeline, global geometry buffers, render graph and passes, RenderResourceManager, shutdown teardown |
| [Asset Manager](asset-manager.md) | CPU-side asset registry, ingest pipeline (mesh/texture/material), GPU material binding, thread safety, initialization contract |
| [ZUI System](zui-system.md) | Native retained-mode UI — box model, layout solver, interaction system, docking, font atlas, Vulkan renderer |
| [Containers](containers.md) | Arena-backed containers: Array, UnorderedHashMap, HashMap (ordered), String — capacity contract, pitfalls, usage patterns |
| [Roadmap](roadmap.md) | Feature roadmap and status |
