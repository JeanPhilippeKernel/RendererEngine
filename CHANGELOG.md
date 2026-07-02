# Changelog

All notable changes to ZEngine will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are managed automatically via [Release Please](https://github.com/googleapis/release-please)
based on [Conventional Commits](https://www.conventionalcommits.org/).

## [0.3.0](https://github.com/JeanPhilippeKernel/RendererEngine/compare/v0.2.0...v0.3.0) (2026-07-02)


### Features

* added support of HashSet and OrderedHashMap containers ([c9d8c35](https://github.com/JeanPhilippeKernel/RendererEngine/commit/c9d8c356f3cc484066f825814e070f74bf91722d))
* added support of tag and versioning system ([#533](https://github.com/JeanPhilippeKernel/RendererEngine/issues/533)) ([13a7ace](https://github.com/JeanPhilippeKernel/RendererEngine/commit/13a7ace879c589acee522018aacc2d7299428fe3))
* added Virtual FileSystem design documents ([#528](https://github.com/JeanPhilippeKernel/RendererEngine/issues/528)) ([7b934bd](https://github.com/JeanPhilippeKernel/RendererEngine/commit/7b934bdfdf68e4c6286a113e2ce448baa19b054f))
* Convert enviroment cube texture to engine format for fast loading  ([#523](https://github.com/JeanPhilippeKernel/RendererEngine/issues/523)) ([c90975b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/c90975b0b90a3200b0efb6288790e5478b5ddaaa))
* Implemented the foundation of VFS ([#530](https://github.com/JeanPhilippeKernel/RendererEngine/issues/530)) ([67fbb53](https://github.com/JeanPhilippeKernel/RendererEngine/commit/67fbb5300a943d1c9754a0855407d90e60e5997a))
* **panzerfaust:** add search support allowing user to find projet by name ([#399](https://github.com/JeanPhilippeKernel/RendererEngine/issues/399)) ([#447](https://github.com/JeanPhilippeKernel/RendererEngine/issues/447)) ([dd95dd6](https://github.com/JeanPhilippeKernel/RendererEngine/commit/dd95dd6a81f93886081ca64d2660e2e6761f8aa8))


### Bug Fixes

* **buildengine:** update `glm`'s cmake minimum version to 3.5 ([#443](https://github.com/JeanPhilippeKernel/RendererEngine/issues/443)) ([488b55d](https://github.com/JeanPhilippeKernel/RendererEngine/commit/488b55df0b782287ada6c45a767a8f3906f214b1))
* corrected inconsistencies in documents ([67fbb53](https://github.com/JeanPhilippeKernel/RendererEngine/commit/67fbb5300a943d1c9754a0855407d90e60e5997a))
* corrected inconsistencies in documents ([4f9f14a](https://github.com/JeanPhilippeKernel/RendererEngine/commit/4f9f14aea4cefeb522ac3b198213fb72d8d86445))
* corrected inconsistencies in documents ([#532](https://github.com/JeanPhilippeKernel/RendererEngine/issues/532)) ([6d6ed63](https://github.com/JeanPhilippeKernel/RendererEngine/commit/6d6ed631b99f885133d7fae19f367f45e6cebc2d))
* improved HandleManager threading mgmt support ([#521](https://github.com/JeanPhilippeKernel/RendererEngine/issues/521)) ([95797b5](https://github.com/JeanPhilippeKernel/RendererEngine/commit/95797b55636a884674fdce9f0ae5e46f26949163))
* made release process consistent across main and develop ([#538](https://github.com/JeanPhilippeKernel/RendererEngine/issues/538)) ([9efb562](https://github.com/JeanPhilippeKernel/RendererEngine/commit/9efb562c263eeb6bb1b90bbd535eeb36a609a1e0))
* switch ArenaAllocator to VirtualAlloc/mmap with on-demand page commit ([#531](https://github.com/JeanPhilippeKernel/RendererEngine/issues/531)) ([43983c1](https://github.com/JeanPhilippeKernel/RendererEngine/commit/43983c116efd94288a5d6daa3ffb0054fa9d816d))
* updated package permission for release workflow ([#536](https://github.com/JeanPhilippeKernel/RendererEngine/issues/536)) ([1d9a0d3](https://github.com/JeanPhilippeKernel/RendererEngine/commit/1d9a0d34fed84a49cfd5b1fb1cea99ee55260abe))
* updated release pipeline ([#535](https://github.com/JeanPhilippeKernel/RendererEngine/issues/535)) ([a2f53c4](https://github.com/JeanPhilippeKernel/RendererEngine/commit/a2f53c4a86334a8544662279bfc535c8ec8954c5))
* updated the base version number ([#537](https://github.com/JeanPhilippeKernel/RendererEngine/issues/537)) ([046a407](https://github.com/JeanPhilippeKernel/RendererEngine/commit/046a407a296441d007803bf48bc891585d0b8e6a))


### Documentation

* add issue template for VFS secure functions refactor ([b77e5b4](https://github.com/JeanPhilippeKernel/RendererEngine/commit/b77e5b4290fbe6e483150c49541924e59e8ef3ab))
* added product planning documents ([#529](https://github.com/JeanPhilippeKernel/RendererEngine/issues/529)) ([f77569e](https://github.com/JeanPhilippeKernel/RendererEngine/commit/f77569ee031753180f45aaaaafc9067d18c46b84))


### CI/CD

* fix release-please config and manifest package key alignment ([#539](https://github.com/JeanPhilippeKernel/RendererEngine/issues/539)) ([0ad919e](https://github.com/JeanPhilippeKernel/RendererEngine/commit/0ad919e97ea0ca7c60b41953506387bb412fbbd3))
* move prerelease flags to top-level and fix PR title pattern ([#541](https://github.com/JeanPhilippeKernel/RendererEngine/issues/541)) ([702cefe](https://github.com/JeanPhilippeKernel/RendererEngine/commit/702cefefd50ab144bb781e02a804e7237ae4e247))
* pass prerelease flags as action inputs instead of config ([#543](https://github.com/JeanPhilippeKernel/RendererEngine/issues/543)) ([74ce42b](https://github.com/JeanPhilippeKernel/RendererEngine/commit/74ce42b7e0b3bf64f4a8ff66e5d02c5faef14739))

## [Unreleased]
