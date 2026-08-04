# Changelog

All notable changes to ZEngine will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are managed automatically via [Release Please](https://github.com/googleapis/release-please)
based on [Conventional Commits](https://www.conventionalcommits.org/).

## [0.4.0](https://github.com/JeanPhilippeKernel/RendererEngine/compare/v0.3.0...v0.4.0) (2026-08-04)


### Features

* added support of HashSet and OrderedHashMap containers ([c9d8c35](https://github.com/JeanPhilippeKernel/RendererEngine/commit/c9d8c356f3cc484066f825814e070f74bf91722d))
* added support of tag and versioning system ([#533](https://github.com/JeanPhilippeKernel/RendererEngine/issues/533)) ([13a7ace](https://github.com/JeanPhilippeKernel/RendererEngine/commit/13a7ace879c589acee522018aacc2d7299428fe3))
* added Virtual FileSystem design documents ([#528](https://github.com/JeanPhilippeKernel/RendererEngine/issues/528)) ([7b934bd](https://github.com/JeanPhilippeKernel/RendererEngine/commit/7b934bdfdf68e4c6286a113e2ce448baa19b054f))
* Convert enviroment cube texture to engine format for fast loading  ([#523](https://github.com/JeanPhilippeKernel/RendererEngine/issues/523)) ([c90975b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/c90975b0b90a3200b0efb6288790e5478b5ddaaa))
* **crashhandler:** add crash handler with signal capture, structured log, and crash dialog ([#551](https://github.com/JeanPhilippeKernel/RendererEngine/issues/551)) ([1e944cc](https://github.com/JeanPhilippeKernel/RendererEngine/commit/1e944cc7c070bed840476e80f11808891a3b753b))
* Implemented the foundation of VFS ([#530](https://github.com/JeanPhilippeKernel/RendererEngine/issues/530)) ([67fbb53](https://github.com/JeanPhilippeKernel/RendererEngine/commit/67fbb5300a943d1c9754a0855407d90e60e5997a))
* **memory:** implement MemoryBudgetConfig and wire MemoryManager through application stack ([#553](https://github.com/JeanPhilippeKernel/RendererEngine/issues/553)) ([e9ababc](https://github.com/JeanPhilippeKernel/RendererEngine/commit/e9ababccde2964aa0cd0973be2a71dd15fe95a6a))
* **panzerfaust:** add search support allowing user to find projet by name ([#399](https://github.com/JeanPhilippeKernel/RendererEngine/issues/399)) ([#447](https://github.com/JeanPhilippeKernel/RendererEngine/issues/447)) ([dd95dd6](https://github.com/JeanPhilippeKernel/RendererEngine/commit/dd95dd6a81f93886081ca64d2660e2e6761f8aa8))
* **panzerfaust:** build launcher UI with engine management, assets, and project workflow ([#554](https://github.com/JeanPhilippeKernel/RendererEngine/issues/554)) ([0320c5c](https://github.com/JeanPhilippeKernel/RendererEngine/commit/0320c5ce59349f964e8c7adf7798b36a34aeb06c))
* **vfs:** add VFS context support ([#550](https://github.com/JeanPhilippeKernel/RendererEngine/issues/550)) ([a926b15](https://github.com/JeanPhilippeKernel/RendererEngine/commit/a926b15124de5b58c5cb91fd62a81cb56857985e))
* **vfs:** implement VFS Memory, Scanner, directory Cache, and tests  ([#555](https://github.com/JeanPhilippeKernel/RendererEngine/issues/555)) ([6572998](https://github.com/JeanPhilippeKernel/RendererEngine/commit/657299854934ced8ae8fdf2cc5d925a3e31e13b7))


### Bug Fixes

* 3 issue : Add much more key in KeyCode struct ([292d0c4](https://github.com/JeanPhilippeKernel/RendererEngine/commit/292d0c47c482cfb5d7a4c9ad04b0b310d0e4dc39))
* 40 ([38e991e](https://github.com/JeanPhilippeKernel/RendererEngine/commit/38e991e2acd275fe35f1faad5ade7b7c9bdff287))
* 6 and [#5](https://github.com/JeanPhilippeKernel/RendererEngine/issues/5) ([1a7495d](https://github.com/JeanPhilippeKernel/RendererEngine/commit/1a7495dbf093c792963a2c79cd613d80c462689c))
* **buildengine:** update `glm`'s cmake minimum version to 3.5 ([#443](https://github.com/JeanPhilippeKernel/RendererEngine/issues/443)) ([488b55d](https://github.com/JeanPhilippeKernel/RendererEngine/commit/488b55df0b782287ada6c45a767a8f3906f214b1))
* corrected inconsistencies in documents ([67fbb53](https://github.com/JeanPhilippeKernel/RendererEngine/commit/67fbb5300a943d1c9754a0855407d90e60e5997a))
* corrected inconsistencies in documents ([4f9f14a](https://github.com/JeanPhilippeKernel/RendererEngine/commit/4f9f14aea4cefeb522ac3b198213fb72d8d86445))
* corrected inconsistencies in documents ([#532](https://github.com/JeanPhilippeKernel/RendererEngine/issues/532)) ([6d6ed63](https://github.com/JeanPhilippeKernel/RendererEngine/commit/6d6ed631b99f885133d7fae19f367f45e6cebc2d))
* **crashhandler:** prevented crashhandler UI Dialog to show-up on Windows CI tests ([#552](https://github.com/JeanPhilippeKernel/RendererEngine/issues/552)) ([9fe52f1](https://github.com/JeanPhilippeKernel/RendererEngine/commit/9fe52f1b709746e00bba4b7e352ee84ad58955c5))
* **deps:** pin gtest to v1.17.0 to avoid operator&lt;&lt; deletion on newer Clang ([e61bb29](https://github.com/JeanPhilippeKernel/RendererEngine/commit/e61bb2939197b0cc97d81db985d9a782d020c0f4))
* **editor:** init ActiveSceneName before append when scene list is empty ([5add6dc](https://github.com/JeanPhilippeKernel/RendererEngine/commit/5add6dcb2bba151eb36105e4f3c9e491713b92af))
* **engine:** fix startup and runtime crashes on macOS arm64 (MoltenVK) ([#564](https://github.com/JeanPhilippeKernel/RendererEngine/issues/564)) ([9159f1d](https://github.com/JeanPhilippeKernel/RendererEngine/commit/9159f1d0bf153026565a697401360b58d504a9b8))
* **format:** apply clang-format to DeviceSwapchain.cpp ([12b798b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/12b798b8052b66efa4bb85d4b5b7c88cf42bf9b1))
* Header Re-Organization for Consistency and Readability ([#497](https://github.com/JeanPhilippeKernel/RendererEngine/issues/497)) ([15af098](https://github.com/JeanPhilippeKernel/RendererEngine/commit/15af098d674abe31d6706baba966ae54f4c3ef1b))
* improved HandleManager threading mgmt support ([#521](https://github.com/JeanPhilippeKernel/RendererEngine/issues/521)) ([95797b5](https://github.com/JeanPhilippeKernel/RendererEngine/commit/95797b55636a884674fdce9f0ae5e46f26949163))
* made release process consistent across main and develop ([#538](https://github.com/JeanPhilippeKernel/RendererEngine/issues/538)) ([9efb562](https://github.com/JeanPhilippeKernel/RendererEngine/commit/9efb562c263eeb6bb1b90bbd535eeb36a609a1e0))
* **shader:** remove _unused sampler and fix pipeline layout with empty descriptor set ([#567](https://github.com/JeanPhilippeKernel/RendererEngine/issues/567)) ([9153732](https://github.com/JeanPhilippeKernel/RendererEngine/commit/915373298f454dd9e5161310022ec1bb7076b51e))
* **swapchain:** fix Linux startup crash due to Wayland extent and zero command buffer count ([#560](https://github.com/JeanPhilippeKernel/RendererEngine/issues/560)) ([4ec4036](https://github.com/JeanPhilippeKernel/RendererEngine/commit/4ec4036482e7385927917b917590dbb6485a056f))
* switch ArenaAllocator to VirtualAlloc/mmap with on-demand page commit ([#531](https://github.com/JeanPhilippeKernel/RendererEngine/issues/531)) ([43983c1](https://github.com/JeanPhilippeKernel/RendererEngine/commit/43983c116efd94288a5d6daa3ffb0054fa9d816d))
* updated package permission for release workflow ([#536](https://github.com/JeanPhilippeKernel/RendererEngine/issues/536)) ([1d9a0d3](https://github.com/JeanPhilippeKernel/RendererEngine/commit/1d9a0d34fed84a49cfd5b1fb1cea99ee55260abe))
* updated release pipeline ([#535](https://github.com/JeanPhilippeKernel/RendererEngine/issues/535)) ([a2f53c4](https://github.com/JeanPhilippeKernel/RendererEngine/commit/a2f53c4a86334a8544662279bfc535c8ec8954c5))
* updated the base version number ([#537](https://github.com/JeanPhilippeKernel/RendererEngine/issues/537)) ([046a407](https://github.com/JeanPhilippeKernel/RendererEngine/commit/046a407a296441d007803bf48bc891585d0b8e6a))
* **vfs:** removed duplicate NOMINMAX definition ([#557](https://github.com/JeanPhilippeKernel/RendererEngine/issues/557)) ([c848f2c](https://github.com/JeanPhilippeKernel/RendererEngine/commit/c848f2c63a8710ffcc532fa501f3a50f800c97e9))
* **vulkan:** fall back to CPU device (lavapipe) when no hardware GPU is found ([43d6929](https://github.com/JeanPhilippeKernel/RendererEngine/commit/43d69296ba7b7d351b0295906e23ffe3299b284b))


### Documentation

* add issue template for VFS secure functions refactor ([b77e5b4](https://github.com/JeanPhilippeKernel/RendererEngine/commit/b77e5b4290fbe6e483150c49541924e59e8ef3ab))
* added product planning documents ([#529](https://github.com/JeanPhilippeKernel/RendererEngine/issues/529)) ([f77569e](https://github.com/JeanPhilippeKernel/RendererEngine/commit/f77569ee031753180f45aaaaafc9067d18c46b84))
* mark all allocator bugs fixed and update sprint 1/2 status ([dd0ed89](https://github.com/JeanPhilippeKernel/RendererEngine/commit/dd0ed899e9e3d7ec055e71c6a73a64bc9fde5efb))
* **panzerfaust:** update location to point to ZodiacEngineHub repository ([a63f1f3](https://github.com/JeanPhilippeKernel/RendererEngine/commit/a63f1f3d317f96fdf44273cfc0b9eaf18f4be8a0))
* RAD Debugger-inspired UI system, 3D gizmo pass, and README overhaul ([#559](https://github.com/JeanPhilippeKernel/RendererEngine/issues/559)) ([8d3d1fc](https://github.com/JeanPhilippeKernel/RendererEngine/commit/8d3d1fcdd9e13de392ce347e485cdcc5cb6f3aa9))
* **readme:** updated logo image ([#556](https://github.com/JeanPhilippeKernel/RendererEngine/issues/556)) ([8added0](https://github.com/JeanPhilippeKernel/RendererEngine/commit/8added0a6696ab3bb63285f8a21c3d1a0c31c80a))


### CI/CD

* add workflow_dispatch trigger to manually publish release assets ([1e0973b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/1e0973b128d3294912a0b259588f985575c9f5ab))
* fix rc versioning logic and artifact packaging in release workflows ([#547](https://github.com/JeanPhilippeKernel/RendererEngine/issues/547)) ([84817ed](https://github.com/JeanPhilippeKernel/RendererEngine/commit/84817edb0cefa522fa48603ca730535361e3fe00))
* fix release-please config and manifest package key alignment ([#539](https://github.com/JeanPhilippeKernel/RendererEngine/issues/539)) ([0ad919e](https://github.com/JeanPhilippeKernel/RendererEngine/commit/0ad919e97ea0ca7c60b41953506387bb412fbbd3))
* generate release notes for rc releases from conventional commits ([#546](https://github.com/JeanPhilippeKernel/RendererEngine/issues/546)) ([724fc98](https://github.com/JeanPhilippeKernel/RendererEngine/commit/724fc98bb171593697a3a0de6bbfb15f2c0edc29))
* move prerelease flags to top-level and fix PR title pattern ([#541](https://github.com/JeanPhilippeKernel/RendererEngine/issues/541)) ([702cefe](https://github.com/JeanPhilippeKernel/RendererEngine/commit/702cefefd50ab144bb781e02a804e7237ae4e247))
* pass prerelease flags as action inputs instead of config ([#543](https://github.com/JeanPhilippeKernel/RendererEngine/issues/543)) ([74ce42b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/74ce42b7e0b3bf64f4a8ff66e5d02c5faef14739))
* replace release-please prerelease with custom bump script ([#544](https://github.com/JeanPhilippeKernel/RendererEngine/issues/544)) ([42982ba](https://github.com/JeanPhilippeKernel/RendererEngine/commit/42982bac1e62c16c5fa46765739b5b5a91cf466b))
* restructure build workflows with per-event stage gating and fix stable release pipeline ([#548](https://github.com/JeanPhilippeKernel/RendererEngine/issues/548)) ([2b7473a](https://github.com/JeanPhilippeKernel/RendererEngine/commit/2b7473aefd8fe9d68987f625099ffe2cebac2db0))
* tag-only prerelease to avoid protected branch push ([#545](https://github.com/JeanPhilippeKernel/RendererEngine/issues/545)) ([cc3d818](https://github.com/JeanPhilippeKernel/RendererEngine/commit/cc3d818864c39529587d66f5cc667efeb23c8fa4))
* update workflow triggers from master to main branch ([b51c0cd](https://github.com/JeanPhilippeKernel/RendererEngine/commit/b51c0cd26d13d99d44ea2eb0658176f714b31515))

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
