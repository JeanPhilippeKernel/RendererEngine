# Develop with ZEngine

This section is for contributors and teams building ZEngine from source. If you
only need an installed engine to create or open a project, start with
[Zodiac Engine Hub](../getting-started/install-with-zodiac-engine-hub.md).

<div class="grid cards" markdown>

- :material-hammer-wrench: [**Build from source**](build-from-source.md)

  Install the required tooling and build with the repository script or a CMake preset.

- :material-play: [**Run and debug**](run-and-debug.md)

  Launch the `Obelisk` application and pass a Hub-generated project configuration.

- :material-file-cog-outline: [**Project configuration**](project-configuration.md)

  Learn the boundary between the Hub-generated configuration and the engine.

</div>

The repository uses CMake and C++20. Dependencies are fetched through CMake
`FetchContent`; no manual submodule initialization is required.
