# Contribute to ZEngine

Contributions start with a discussion in the
[issue tracker](https://github.com/JeanPhilippeKernel/RendererEngine/issues).
Use the documentation site to understand current contracts, then keep changes
small, tested, and connected to an approved issue or design.

<div class="grid cards" markdown>

- :material-source-commit: [**Commit conventions**](commit-conventions.md)

  Use Conventional Commits so CI and release automation can classify the change.

- :material-rocket-launch-outline: [**Release process**](release-process.md)

  See how development candidates and stable releases are produced.

- :material-book-open-variant: [**Design documents**](../design/index.md)

  Check the implementation status before treating a design as current behavior.

</div>

## Local expectations

The repository installs Git hooks through CMake. The pre-push hook runs the
formatting check across engine, editor, and shader sources when PowerShell Core
and clang-format are available. CI remains the authoritative check.
