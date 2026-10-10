# ZEngine — Texture Compression and Transcoding

**Priority:** Next-year plan
**Status:** Design — the current importers do not provide a general cooked compressed
texture/transcode pipeline.
**Depends on:** import/cook artifacts, target GPU format policy, texture streaming, and
material binding.

## Scope

Texture compression is a platform-aware offline/cook and load-time transcode contract.
It must preserve the semantic texture type, color-space intent, mip policy, alpha
requirements, and normal-map conventions. A filename extension or a generic promise of
a compression ratio is not a format contract.

Earlier codec choices, file extensions, quality settings, and asset APIs were proposals,
not current engine behavior.

## Required design

- Define canonical source inputs and cooked container/metadata, including dimensions,
  mip chain, color space, alpha, normal/ORM/HDR semantics, hashes, versions, and bounds.
- Define per-platform supported GPU formats and fallback/transcode policy; validate
  feature availability on the selected device rather than assuming a desktop format.
- Decide encoder/tool licensing, reproducibility, rate-distortion settings, cache keys,
  build-farm compatibility, and source-artifact retention.
- Integrate uploads, descriptor lifetime, streaming, memory accounting, visual fallback,
  and import error diagnostics with the actual RenderResourceManager path.
- Preserve uncompressed/source-quality assets where a target cannot represent the
  semantic data without unacceptable loss.

## Acceptance gates

- Cooked metadata detects stale/corrupt/mismatched artifacts before upload.
- Representative albedo, alpha, normal, ORM, UI, HDR, and environment assets meet
  documented quality and memory measurements on supported hardware.
- Load, eviction, reload, and device-feature fallback paths are validation-layer clean
  and retain a defined placeholder on failure.
