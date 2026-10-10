# ZEngine — Crash Handler

**Status:** Implemented platform crash-handler subsystem; production reporting and
operational policy remain product work.
**Source:** `ZEngine/ZEngine/CrashHandlers/`

## Current contract

`CrashHandlers::CrashHandler` has platform implementations for Windows, Linux, and
macOS. The public interface supplies:

- `Install(const char* app_name, const char* version, const char* crash_log_dir)`;
- `Uninstall()`;
- `SetPreCrashCallback(PreCrashFn, void*)`;
- `OnCrash(const char*, void*)`; and
- `OnAssertionFailure(const char*, int, const char*)`.

These declarations intentionally retain `const char*`: they are the shipped public
signature, so changing them in documentation to `cstring` would misrepresent the API.
New engine-owned C++ interfaces should prefer `cstring` when it is a compatible
choice.

`ZENGINE_VALIDATE_ASSERT` routes release/RelWithDebInfo assertions to
`OnAssertionFailure`; debug builds log and break instead. The handler permits one
pre-crash callback with caller-owned context and documents a two-second timeout. It must
be installed before engine subsystems and uninstalled during orderly shutdown.

## Safety boundary

Crash/signal/exception contexts are severely constrained. The pre-crash callback is
not normal application code: it must not allocate from engine memory or trigger
`ZENGINE_VALIDATE_ASSERT`. New crash-path code must be audited per platform for
async-signal/exception safety, reentrancy, bounded storage, and guaranteed termination.
Do not add VFS, renderer, ECS, network, or arbitrary logging operations to that path
without proving their safety.

## Production completion gates

- Exercise assertion, fatal signal/exception, reentrancy, missing/unwritable crash
  directory, and callback-timeout paths on every supported platform.
- Version and retain reports according to a product-owned privacy/consent/retention
  policy. Upload is not implied by the local crash handler.
- Test symbol collection, symbol-server access, report grouping, and release-build
  diagnostics in CI and in the actual packaging environment.
- Keep crash-path dependencies small and validate every SDK/platform upgrade against
  the failure-path tests.
