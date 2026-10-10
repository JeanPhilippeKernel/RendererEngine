# Memory Allocator Audit — Bugs and UB

**Priority:** P0 — Must fix before any new code lands  
**Status:** All bugs fixed — see PRs #497, #531  
**Blocks:** ~~All phases of migration-plan.md~~ — unblocked  

**Files audited:**
- `ZEngine/Core/Memory/Allocator.h` / `Allocator.cpp`
- `ZEngine/Core/Memory/MemoryManager.h`
- `ZEngine/Helpers/MemoryOperations.h`

> **Scope note:** this audit covers the 16 bugs in the summary table below, found in the review
> that produced PRs #497/#531 — it is a closed, point-in-time report, not a standing guarantee
> that no further allocator bugs exist. Re-verified every one of the 16 against the current code
> (2026-09) — all still hold; no regressions. Later, unrelated bugs in the same files were found
> and fixed independently: alignment precondition + `AllocateNoZero` (#680, #683), `PoolAllocator`
> exhaustion/double-free asserts (#681, #697), Windows eager sub-arena commit (#728), and the
> `m_mem_page_size` 32-bit-on-Windows truncation that was the real root cause of a Windows startup
> crash (#731) — see
> [Memory Management](https://github.com/JeanPhilippeKernel/RendererEngine/wiki/Memory-Management)
> on the wiki for the current, maintained picture of the allocator.

---

## Summary Table

| # | Severity | Location | Class | Status |
|---|---|---|---|---|
| 1 | **Critical** | `ArenaAllocator::Allocate` | UB — `assert` in release is no-op, silent OOB write | Fixed — returns `nullptr` with null-check guard |
| 2 | **Critical** | `ArenaAllocator::Resize` | Silent data loss — returns `nullptr` on OOB resize | Fixed — falls through to slow path; asserts on OOM |
| 3 | **Critical** | `ArenaAllocator::Initialize` | No null-check on `malloc` — immediate UB if OOM | Fixed — switched to `mmap`/`VirtualAlloc` with null-check |
| 4 | **Critical** | `ArenaAllocator::Shutdown` | Double-free if called twice; `free` after `Clear` resets offsets but not pointer | Fixed — guards on `m_memory`; destructor calls `Shutdown()` |
| 5 | **Critical** | `PoolAllocator::Free` | Use-after-free is silently accepted — bounds check does not verify alignment | Fixed — asserts ownership and chunk alignment |
| 6 | **High** | `ArenaAllocator::Resize` — fast path | Silent truncation when `new_size < old_size` and pointer is last allocation | Fixed — zeroes freed tail on shrink |
| 7 | **High** | `PoolAllocator::Initialize` | Size reduced by alignment padding before `Allocate` — `Allocate` may re-pad, double-reducing usable size | Fixed — removed pre-subtraction; `Allocate` manages alignment |
| 8 | **High** | `ArenaAllocator::Clear` | `m_initial_current_offset` and `m_initial_previous_offset` are always 0 — `Clear` does not respect sub-arenas | Fixed — both fields set explicitly in `Initialize` and `CreateSubArena` |
| 9 | **High** | `CreateSubArena` + `Shutdown` | Sub-arena `Shutdown` calls `free` on memory it does not own | Fixed — `m_is_sub_arena` flag; `Shutdown` skips unmap for sub-arenas |
| 10 | **High** | `ZGetScratch` / `EndTempArena` | No nesting support — nested scratch arenas corrupt each other | Documented — non-re-entrant contract noted in `Allocator.h` header comment |
| 11 | **Medium** | `ArenaAllocator::Resize` — slow path | `secure_memmove` destination size is `old_size`, not `new_size` — copy truncated when growing | Fixed — passes `new_size` as `destSize` to `secure_memcpy` |
| 12 | **Medium** | `PoolAllocator::Clear` | Rebuild of free list does not zero memory — stale data readable after `Clear` | Fixed — `Clear` now zeros each chunk before rebuilding the free list |
| 13 | **Medium** | `is_power_of_two(0)` | Returns `true` — zero alignment silently accepted, triggers infinite loop in `memory_align` | Fixed — `(x != 0) && ((x & (x-1)) == 0)` |
| 14 | **Medium** | `MemoryManager::Shutdowm` | Typo in method name — `Shutdowm` not `Shutdown` | Fixed — renamed to `Shutdown` |
| 15 | **Low** | `ArenaAllocator::Allocate` | `m_initial_current_offset` never set in `Initialize` — `Clear` always resets to 0 regardless | Fixed — both `m_initial_*` fields set explicitly in `Initialize` |
| 16 | **Low** | `secure_strncpy` | Off-by-one: rejects `count == destSize - 1` (valid) because condition is `count >= destSize` | Fixed — `dest[count] = '\0'` ensures null termination |

---

## Bug 1 — Critical: `assert` in `Allocate` is a no-op in release builds

**Location:** `Allocator.cpp:26`

```cpp
assert((offset + size) <= m_total_size);
```

`assert` is compiled out when `NDEBUG` is defined (all release builds). An allocation
that exceeds the arena silently returns a pointer past the end of `m_memory`, writing
into unowned memory. This is undefined behavior and will corrupt adjacent heap data
with no diagnostic.

**Fix:** Replace with `ZENGINE_VALIDATE_ASSERT` (which calls `ZENGINE_CORE_CRITICAL` +
`ZENGINE_DEBUG_BREAK` and is not stripped in release):

```cpp
ZENGINE_VALIDATE_ASSERT((offset + size) <= m_total_size,
    "ArenaAllocator: out of memory — allocation exceeds arena capacity")
```

Also add an overflow guard — `offset + size` can wrap if both are large:

```cpp
ZENGINE_VALIDATE_ASSERT(size <= m_total_size - offset,
    "ArenaAllocator: out of memory — allocation exceeds arena capacity")
```

---

## Bug 2 — Critical: `Resize` returns `nullptr` silently on out-of-bounds

**Location:** `Allocator.cpp:41–76`

```cpp
void* ArenaAllocator::Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment)
{
    // ...
    else if ((m_memory <= old_mem) && old_mem < (m_memory + m_total_size))
    {
        if ((m_memory + m_previous_offset) == old_mem)
        {
            m_current_offset = m_previous_offset + new_size;
            if (m_current_offset <= m_total_size)
            {
                // ...
                return old_memory;
            }
            // ← falls through to the outer else branch? No — falls through to nullptr
        }
        else { ... }
    }
    return nullptr;  // ← caller has no idea this happened
}
```

When the fast-path resize (last allocation in-place grow) fails because
`m_previous_offset + new_size > m_total_size`, the function falls off the inner `if`
and returns `nullptr` without any diagnostic. The caller (`Array::Resize`,
`String::Resize`) receives a null pointer and proceeds to write through it.

**Fix:** Add an assert before returning `nullptr`, and handle the fallback case
explicitly — either fall through to the slow-path (new allocation + copy) or assert:

```cpp
if (m_current_offset <= m_total_size)
{
    // in-place grow succeeded
    ...
    return old_memory;
}
// in-place grow failed — fall through to slow path: new alloc + copy
auto new_mem = Allocate(new_size, alignment);
ZENGINE_VALIDATE_ASSERT(new_mem, "ArenaAllocator::Resize: arena out of memory")
size_t copy_size = old_size < new_size ? old_size : new_size;
Helpers::secure_memmove(new_mem, new_size, old_memory, copy_size);
return new_mem;
```

---

## Bug 3 — Critical: No null-check on `malloc` in `Initialize`

**Location:** `Allocator.cpp:8`

```cpp
m_memory = (uint8_t*) malloc(size);
```

`malloc` returns `nullptr` on failure (OOM or zero size). The code stores it into
`m_memory` without checking. Every subsequent `Allocate` call computes
`(uintptr_t) nullptr + offset` — arithmetic on a null pointer is undefined behavior.
On most platforms this produces address `offset` (typically < 4096), and the first
write causes a segfault, but it is not guaranteed.

**Fix:**

```cpp
m_memory = (uint8_t*) malloc(size);
ZENGINE_VALIDATE_ASSERT(m_memory != nullptr,
    "ArenaAllocator::Initialize: malloc failed — requested size may be too large")
ZENGINE_VALIDATE_ASSERT(size > 0,
    "ArenaAllocator::Initialize: size must be > 0")
```

---

## Bug 4 — Critical: Double-free in `Shutdown`

**Location:** `Allocator.cpp:14–18`

```cpp
void ArenaAllocator::Shutdown()
{
    Clear();
    free(m_memory);
}
```

`Shutdown` calls `Clear()` which resets offsets, then calls `free(m_memory)`. If
`Shutdown` is called a second time (e.g. in a destructor path or on error), `free` is
called on the same pointer twice — undefined behavior.

Additionally, the destructor `~ArenaAllocator() {}` does nothing, so a stack-allocated
`ArenaAllocator` that is initialized but goes out of scope without `Shutdown` being
called leaks the `malloc`'d buffer.

**Fixes:**

```cpp
void ArenaAllocator::Shutdown()
{
    if (m_memory)
    {
        Clear();
        free(m_memory);
        m_memory     = nullptr;
        m_total_size = 0;
    }
}

// Also fix the destructor to prevent leaks:
~ArenaAllocator()
{
    Shutdown();
}
```

**Note:** Sub-arenas must NOT call `Shutdown` — see Bug 9.

---

## Bug 5 — Critical: `PoolAllocator::Free` accepts dangling/misaligned pointers

**Location:** `Allocator.cpp:153–171`

```cpp
void PoolAllocator::Free(void* ptr)
{
    auto start = memory;
    auto end   = &memory[total_size];

    if (!(start <= ptr && ptr < end))
        return;   // silently ignores out-of-range

    PoolFreeNode* node = (PoolFreeNode*) (ptr);
    node->Next = head;
    head       = node;
}
```

Two problems:

**5a.** The bounds check only verifies that `ptr` is within the pool range. It does not
verify that `ptr` is aligned to `chunk_size` — i.e., that it actually points to the
start of a valid chunk. A misaligned pointer pushes a corrupted `PoolFreeNode` onto the
free list. The next `Allocate` returns that misaligned pointer, and the write to it
creates a misaligned access (UB on strict-alignment platforms) and corrupts the pool
structure.

**5b.** The function silently returns when `ptr` is out of range. This hides double-free
and wrong-pool-free bugs. It should assert.

**Fix:**

```cpp
void PoolAllocator::Free(void* ptr)
{
    ZENGINE_VALIDATE_ASSERT(ptr != nullptr, "PoolAllocator::Free: null pointer")

    auto start = (uintptr_t) memory;
    auto end   = (uintptr_t) memory + total_size;
    auto p     = (uintptr_t) ptr;

    ZENGINE_VALIDATE_ASSERT(p >= start && p < end,
        "PoolAllocator::Free: pointer not owned by this pool")
    ZENGINE_VALIDATE_ASSERT((p - start) % chunk_size == 0,
        "PoolAllocator::Free: pointer is not chunk-aligned — possible corruption or wrong pointer")

    PoolFreeNode* node = (PoolFreeNode*) ptr;
    node->Next = head;
    head       = node;
}
```

---

## Bug 6 — High: `Resize` fast path silently truncates data on shrink

**Location:** `Allocator.cpp:52–65`

```cpp
if ((m_memory + m_previous_offset) == old_mem)
{
    m_current_offset = m_previous_offset + new_size;
    if (m_current_offset <= m_total_size)
    {
        if (new_size > old_size)
        {
            // zero new bytes only when growing
        }
        return old_memory;
    }
}
```

When `new_size < old_size` (shrink), `m_current_offset` is set to
`m_previous_offset + new_size` — which moves the bump pointer backward, "freeing" the
tail of the last allocation. This is intentional for an arena. However, the bytes
between `new_size` and `old_size` are not zeroed. If the arena is later used for
another allocation, it will not see zeroed memory there because `Allocate` only zeros
the bytes from the new current_offset forward.

More importantly: if `new_size == 0`, `m_current_offset` is set to `m_previous_offset`,
meaning the previous offset and current offset are identical, making `previous_offset`
invalid for the next resize. This is a latent bug waiting to be triggered.

**Fix:** Assert `new_size > 0` in `Resize`. Zero the freed tail on shrink:

```cpp
ZENGINE_VALIDATE_ASSERT(new_size > 0, "ArenaAllocator::Resize: new_size must be > 0")

if (new_size < old_size) {
    // Zero the freed tail so it cannot be read as valid data
    Helpers::secure_memset(&m_memory[m_previous_offset + new_size], 0,
                           old_size - new_size, old_size - new_size);
}
```

---

## Bug 7 — High: `PoolAllocator::Initialize` double-reduces usable size

**Location:** `Allocator.cpp:111–131`

```cpp
uintptr_t initial_start = (uintptr_t) &arena->m_memory[arena->m_current_offset];
uintptr_t start         = Helpers::memory_align(initial_start, (uintptr_t) alignment);
size                   -= (size_t)(start - initial_start);   // ← reduce size by alignment padding

// ...
memory = (uint8_t*) arena->Allocate(size, alignment);        // ← Allocate re-aligns internally
```

`Allocate` internally calls `memory_align` again on `arena->m_current_offset`. If
`arena->m_current_offset` is already aligned (the common case), the padding computed
outside is non-zero but `Allocate` adds no padding — the `size` reduction was wrong.
If `arena->m_current_offset` is not aligned, `Allocate` adds padding internally, and
the size was correctly pre-reduced. The behavior depends on the arena's current state
at the time of the call, making this non-deterministic.

The safe pattern is to let `Allocate` manage alignment entirely and not pre-reduce `size`:

```cpp
void PoolAllocator::Initialize(Arena* arena, size_t size, size_t chk_size, size_t alignment)
{
    chk_size = Helpers::memory_align_size_t(chk_size, alignment);

    ZENGINE_VALIDATE_ASSERT(chk_size >= sizeof(PoolFreeNode), "Chunk size is too small")
    ZENGINE_VALIDATE_ASSERT(size >= chk_size, "Backing buffer length is smaller than chunk size")

    memory = (uint8_t*) arena->Allocate(size, alignment);
    ZENGINE_VALIDATE_ASSERT(memory != nullptr, "PoolAllocator::Initialize: allocation failed")

    total_size = size;
    chunk_size = chk_size;
    head       = nullptr;

    Clear();
}
```

---

## Bug 8 — High: `Clear` always resets to offset 0, ignoring sub-arena base

**Location:** `Allocator.cpp:78–82`

```cpp
void ArenaAllocator::Clear()
{
    m_previous_offset = m_initial_previous_offset;  // always 0
    m_current_offset  = m_initial_current_offset;   // always 0
}
```

`m_initial_current_offset` and `m_initial_previous_offset` are declared as `= 0` in the
header and are **never set by `Initialize`** (which only sets `m_current_offset` and
`m_previous_offset` to 0). They are also never set by `CreateSubArena`.

The design intent is clearly to reset the arena to its initial post-construction state.
But for a sub-arena, if the first allocation sits at offset 0 inside the sub-arena's
memory block, this is correct. The real problem is that `Clear` does not zero the
memory — it just resets the offset. Objects allocated from the arena that have
destructors are not destroyed; any pointers into the arena that callers still hold
become dangling without any signal. This is an acceptable arena trade-off but must be
documented: **callers must not use any pointer into the arena after `Clear`.**

Additionally `m_initial_*` are misleading names — they are always 0, not "the initial
value at the time the arena was set up." If a future use case needs a non-zero initial
offset (e.g. reserving a header at the front), these fields need to be set in
`Initialize`.

**Fix:** Set them explicitly in `Initialize`:

```cpp
void ArenaAllocator::Initialize(uint64_t size)
{
    ZENGINE_VALIDATE_ASSERT(size > 0, "ArenaAllocator::Initialize: size must be > 0")
    m_memory = (uint8_t*) malloc(size);
    ZENGINE_VALIDATE_ASSERT(m_memory != nullptr, "ArenaAllocator::Initialize: malloc failed")

    m_total_size              = size;
    m_current_offset          = 0;
    m_previous_offset         = 0;
    m_initial_current_offset  = 0;   // ← explicit
    m_initial_previous_offset = 0;   // ← explicit
}
```

---

## Bug 9 — High: Sub-arena `Shutdown` frees memory it does not own

**Location:** `Allocator.cpp:84–93` + `Allocator.cpp:14–18`

```cpp
void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena)
{
    out_arena->m_memory     = reinterpret_cast<uint8_t*>(Allocate(size));
    // ...
}
```

`out_arena->m_memory` points into the middle of the parent arena's `malloc`'d block.
`ArenaAllocator::Shutdown` calls `free(m_memory)`. If a sub-arena ever has `Shutdown`
called on it — or if the destructor is fixed to call `Shutdown` (Bug 4's fix) — it will
call `free` on a pointer that was not returned by `malloc`. This is undefined behavior
and will corrupt the allocator's internal state.

The current destructor `~ArenaAllocator() {}` is a no-op, which masks this. If Bug 4 is
fixed by making the destructor call `Shutdown`, this bug is immediately triggered for
every sub-arena.

**Fix:** Distinguish owned vs borrowed memory:

```cpp
struct ArenaAllocator
{
    // ...
    bool m_owns_memory = false;   // true only when Initialize() called malloc
};

void ArenaAllocator::Initialize(uint64_t size)
{
    m_memory      = (uint8_t*) malloc(size);
    m_owns_memory = true;
    // ...
}

void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena)
{
    out_arena->m_memory                = reinterpret_cast<uint8_t*>(Allocate(size));
    out_arena->m_total_size            = size;
    out_arena->m_current_offset        = 0;
    out_arena->m_previous_offset       = 0;
    out_arena->m_initial_current_offset  = 0;   // explicit — Clear() resets to this
    out_arena->m_initial_previous_offset = 0;   // explicit — Clear() resets to this
    out_arena->m_owns_memory           = false;  // does NOT own — parent does
}

void ArenaAllocator::Shutdown()
{
    if (m_memory && m_owns_memory)
    {
        free(m_memory);
    }
    m_memory      = nullptr;
    m_total_size  = 0;
    m_owns_memory = false;
    m_current_offset  = 0;
    m_previous_offset = 0;
}

~ArenaAllocator()
{
    Shutdown();
}
```

---

## Bug 10 — High: `ZGetScratch` / `EndTempArena` does not support nesting

**Location:** `Allocator.cpp:95–109`; used in `HashMap.h:257`, `VulkanDevice.cpp:71`,
`DeviceSwapchain.cpp:91`, `Shader.cpp:425`, `AppRenderPipeline.cpp:112`, and many more.

```cpp
ArenaTemp BeginTempArena(ArenaAllocator* arena)
{
    ArenaTemp temp      = {};
    temp.Arena          = arena;
    temp.PreviousOffset = arena->m_previous_offset;
    temp.CurrentOffset  = arena->m_current_offset;
    return temp;
}

void EndTempArena(ArenaTemp tmp)
{
    auto arena               = tmp.Arena;
    arena->m_previous_offset = tmp.PreviousOffset;
    arena->m_current_offset  = tmp.CurrentOffset;
}
```

`BeginTempArena` snapshots `m_current_offset`. `EndTempArena` restores it. This is
correct for a single scratch scope. But `HashMap.h:257` uses `ZGetScratch` on
`m_allocator`, and `VulkanDevice.cpp:497` calls `ZGetScratch` on the same arena twice
within the same function. When the outer `EndTempArena` runs, it restores
`m_current_offset` to the value before the inner scope, rolling back everything
including any allocations the inner scope placed that are still in use.

Example of the broken pattern in `VulkanDevice.cpp`:
```cpp
auto scratch = ZGetScratch(Arena);       // outer scratch: saves offset A
// ...
scratch      = ZGetScratch(Arena);       // reassigns scratch! saves offset B (after outer allocations)
// ...
ZReleaseScratch(scratch);                // restores offset B — outer allocations still live
// ...
ZReleaseScratch(scratch);               // called again — restores offset B again, no-op but misleading
```

The deeper problem: `scratch` is a value type. Reassigning it loses the outer snapshot.

**Fix:** The scratch arena system needs a nesting depth or a stack. Minimal fix:

```cpp
struct ArenaTemp
{
    ArenaAllocator* Arena          = nullptr;
    size_t          CurrentOffset  = 0;
    size_t          PreviousOffset = 0;
    bool            IsActive       = false;   // guard against double-release
};

void EndTempArena(ArenaTemp& tmp)    // pass by reference
{
    ZENGINE_VALIDATE_ASSERT(tmp.IsActive, "EndTempArena: scratch already released or never started")
    auto arena               = tmp.Arena;
    arena->m_previous_offset = tmp.PreviousOffset;
    arena->m_current_offset  = tmp.CurrentOffset;
    tmp.IsActive             = false;
}
```

For true nesting support, the arena needs a `scratch_depth` counter or the call sites
must use separate child arenas instead of re-using the same parent arena for nested
scratch scopes.

---

## Bug 11 — Medium: `Resize` slow path passes wrong destination size to `secure_memmove`

**Location:** `Allocator.cpp:68–70`

```cpp
auto   new_mem = Allocate(new_size, alignment);
size_t size    = old_size < new_size ? old_size : new_size;
Helpers::secure_memmove(new_mem, size, old_memory, size);  // ← destSize == copy size, not new_size
```

`secure_memmove(dest, destSize, src, count)` — `destSize` is meant to be the
destination buffer's total capacity, used to guard against count > destSize. Here
`destSize` is set to `min(old_size, new_size)` instead of `new_size`. When
`new_size > old_size`, `destSize == old_size`, which is smaller than the actual
destination capacity. `secure_memmove` will not overflow in this case (count == destSize),
but the protective intent of destSize is defeated — it cannot catch a future code change
that sets count > min(old_size, new_size).

**Fix:**

```cpp
auto   new_mem   = Allocate(new_size, alignment);
ZENGINE_VALIDATE_ASSERT(new_mem, "ArenaAllocator::Resize: arena out of memory")
size_t copy_size = old_size < new_size ? old_size : new_size;
Helpers::secure_memmove(new_mem, new_size, old_memory, copy_size);
```

---

## Bug 12 — Medium: `PoolAllocator::Clear` rebuilds free list without zeroing memory

**Location:** `Allocator.cpp:173–186`

```cpp
void PoolAllocator::Clear()
{
    auto   chunk_count = total_size / chunk_size;
    for (size_t i = 0; i < chunk_count; i++)
    {
        void*         ptr  = &memory[i * chunk_size];
        PoolFreeNode* node = (PoolFreeNode*) ptr;
        node->Next         = head;
        head               = node;
    }
}
```

`Clear` rebuilds the free list by writing `Next` pointers into each chunk but does not
zero the rest of the chunk data. After `Clear`, the next `Allocate` call returns a
chunk that still contains data from the previous user. `Allocate` does call
`secure_memset(node, 0, chunk_size, chunk_size)` after popping from the free list, so
data is zeroed at allocation time. However this means data is NOT zeroed after `Clear`,
only after the next `Allocate`. This is inconsistent with `ArenaAllocator::Allocate`
which zeros on allocation.

This is not a correctness bug as long as all users go through `Allocate`, but it is a
latent information-leak risk: if the pool is used from multiple threads and a `Clear` +
`Allocate` race is possible, stale data becomes visible.

**Fix:** Zero all memory in `Clear` before rebuilding the free list, or document
explicitly that `Clear` does not zero and callers must not rely on zeroed memory post-Clear.

---

## Bug 13 — Medium: `is_power_of_two(0)` returns `true`

**Location:** `MemoryOperations.h:161`

```cpp
inline bool is_power_of_two(uintptr_t x)
{
    return (x & (x - 1)) == 0;
}
```

For `x = 0`: `(0 & (0 - 1))` = `(0 & UINTPTR_MAX)` = `0` → returns `true`.

Zero is not a power of two. `memory_align` with `align = 0` computes `mod = p & (0 - 1)` =
`p & UINTPTR_MAX` = `p`, so `mod != 0` always (for `p > 0`), and `p += 0 - p` = 0.
This would return address 0 as the aligned pointer — a null pointer write.

The `ZENGINE_VALIDATE_ASSERT(is_power_of_two(alignment))` guard in `memory_align` passes
for alignment 0, making the assert useless.

**Fix:**

```cpp
inline bool is_power_of_two(uintptr_t x)
{
    return (x > 0) && ((x & (x - 1)) == 0);
}
```

---

## Bug 14 — Medium: `MemoryManager::Shutdowm` — typo, unreachable via correct spelling

**Location:** `MemoryManager.h:18`

```cpp
void Shutdowm()   // ← typo: 'Shutdowm' not 'Shutdown'
{
    Allocator.Shutdown();
}
```

Any caller trying to shut down the memory manager via `Shutdown()` would fail to
compile (no such method), and the typo version would silently never be called. The main
arena would leak. Once Bug 4's destructor fix is in place this becomes less severe, but
the typo should be fixed.

**Fix:** Rename to `Shutdown`.

---

## Bug 15 — Low: `m_initial_current_offset` never set in `Initialize`

**Location:** `Allocator.cpp:6–12`; `Allocator.h:36–37`

Both `m_initial_current_offset` and `m_initial_previous_offset` are defaulted to `0`
in the header and are never written by `Initialize`. `Clear` resets to these values.
This happens to be correct for the current usage (arenas always start at offset 0), but
it means the fields serve no purpose beyond `= 0`. They are misleadingly named. Either
set them explicitly in `Initialize` (see Bug 8 fix) or remove them and replace `Clear`
with a direct assignment to 0.

---

## Bug 16 — Low: `secure_strncpy` off-by-one — rejects valid copy length

**Location:** `MemoryOperations.h:84`

```cpp
if (destSize == 0 || count >= destSize)
{
    return MEMORY_OP_FAILURE;
}
```

This rejects `count = destSize - 1`, which is the maximum valid count (leaving room for
the null terminator). For example, copying 7 chars into an 8-byte buffer (`count=7`,
`destSize=8`) should succeed — `7 < 8` is true, but the condition `count >= destSize`
reads `7 >= 8` which is false, so this particular case does pass. The real problem is
`count == destSize - 1` — e.g. copying 7 chars into a 7-byte buffer (`count=6`,
`destSize=7`): `6 >= 7` is false, passes. Actually the logic is: it fails when
`count >= destSize`, meaning `count = destSize` would fail (no room for null). This is
technically correct.

However the `else` branch calls `std::strncpy(dest, src, count)` which does NOT null-
terminate if `src` is longer than `count`. The caller gets a non-null-terminated string
with no error indication. The `secure_strcpy` version handles this correctly by checking
`src_len + 1 > destSize`. `secure_strncpy` should do the same, or explicitly set
`dest[count] = '\0'` after the copy.

**Fix:**

```cpp
// After the strncpy call, always null-terminate:
std::strncpy(dest, src, count);
dest[count] = '\0';   // guarantee null termination regardless of src length
return MEMORY_OP_SUCCESS;
```

---

## Fix Priority Order

All bugs fixed as of PRs #497 and #531. Original priority order preserved for reference.

```
Immediate (before any new ECS/animation code uses the allocator): DONE
  Bug 1  — assert → ZENGINE_VALIDATE_ASSERT in Allocate
  Bug 3  — null-check malloc in Initialize (superseded: switched to mmap/VirtualAlloc)
  Bug 9  — m_is_sub_arena flag to prevent sub-arena free
  Bug 4  — guard free against double-call; safe destructor (depends on Bug 9)
  Bug 13 — is_power_of_two(0) returns true

Before threading / parallel systems: DONE
  Bug 10 — ZGetScratch nesting safety — documented as non-re-entrant in Allocator.h
  Bug 5  — PoolAllocator::Free alignment check

Before extended use: DONE
  Bug 2  — Resize silent nullptr return
  Bug 7  — PoolAllocator Initialize double-alignment reduction
  Bug 8  — Initialize sets m_initial_* explicitly
  Bug 11 — Resize slow path wrong destSize in memmove
  Bug 6  — Resize shrink does not zero freed tail
  Bug 14 — MemoryManager typo

Cleanup: DONE
  Bug 12 — PoolAllocator::Clear now zeros memory before rebuilding free list
  Bug 15 — m_initial_* fields set explicitly in Initialize and CreateSubArena
  Bug 16 — secure_strncpy null termination
```
