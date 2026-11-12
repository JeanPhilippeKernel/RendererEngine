# CI workflow

GitHub Actions validates changes, builds and tests the engine on supported
platforms, and packages releases. The workflow definitions live in
`.github/workflows/`.

## Engine CI

[`Engine-CI.yml`](.github/workflows/Engine-CI.yml) runs for pushes and pull
requests targeting `main` or `develop` when engine, editor, application,
resource, script, CMake, dependency, or workflow files change. It can also be
started manually with `workflow_dispatch`.

The workflow coordinates these checks:

1. **Scene evidence tests** run Python unit tests on Ubuntu, Windows, and
   macOS.
2. **Commitlint** checks pull request commits, except for the
   `develop`-to-`main` promotion pull request.
3. **clang-format** checks the engine, editor, application, and shader sources.
4. **Build and test jobs** run for Linux x64, Windows x64, and macOS arm64.
   Pull requests run Debug. Pushes run Release and enable deployment. Manual
   runs can select Debug, Release, and deployment options.
5. **Deployment jobs** package Release artifacts when deployment is enabled.

Each platform workflow builds with the repository's CMake presets and then
runs the engine tests, editor tests, and process-level scene/cook checks. Build
and test jobs are separated by uploaded artifacts.

## Other workflows

- `job-cmakebuild-*.yml` and `job-test-*.yml` define reusable per-platform
  build and test jobs.
- `job-clangformat.yml` and `job-commitlint.yml` provide reusable quality
  checks.
- `release-please.yml` handles stable releases from `main`.
- `release-please-prerelease.yml` handles prereleases from `develop`.
- `publish-prerelease.yml` builds and publishes prerelease artifacts.
- `docs.yml` builds the MkDocs site and deploys it to the documentation
  repository from `main`.

## Running checks locally

Use the repository build script from PowerShell Core:

```powershell
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $True
```

The script runs clang-format by default. To check formatting without modifying
files, pass `-VerifyFormatting $True`. For direct CTest use, configure and
build a preset, install the build, then run `ctest --test-dir <build-dir>
--output-on-failure` where tests are registered for that configuration.

See [Requirements](requirements.md) for toolchain dependencies and
[Contributing](CONTRIBUTING.md) for contribution expectations.
