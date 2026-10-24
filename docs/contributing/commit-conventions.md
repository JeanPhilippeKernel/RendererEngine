# Commit conventions

ZEngine uses [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).
Commitlint runs on pull requests, and release automation uses the commit type to
produce version and changelog changes.

```text
<type>(<scope>): <subject>
```

The header must be imperative, lowercase, have no trailing period, and stay at
or below 100 characters.

| Type | Use |
|---|---|
| `feat` | User-visible or consumer-visible feature |
| `fix` | Bug fix |
| `perf` | Measured performance improvement |
| `refactor` | Code restructuring without intended behavior change |
| `docs` | Documentation-only change |
| `test`, `build`, `ci`, `style`, `chore` | Maintenance work in the named area |

Use `!` after the type or a `BREAKING CHANGE:` footer when a change requires a
major version bump. Examples:

```text
feat(rendering): add indirect draw support
fix(vulkan): correct semaphore teardown
feat!: remove a legacy startup API
```

See [CONTRIBUTING.md](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/CONTRIBUTING.md)
for the repository policy and complete examples.
