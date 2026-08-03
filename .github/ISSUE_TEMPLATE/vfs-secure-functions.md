---
name: Replace unsafe C functions with secure variants in VFS implementation
about: Follow-up to PR #530 - refactor VFS code to use safe memory operation helpers
title: "Replace unsafe C functions with secure variants in VFS implementation"
labels: ["good first issue"]
---

## Overview

This is a follow-up task to **PR #530** ([feat: implemented the foundation of VFS](https://github.com/JeanPhilippeKernel/RendererEngine/pull/530)) that refactors VFS code to use the engine's safe memory operation helpers instead of unsafe standard C functions.

## Context

The VFS implementation has been merged with blocking review feedback that requires systematic replacement of standard library functions with bounds-checked variants from `Helpers/MemoryOperations.h`. This improves code safety and consistency with the ZEngine codebase standards.

## What needs to be done

Replace all instances of unsafe C functions with their secure equivalents in two files. There are **~18 touch points** across both files.

### **File 1: `ZEngine/ZEngine/Core/VFS/VFSPath.cpp`** (8 replacements)

| Function | Count | Line ranges | Notes |
|----------|-------|-------------|-------|
| `secure_memcpy` | 5 | 28, 93, 260, 293, 349 | Add error handling checks for all calls |
| `secure_strlen` | 2 | 44, 293 | Most already correct; verify usage |
| `secure_memcmp` | 2 | 315, 336 | Use actual `m_length` instead of `VFS_MAX_PATH` |
| `secure_strcmp` | 1 | 324 | Add null validation at call site |
| **Buffer zero-init** | 2 | 28, 349 | Initialize all buffers as `char buf[SIZE] = {}` |

### **File 2: `ZEngine/ZEngine/Core/VFS/VFSDiskContext.cpp`** (7 replacements)

| Function | Count | Line ranges | Notes |
|----------|-------|-------------|-------|
| `secure_memcpy` | 3 | 103, 115, 121 | Check each call's return value for `MEMORY_OP_SUCCESS` |
| `secure_strlen` | 1 | 98 | Verify null-pointer handling |
| **Buffer zero-init** | 1 | 96 | Initialize root buffer with `= {}` |

## Implementation guidelines

1. **Check return values**: All `secure_*` functions return `int`:
   - `Helpers::MEMORY_OP_SUCCESS` (0) = operation succeeded
   - `Helpers::MEMORY_OP_FAILURE` (-1) = operation failed

2. **Zero-initialize buffers** on declaration:
   ```cpp
   char buffer[VFS_MAX_PATH] = {};  // Good ✓
   char buffer[VFS_MAX_PATH];        // Avoid ✗
   ```

3. **Use correct buffer sizes** in `secure_memcmp()`:
   - Don't use `VFS_MAX_PATH` if the actual string length is known
   - Use the real data size: `secure_memcmp(data, actual_len, other, actual_len, actual_len)`

4. **Handle errors appropriately**:
   - In path operations: return `VFSResult<...>::Fail(VFSError::InvalidPath)`
   - In I/O operations: return `VFSResult<...>::Fail(VFSError::IOError)`
   - In utility functions: return false or empty result on error

5. **Error handling pattern**:
   ```cpp
   int result = Helpers::secure_memcpy(dest, dest_size, src, src_len);
   if (result != Helpers::MEMORY_OP_SUCCESS)
   {
       // Handle error
       return VFSResult<T>::Fail(appropriate_error);
   }
   ```

## Resources

- **Helper functions API**: [ZEngine/Helpers/MemoryOperations.h](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/ZEngine/ZEngine/Helpers/MemoryOperations.h)
- **VFS Design Document**: [ZEngine/docs/vfs-design.md](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/ZEngine/docs/vfs-design.md)
- **PR Review Comments**: [GitHub PR #530 Full Discussion](https://github.com/JeanPhilippeKernel/RendererEngine/pull/530)

## Testing

After making changes, verify:

1. **Compile without warnings**:
   ```bash
   cd ZEngine && cmake --build . --config Debug
   ```

2. **VFS unit tests pass**:
   ```bash
   ./build/tests/ZEngineTests --gtest_filter="VFSPath*"
   ./build/tests/ZEngineTests --gtest_filter="VFSDiskFile*"
   ```

3. **No functional regression**: The VFS behavior should be identical before and after this refactor.

## Related PR

- **PR #530**: [feat: implemented the foundation of VFS](https://github.com/JeanPhilippeKernel/RendererEngine/pull/530)

## Estimated effort

**2–3 hours** (straightforward mechanical replacements with error handling additions)

## Acceptance criteria

- [ ] All 11 replacements completed
- [ ] All return values from `secure_*` functions are checked against `MEMORY_OP_SUCCESS`
- [ ] All buffers are zero-initialized on declaration
- [ ] Correct buffer sizes used in `secure_memcmp()` (actual length, not `VFS_MAX_PATH`)
- [ ] VFSPath unit tests pass without modification
- [ ] VFSDiskContext unit tests pass without modification
- [ ] Code compiles without warnings in Debug and Release modes
- [ ] No functional behavior change in VFS operations
- [ ] PR review comments from #530 are addressed
