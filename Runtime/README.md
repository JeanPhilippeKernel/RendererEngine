# Editor-free runtime

`ZEngineRuntime` is a renderer-only executable linked to `zEngineLib`; it does
not link the Tetragrama editor. Configure with `-DZENGINE_BUILD_EDITOR=OFF` to
omit the editor and its Obelisk entry point from the build. Build the
`ZEngineRuntime` target. The build copies engine settings and compiled shaders
into `ZodiacEngine` next to the executable and refreshes them on subsequent builds.

Launch it with a cooked package and startup scene UUID:

```
ZEngineRuntime --cookedPackage /path/to/package --cookedScene <scene-uuid>
```

The scene must contain exactly one main camera, unless `--cookedCamera` selects
one explicitly. The package must already exist; creating a user-facing cooking
and export workflow is separate work. Simulation is disabled. The current
runtime shares the existing scene-loading memory profile, including the arenas
currently named for the editor.

To register the desktop Vulkan smoke test, configure with
`-DZENGINE_ENABLE_RUNTIME_DESKTOP_TESTS=ON`, build `ZEngineRuntime` and
`SceneCookProcess`, and run `ctest -R RuntimeDesktop --output-on-failure`.
The test requires a desktop GPU and Vulkan validation layer.

The `RuntimeCLI.StartupArguments` CTest checks command-line validation without
a desktop GPU. Build `ZEngineRuntime` before running it.
