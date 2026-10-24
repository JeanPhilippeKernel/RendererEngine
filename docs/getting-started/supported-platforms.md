# Supported platforms

ZEngine is actively developed and tested on the following platforms.

| Platform | Architectures covered by the source preset matrix |
|---|---|
| Windows | x64 |
| macOS | x64, Apple silicon (arm64) |
| Linux | x64; Debian and Ubuntu are the documented development environments |

Released engine builds are installed through [Zodiac Engine Hub](install-with-zodiac-engine-hub.md).
Source builds use the CMake presets listed in [Build from source](../development/build-from-source.md).

## Graphics support

ZEngine uses Vulkan. A usable Vulkan driver for the target platform is required
to run the renderer. Platform-specific drivers, SDK installation, and hardware
requirements vary by system, so verify Vulkan support for the target machine
before planning a deployment.
