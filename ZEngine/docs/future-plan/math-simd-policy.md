# ZEngine — Portable Math and SIMD Policy

**Priority:** P3 — correctness and measurement before optimization
**Status:** Planning
**Tracked by:** [#796](https://github.com/JeanPhilippeKernel/RendererEngine/issues/796)

## Current baseline

ZEngine owns its vector, matrix, and quaternion types. Scalar transcendental
operations use the standard library as the correctness baseline. The public engine
type boundary must remain independent of a platform vendor library, compiler-specific
vector type, or an optional SIMD package.

There is no measured case for a general SIMD backend yet. Adding one because a
platform supports vector instructions is not itself a performance result.

## Required decisions

1. Define numerical expectations for the engine-owned math operations: finite-input
   behavior, tolerances, angle range/reduction, denormal/floating-point-mode policy,
   and deterministic requirements where gameplay or tests need them.
2. Benchmark representative transform, camera, culling, skinning, and batched
   trigonometry workloads on Apple Silicon, Windows x64, and Linux x64. Capture the
   compiler, flags, workload, data alignment, and scalar baseline with every result.
3. Evaluate an implementation hidden behind the existing engine types: targeted
   platform intrinsics and narrowly scoped portable libraries are candidates only if
   they preserve the public ABI and have a tested scalar fallback.
4. Document the selected policy in an ADR, including rejected alternatives and the
   conditions under which a new optimized path is allowed.

## Constraints

- No public API may expose platform SIMD registers or a third-party vector/matrix
  type.
- An optimized path must produce documented-equivalent results to the scalar path,
  retain an allocation-free hot path, and preserve supported-platform builds.
- Do not make game determinism depend on unqualified floating-point behavior across
  architectures. Where bitwise determinism is required, specify and test it
  independently of visual/rendering math.

## Completion evidence

- Precision regression tests cover the defined scalar contract.
- Reproducible measurements justify each optimized operation on its target hardware.
- Scalar fallback and selected optimized paths build and pass equivalence tests on
  every supported platform.
- The ADR records the compatibility, alignment, compiler, and floating-point-mode
  decisions.
