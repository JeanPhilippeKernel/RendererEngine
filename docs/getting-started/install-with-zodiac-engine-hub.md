# Install ZEngine with Zodiac Engine Hub

Zodiac Engine Hub, also known as **Panzerfaust**, is the supported launcher and
project manager for released ZEngine builds. Use it to download, install, and
switch engine versions without building the repository yourself.

## Install the Hub

1. Open the [Zodiac Engine Hub releases page](https://github.com/JeanPhilippeKernel/ZodiacEngineHub/releases).
2. Download the package for your operating system.
3. Install and open Zodiac Engine Hub.

## Install an engine version

1. Open the Hub's engine-management view.
2. Select the ZEngine version you need. For the current stable version, select
   **ZEngine 1.0.0**.
3. Download and install that version from the Hub.
4. Create a project or open an existing one from the Hub.

The Hub supplies the generated `project.json` configuration when it launches the
engine. Treat that file as Hub-managed configuration: do not hand-edit it merely
to change an engine default. See [project configuration](../development/project-configuration.md)
for the runtime handoff contract.

## Next step

[Create or open your first project](first-project.md), or read the
[1.0.0 release notes](../project/releases.md) before migrating an existing project.
