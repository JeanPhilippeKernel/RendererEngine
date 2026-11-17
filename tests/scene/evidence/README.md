# Scene runtime evidence

These files retain the observations cited by the
[M2 profile candidate](../../../docs/design/future-plan/scene-runtime-m2-profile-candidate.md). Each archive
contains evidence from its named run; it does not imply that the current build
or a production profile is qualified. Consult the profile for runtime identity,
scope, results and limitations.

- [capacity](capacity/) contains synthetic workload, draw-limit and texture
  admission observations.
- [failures](failures/) contains package-failure observations.
- [lifecycle](lifecycle/) contains cancellation, replacement and soak evidence.
- [performance](performance/) contains timing traces and presentation diagnostics.
- [visual](visual/) contains SampleProject capture and semantic-control evidence.

The reviewed reference images remain in `ZEngine/tests/Rendering/Fixtures`;
strict performance and calibrated-resource qualification is tracked in
[follow-up #880](https://github.com/JeanPhilippeKernel/RendererEngine/issues/880).
