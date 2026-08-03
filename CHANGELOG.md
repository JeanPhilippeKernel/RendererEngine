# Changelog

All notable changes to ZEngine will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are managed automatically via [Release Please](https://github.com/googleapis/release-please)
based on [Conventional Commits](https://www.conventionalcommits.org/).

## [0.3.0] - 2026-08-04

### Features

- feat(vfs): implement VFS Memory, Scanner, directory Cache, and tests (#555)
- feat(memory): implement MemoryBudgetConfig and wire MemoryManager through application stack (#553)
- feat(panzerfaust): build launcher UI with engine management, assets, and project workflow (#554)
- feat(vfs): add VFS context support (#550)

### Bug Fixes

- fix(shader): remove _unused sampler and fix pipeline layout with empty descriptor set (#567)
- fix(imgui): fix blank screen in release build (#566)
- fix(vulkan): fall back to CPU device (lavapipe) when no hardware GPU is found
- fix(editor): init ActiveSceneName before append when scene list is empty
- fix(engine): fix startup and runtime crashes on macOS arm64 (MoltenVK) (#564)
- fix(swapchain): fix Linux startup crash due to Wayland extent and zero command buffer count (#560)
- fix(vfs): removed duplicate NOMINMAX definition (#557)
- fix(crashhandler): prevented crashhandler UI Dialog to show-up on Windows CI tests (#552)

## [Unreleased]
