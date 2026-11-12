
# Contributing to ZEngine

When contributing to this repository, please discuss significant changes in an
issue before starting implementation. For smaller fixes, open a pull request
with a clear description and enough context for reviewers to understand the
change.

## Rules

**ZEngine** is built upon 2 rules:

1. Avoid work that unnecessarily slows down the rendering process.
2. Keep the engine and its workflows straightforward to use.

### Performance

ZEngine is a 3D rendering engine. Consider the performance impact of code on
frame-time, memory use, and resource lifetime. Prefer measured, maintainable
solutions over speculative optimization.

## Project guides

- [Architecture](architecture.md) describes the main components and their
  relationships.
- [Requirements](requirements.md) lists the tools and platform dependencies
  needed to build from source.
- [CI workflow](workflow.md) explains the GitHub Actions checks and build
  pipeline.
- [Development progress](progress.md) summarizes current priorities and
  separates active work from longer-term proposals.

## Commit Message Convention

ZEngine uses [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/). Every commit message is linted on pull requests and drives automatic versioning — the type you choose determines how the version number is bumped.

### Format

```
<type>(<scope>): <subject>

[optional body]

[optional footer(s)]
```

- **type** — required, lowercase (see table below)
- **scope** — optional, lowercase, names the subsystem affected (e.g. `rendering`, `vulkan`, `camera`, `memory`, `ci`)
- **subject** — required, imperative mood, no trailing period, max 100 characters total for the header
- **body** — free prose; wrap at 72 characters; separate from subject with a blank line
- **footer** — key: value pairs; `BREAKING CHANGE: <description>` triggers a major version bump

### Types and version impact

| Type | When to use | Version bump |
|---|---|---|
| `feat` | A new feature visible to users or engine consumers | minor (`0.3.0` → `0.4.0`) |
| `fix` | A bug fix | patch (`0.3.0` → `0.3.1`) |
| `perf` | A performance improvement with no API change | patch |
| `refactor` | Code restructuring with no behaviour or API change | patch |
| `docs` | Documentation only | none |
| `style` | Formatting, whitespace — no logic change | none |
| `test` | Adding or correcting tests | none |
| `build` | Build system or external dependency changes | none |
| `ci` | CI/CD pipeline changes | none |
| `chore` | Maintenance tasks (e.g. release bumps) | none |
| `revert` | Reverts a previous commit | patch |

A `!` suffix on any type (e.g. `feat!:`) or a `BREAKING CHANGE:` footer triggers a **major** bump.

### Examples

```
feat(rendering): add indirect draw support for mesh batches
```

```
fix(vulkan): correct semaphore leak on swapchain recreation
```

```
perf(memory): replace per-frame heap alloc with arena in render loop
```

```
feat!: remove legacy OpenGL backend

BREAKING CHANGE: the OpenGL renderer has been removed. Vulkan is now
the only supported backend. Update your application startup code to
remove any OpenGL-specific initialisation.
```

```
refactor(camera): extract projection logic into CameraUtils

No behaviour change. Simplifies FlyCamera and OrbitCamera by sharing
the common frustum calculation.
```

### What happens if a commit message is wrong

A CI check (`commitlint`) runs on every pull request and will block the build if any commit in the PR branch does not follow the format. Fix the message with `git rebase -i` before requesting review.

## Local Setup

See [Requirements](requirements.md) for platform prerequisites and
[Build from source](https://jeanphilippekernel.github.io/zodiac-engine-docs/development/build-from-source/)
for the complete setup and build instructions. Configure and build with the
repository script from PowerShell Core:

```powershell
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $True
```

Git hooks are copied into `.git/hooks` during CMake configuration when the
repository's `.git` path is a directory. The `pre-push` hook runs clang-format
on `ZEngine` and `Tetragrama` before pushing.

Formatting can also be checked locally with:

```powershell
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $False -RunClangFormat $True -VerifyFormatting $True
```

The formatting scripts require PowerShell Core (`pwsh`) and LLVM
`clang-format`. If the hook cannot run locally, CI performs the formatting
check before merging.

## Release Process

ZEngine has two release tracks, both fully automated from commit messages.

### Stable releases (`main`)

Merges into `main` are picked up by Release Please, which opens a **Release PR** accumulating all changes since the last stable tag. When that PR is merged:

- `VERSION.txt` is bumped (`0.3.0` → `0.3.1` / `0.4.0` / `1.0.0`) based on commit types
- A tag `v0.3.1` is created
- A GitHub Release is published with build artifacts for all platforms

### Pre-releases (`develop`)

Pushes to `develop` follow the same process but produce rc versions:

- Release Please opens a **Pre-release PR** on `develop`
- When merged: `VERSION.txt` is set to e.g. `0.4.0-rc.1`, tag `v0.4.0-rc.1` is created
- A GitHub Pre-release is published (marked as pre-release in the GitHub UI)
- Each subsequent batch of commits increments the rc counter: `rc.1` → `rc.2` → …

### Promoting a pre-release to stable

Once `develop` is stable enough to ship:

1. Open a PR from `develop` → `main`
2. Merge it — Release Please on `main` sees all the accumulated `feat:`/`fix:` commits and opens a stable Release PR
3. Merge the Release PR → `v0.4.0` stable is tagged and published

The `ZENGINE_VERSION_PRERELEASE` macro in the generated `Core/version.h` will be non-empty on rc builds and empty on stable, so engine code can detect this at compile time.

## Pull Request Process

1. Explain the problem being solved, the approach, and any relevant trade-offs.

2. Include focused tests for behavior that changes, and update directly related
   documentation when public behavior or developer workflows change.

3. Keep the pull request focused. Describe the checks you ran and note any
   platform-specific limitations.

4. Ensure the required CI checks pass and respond to review feedback before
   merging. Follow the repository's branch protection and review requirements.

