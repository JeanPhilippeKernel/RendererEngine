# Release process

ZEngine maintains two release tracks.

| Track | Branch | Purpose |
|---|---|---|
| Development candidate | `develop` | Produces release candidates while active development continues. |
| Stable | `main` | Produces the stable release PR, tag, changelog, and platform artifacts. |

Release automation reads Conventional Commit messages. A release PR updates
`VERSION.txt`, the release manifest, and `CHANGELOG.md`; merging the approved
release PR creates the tag and GitHub release.

## Publishing documentation

The documentation website deploys from `main` when `docs/**` or `mkdocs.yml`
changes. Documentation changes should therefore be reviewed with a local MkDocs
build before they are merged into the stable branch.

## Release consumers

Release users install an engine version through
[Zodiac Engine Hub](../getting-started/install-with-zodiac-engine-hub.md), rather
than building the repository. Each release announcement should link to the
[release page](../project/releases.md) and the [full changelog](https://github.com/JeanPhilippeKernel/RendererEngine/blob/main/CHANGELOG.md).

For the repository's detailed automation policy, see
[CONTRIBUTING.md](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/CONTRIBUTING.md#release-process).
