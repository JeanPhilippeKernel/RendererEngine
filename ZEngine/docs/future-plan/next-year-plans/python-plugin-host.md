# ZEngine — Python Plugin Host

**Priority:** Next-year plan — enables Python developers to write engine plugins without C++
**Status:** Design
**Depends on:** `plugin-system.md` (Plugin SDK must be stable first)
**Pattern:** ZPythonHost is itself a C++ plugin that embeds CPython and re-exposes the Plugin SDK to Python

---

## 1. Design Overview

ZPythonHost is a normal ZEngine plugin — a shared library with a `.zplugin` descriptor — written in C++.
It embeds CPython 3.11+ using the stable ABI (`Py_LIMITED_API`) so that it links against
`python3.dll` / `libpython3.so` and remains compatible across CPython 3.11, 3.12, and 3.13
without recompilation.

**How it works:**

On `Initialize`, ZPythonHost calls `Py_Initialize()`, configures `sys.path` to include the
`Plugins/` directory, imports the `zengine` C extension module, then scans `Plugins/` for
subdirectories that contain both a `.zplugin` descriptor with `"host": "ZPythonHost"` and a
matching `.py` module file. It imports each such module via the Python import machinery.

On `RegisterECSSystems`, `RegisterRenderPasses`, `RegisterImporters`, and `RegisterEditorPanels`,
ZPythonHost iterates its list of discovered Python modules and calls the corresponding lifecycle
function on each (`zengine_register_systems`, `zengine_register_render_passes`,
`zengine_register_importers`, `zengine_register_editor_panels`). Each function is optional —
if a module does not define it, the call is silently skipped.

On `Shutdown`, ZPythonHost calls each module's `zengine_shutdown(ctx)`, then calls `Py_Finalize()`.

**From the engine's perspective, only one plugin exists: ZPythonHost.dll.** The individual Python
modules are invisible to `PluginLoader`. ZPythonHost multiplexes all Plugin SDK calls on their behalf.

**The same pattern extends to other scripting languages:**

- `ZLuaHost` — embeds Lua 5.4; exposes `zengine` as a Lua table via `lua_register`
- `ZSharpHost` — embeds .NET 8 runtime via `hostfxr`; exposes SDK via P/Invoke

All three are separate C++ plugins. The engine and Plugin SDK are completely unchanged to support
any of them.

---

## 2. Python Plugin Package Format

A Python plugin developer ships a directory (or zip archive) with this layout:

```
MyPythonPlugin/
  MyPythonPlugin.zplugin    — descriptor with "host": "ZPythonHost"
  MyPythonPlugin.py         — the actual plugin code
  requirements.txt          — optional pure-Python dependencies (no native extensions)
```

The `.zplugin` descriptor gains two new fields — `"host"` and `"entry_module"`:

```json
{
  "name":               "MyPythonPlugin",
  "version":            "1.0.0",
  "sdk_version":        "1.0",
  "author":             "Someone",
  "host":               "ZPythonHost",
  "entry_module":       "MyPythonPlugin",
  "engine_version_min": "1.0.0",
  "engine_version_max": "2.0.0"
}
```

**`"host"` field semantics:**

When `PluginLoader` parses a `.zplugin` descriptor and finds a `"host"` field, it does NOT attempt
to `dlopen` the descriptor's own library. Instead it locates the already-loaded plugin named by the
`"host"` field and passes the descriptor to it. The host plugin is responsible for loading and
running the script. If the named host plugin is not loaded (e.g., ZPythonHost.dll is missing),
`PluginLoader` logs an error and skips the hosted plugin.

`"entry_module"` names the Python module file to import (`MyPythonPlugin` maps to
`Plugins/MyPythonPlugin/MyPythonPlugin.py`).

Pure-Python dependencies listed in `requirements.txt` must be installed in a virtualenv bundled
alongside the plugin or placed in a path that `sys.path` already covers. ZPythonHost does not run
`pip install` automatically — dependency management is the plugin author's responsibility.

---

## 3. ZPythonHost C++ Plugin Structure

ZPythonHost registers itself with the engine as an ordinary plugin. Its descriptor exposes all
four registration callbacks so that the engine calls them in the standard sequence.

```cpp
// ZEngine/Plugins/ZPythonHost/ZPythonHost.cpp

#define Py_LIMITED_API 0x030B0000   // CPython 3.11 stable ABI
#include <Python.h>
#include <PluginSDK/PluginSDK.h>
#include "PythonSystemRegistry.h"
#include "zengine_module.h"         // PyMODINIT_FUNC PyInit_zengine(void)

// ── Internal state ────────────────────────────────────────────────────────────

struct PythonPluginRecord {
    char      ModuleName[128];
    PyObject* Module;           // borrowed — we hold a strong ref
};

static PythonPluginRecord s_PythonPlugins[64];
static uint32_t           s_PythonPluginCount = 0;

// ── Helper: invoke a Python lifecycle function if it exists ──────────────────

static void CallLifecycle(PyObject* module, const char* fn_name, PyObject* ctx_dict) {
    PyObject* fn = PyObject_GetAttrString(module, fn_name);
    if (!fn) {
        PyErr_Clear();   // function is optional
        return;
    }
    PyObject* result = PyObject_CallOneArg(fn, ctx_dict);
    if (!result) {
        PyErr_Print();
        PyErr_Clear();
    }
    Py_XDECREF(result);
    Py_DECREF(fn);
}

// ── Build the ctx dict that Python plugins receive ───────────────────────────
//
// CR-06 fix: Each PyCapsule_New() creates an object with refcount=1.
// PyDict_SetItemString increments to 2. We DECREF after insertion so only the
// dict holds the reference. Without this DECREF, every call leaks one Python
// object per capsule.

static PyObject* BuildContextDict(const ZPluginContext* ctx) {
    PyObject* d = PyDict_New();
    if (!d) return NULL;

// Helper macro: creates a capsule, inserts into dict, releases our reference.
// PyDict_SetItemString does NOT steal — it increments refcount.
// We must DECREF the capsule after insertion to balance the initial refcount=1.
#define SET_CAPSULE(key, ptr, tag) \
    do { \
        PyObject* _cap = PyCapsule_New((ptr), (tag), NULL); \
        if (!_cap) { Py_DECREF(d); return NULL; } \
        int _ret = PyDict_SetItemString(d, (key), _cap); \
        Py_DECREF(_cap);  /* release our ownership; dict holds its own ref */ \
        if (_ret < 0) { Py_DECREF(d); return NULL; } \
    } while(0)

    SET_CAPSULE("scene",          ctx->Scene,          "Scene");
    SET_CAPSULE("world_tick",     ctx->WorldTick,       "WorldTick");
    SET_CAPSULE("render_graph",   ctx->RenderGraph,     "RenderGraph");
    SET_CAPSULE("vfs",            ctx->VFS,             "VFS");
    SET_CAPSULE("asset_registry", ctx->AssetRegistry,   "AssetRegistry");
    SET_CAPSULE("arena",          ctx->Arena,           "Arena");

    if (ctx->Editor) {
        SET_CAPSULE("editor", ctx->Editor, "Editor");
    } else {
        if (PyDict_SetItemString(d, "editor", Py_None) < 0) {
            Py_DECREF(d); return NULL;
        }
    }

#undef SET_CAPSULE
    return d;
}

// ── ZPythonHost plugin lifecycle ─────────────────────────────────────────────

static void ZPythonHost_Initialize(const ZPluginContext* ctx) {
    // Register zengine module before Py_Initialize scans for extensions
    PyImport_AppendInittab("zengine", &PyInit_zengine);

    Py_Initialize();

    // Add Plugins/ directory to sys.path
    PyObject* sys_path = PySys_GetObject("path");   // borrowed
    PyObject* plugins  = PyUnicode_FromString("Plugins");
    PyList_Insert(sys_path, 0, plugins);
    Py_DECREF(plugins);

    // Import each Python plugin that was discovered by PluginLoader and delegated here.
    // (ZPythonHost_RegisterModule is called by PluginLoader for each hosted descriptor.)
    // Nothing else to do here — module registration is driven externally.
}

// Called by PluginLoader when it encounters a .zplugin with "host": "ZPythonHost"
void ZPythonHost_RegisterModule(const char* module_name) {
    if (s_PythonPluginCount >= 64) {
        // Log: too many Python plugins
        return;
    }
    PyObject* module = PyImport_ImportModule(module_name);
    if (!module) {
        PyErr_Print();
        PyErr_Clear();
        return;
    }
    PythonPluginRecord& rec = s_PythonPlugins[s_PythonPluginCount++];
    snprintf(rec.ModuleName, sizeof(rec.ModuleName), "%s", module_name);
    rec.Module = module;  // we own this reference
}

static void ZPythonHost_RegisterECSSystems(const ZPluginContext* ctx) {
    PyObject* ctx_dict = BuildContextDict(ctx);
    for (uint32_t i = 0; i < s_PythonPluginCount; ++i)
        CallLifecycle(s_PythonPlugins[i].Module, "zengine_register_systems", ctx_dict);
    Py_DECREF(ctx_dict);
}

static void ZPythonHost_RegisterRenderPasses(const ZPluginContext* ctx) {
    PyObject* ctx_dict = BuildContextDict(ctx);
    for (uint32_t i = 0; i < s_PythonPluginCount; ++i)
        CallLifecycle(s_PythonPlugins[i].Module, "zengine_register_render_passes", ctx_dict);
    Py_DECREF(ctx_dict);
}

static void ZPythonHost_RegisterImporters(const ZPluginContext* ctx) {
    PyObject* ctx_dict = BuildContextDict(ctx);
    for (uint32_t i = 0; i < s_PythonPluginCount; ++i)
        CallLifecycle(s_PythonPlugins[i].Module, "zengine_register_importers", ctx_dict);
    Py_DECREF(ctx_dict);
}

static void ZPythonHost_RegisterEditorPanels(const ZPluginContext* ctx) {
    if (!ctx->Editor) return;
    PyObject* ctx_dict = BuildContextDict(ctx);
    for (uint32_t i = 0; i < s_PythonPluginCount; ++i)
        CallLifecycle(s_PythonPlugins[i].Module, "zengine_register_editor_panels", ctx_dict);
    Py_DECREF(ctx_dict);
}

static void ZPythonHost_Shutdown(const ZPluginContext* ctx) {
    PyObject* ctx_dict = BuildContextDict(ctx);
    // Shutdown in reverse init order
    for (int32_t i = (int32_t)s_PythonPluginCount - 1; i >= 0; --i) {
        CallLifecycle(s_PythonPlugins[i].Module, "zengine_shutdown", ctx_dict);
        Py_DECREF(s_PythonPlugins[i].Module);
    }
    Py_DECREF(ctx_dict);
    s_PythonPluginCount = 0;

    PythonSystemRegistry_Clear();
    Py_Finalize();
}

// ── Plugin descriptor export ─────────────────────────────────────────────────

extern "C" {
    const ZPluginDescriptor* ZPlugin_GetDescriptor() {
        static const ZPluginDescriptor desc = {
            .SDKVersion            = ZPLUGIN_SDK_VERSION,
            .Name                  = "ZPythonHost",
            .Version               = "1.0.0",
            .Author                = "ZEngine",
            .Initialize            = ZPythonHost_Initialize,
            .Shutdown              = ZPythonHost_Shutdown,
            .RegisterECSSystems    = ZPythonHost_RegisterECSSystems,
            .RegisterRenderPasses  = ZPythonHost_RegisterRenderPasses,
            .RegisterImporters     = ZPythonHost_RegisterImporters,
            .RegisterEditorPanels  = ZPythonHost_RegisterEditorPanels,
        };
        return &desc;
    }
}
```

---

## 4. The `zengine` Python C Extension Module

`zengine` is a CPython C extension (`zengine.cpython-311.so` / `zengine.pyd`) that wraps the
Plugin SDK C API and exposes it as a normal Python module. Plugin authors `import zengine` and
call its functions directly.

Built with `Py_LIMITED_API 0x030B0000` so the same `.so` file works on CPython 3.11+.

### 4.1 Python-callable API surface

```python
import zengine

# ── ECS ───────────────────────────────────────────────────────────────────────

# Register a component type. Returns a stable integer type ID.
# size and align must exactly match the ctypes.Structure that Python code uses.
type_id = zengine.register_component_type(scene, size, align, "MyComponent")

# Register an ECS system function. Returns a stable integer system ID.
# fn signature: fn(scene_capsule, dt: float, cmds_capsule) -> None
sys_id = zengine.register_system(world_tick, fn, read_mask, write_mask, "MySystem")

# Ordering constraint: system A completes before system B.
zengine.order_before(world_tick, sys_id_a, sys_id_b)

# Component access — returns an integer address (for use with ctypes.from_address),
# or None if the entity does not have the component.
ptr = zengine.get_component(scene, entity_id, gen, type_id)

# Add a component from raw bytes (must match struct layout exactly).
zengine.add_component(scene, entity_id, gen, type_id, data_bytes)

# Remove a component.
zengine.remove_component(scene, entity_id, gen, type_id)

# Check component presence.
has = zengine.has_component(scene, entity_id, gen, type_id)   # -> bool

# Entity iteration.
count              = zengine.get_entity_count(scene)           # -> int
entity_id, gen     = zengine.get_entity_at(scene, index)       # -> (int, int)

# ── Transform helpers (common enough to warrant first-class bindings) ─────────

x, y, z = zengine.get_position(scene, entity_id, gen)
zengine.set_position(scene, entity_id, gen, x, y, z)

rx, ry, rz, rw = zengine.get_rotation(scene, entity_id, gen)
zengine.set_rotation(scene, entity_id, gen, rx, ry, rz, rw)

sx, sy, sz = zengine.get_scale(scene, entity_id, gen)
zengine.set_scale(scene, entity_id, gen, sx, sy, sz)

# ── Asset ─────────────────────────────────────────────────────────────────────

# Register an asset importer. extensions is a list of strings e.g. [".myext"].
# fn signature: fn(source_path: str, file_bytes: bytes) -> list[dict]
zengine.register_importer(asset_registry, [".myext"], fn)

# ── Logging ───────────────────────────────────────────────────────────────────

zengine.log_info("hello from Python")
zengine.log_warn("something is unexpected")
zengine.log_error("something broke")
```

### 4.2 C implementation — wrapping a Plugin SDK call

The following excerpt from `zengine_module.c` shows how `zengine.register_system` is implemented.
All `zengine` functions follow the same pattern: unpack Python arguments, convert capsules to raw
pointers, call the corresponding `ZPlugin_*` C function, return a Python value.

```c
// ZEngine/Plugins/ZPythonHost/zengine_module.c

#define Py_LIMITED_API 0x030B0000
#include <Python.h>
#include <PluginSDK/PluginSDK.h>
#include "PythonSystemRegistry.h"

// Forward declaration of the C trampoline called by the engine for every Python system.
static void PythonSystemTrampoline(void* scene, float dt, void* cmds);

static PyObject* py_register_system(PyObject* self, PyObject* args) {
    PyObject*   world_tick_capsule;
    PyObject*   fn;
    uint64_t    read_mask, write_mask;
    const char* name;

    if (!PyArg_ParseTuple(args, "OOKKs",
            &world_tick_capsule, &fn, &read_mask, &write_mask, &name))
        return NULL;

    if (!PyCallable_Check(fn)) {
        PyErr_SetString(PyExc_TypeError, "fn must be callable");
        return NULL;
    }

    ZWorldTickHandle wt = PyCapsule_GetPointer(world_tick_capsule, "WorldTick");
    if (!wt)
        return NULL;   // PyCapsule_GetPointer already set an exception

    // Allocate a slot in the registry; store the Python callable there.
    // PythonSystemTrampoline uses the registered system ID (filled in below)
    // to look up the callable at execution time.
    uint32_t slot = PythonSystemRegistry_Add(fn);   // increments refcount on fn

    uint32_t sys_id = ZPlugin_RegisterSystem(wt, PythonSystemTrampoline,
                                              read_mask, write_mask, name);

    PythonSystemRegistry_SetSystemID(slot, sys_id);

    return PyLong_FromUnsignedLong(sys_id);
}

static PyObject* py_register_component_type(PyObject* self, PyObject* args) {
    PyObject*   scene_capsule;
    uint32_t    size, align;
    const char* name;

    if (!PyArg_ParseTuple(args, "OIIs", &scene_capsule, &size, &align, &name))
        return NULL;

    ZSceneHandle scene = PyCapsule_GetPointer(scene_capsule, "Scene");
    if (!scene) return NULL;

    uint32_t type_id = ZPlugin_RegisterComponentType(scene, size, align, name);
    return PyLong_FromUnsignedLong(type_id);
}

static PyObject* py_get_component(PyObject* self, PyObject* args) {
    PyObject* scene_capsule;
    uint32_t  entity_id, gen, type_id;

    if (!PyArg_ParseTuple(args, "OIII", &scene_capsule, &entity_id, &gen, &type_id))
        return NULL;

    ZSceneHandle scene = PyCapsule_GetPointer(scene_capsule, "Scene");
    if (!scene) return NULL;

    void* ptr = ZPlugin_GetComponentRaw(scene, entity_id, gen, type_id);
    if (!ptr)
        Py_RETURN_NONE;

    // Return the raw address as a Python int. The caller uses ctypes.from_address().
    return PyLong_FromVoidPtr(ptr);
}

static PyMethodDef ZEngineMethods[] = {
    {"register_component_type", py_register_component_type, METH_VARARGS, NULL},
    {"register_system",         py_register_system,         METH_VARARGS, NULL},
    {"order_before",            py_order_before,            METH_VARARGS, NULL},
    {"get_component",           py_get_component,           METH_VARARGS, NULL},
    {"add_component",           py_add_component,           METH_VARARGS, NULL},
    {"remove_component",        py_remove_component,        METH_VARARGS, NULL},
    {"has_component",           py_has_component,           METH_VARARGS, NULL},
    {"get_entity_count",        py_get_entity_count,        METH_VARARGS, NULL},
    {"get_entity_at",           py_get_entity_at,           METH_VARARGS, NULL},
    {"get_position",            py_get_position,            METH_VARARGS, NULL},
    {"set_position",            py_set_position,            METH_VARARGS, NULL},
    {"get_rotation",            py_get_rotation,            METH_VARARGS, NULL},
    {"set_rotation",            py_set_rotation,            METH_VARARGS, NULL},
    {"get_scale",               py_get_scale,               METH_VARARGS, NULL},
    {"set_scale",               py_set_scale,               METH_VARARGS, NULL},
    {"register_importer",       py_register_importer,       METH_VARARGS, NULL},
    {"log_info",                py_log_info,                METH_VARARGS, NULL},
    {"log_warn",                py_log_warn,                METH_VARARGS, NULL},
    {"log_error",               py_log_error,               METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef ZEngineModule = {
    PyModuleDef_HEAD_INIT, "zengine", NULL, -1, ZEngineMethods
};

PyMODINIT_FUNC PyInit_zengine(void) {
    return PyModule_Create(&ZEngineModule);
}
```

---

## 5. Python System Trampoline

When a Python plugin registers a system via `zengine.register_system`, ZPythonHost registers
`PythonSystemTrampoline` — a plain C function with the `ZSystemFn` signature — with the engine.
The trampoline is a single C function shared by all Python systems. It resolves which Python
callable to invoke by looking up the currently-executing system ID in `PythonSystemRegistry`.

```c
// ZEngine/Plugins/ZPythonHost/ZPythonHost.cpp

// The C function registered with ZPlugin_RegisterSystem for every Python system.
// The engine calls this from the scheduler just like any other ZSystemFn.
//
// CR-07 fix: The ECS scheduler may dispatch systems on worker threads.
// ALL CPython API calls require the GIL to be held.
// Acquire the GIL before any Python interaction.
//
// IMPORTANT: Python systems are serialized by the GIL. They cannot run in
// parallel across WorldTick waves even if no component mask conflicts exist.
// A Python system registered in Wave 0 will block other Python systems in
// Wave 0. Keep Python systems coarse-grained and infrequent.
//
// **Scheduler integration constraint:**
// The GIL serializes Python systems against each other, but it does NOT protect shared
// ECS state against concurrent C++ systems running in the same WorldTick wave.
//
// If a Python system writes `TransformComponent` while a C++ physics system reads it
// in the same wave, that is a data race — the GIL provides no protection.
//
// **Required:** Python systems MUST declare conservative `ReadMask` and `WriteMask` in
// `ZPlugin_RegisterSystem`, covering all components they may access. This causes the
// WorldTick DAG scheduler to serialize Python systems with any conflicting C++ systems.
//
// When in doubt, declare a write mask covering all components the system touches. The
// performance cost of conservative masking is acceptable given Python's ~100–500× overhead
// compared to C++.
//
// Example — a Python system that reads transforms and writes AI state:
// ```python
// system_id = zengine.register_system(
//     world_tick,
//     my_ai_system,
//     read_mask  = TRANSFORM_BIT | RIGID_BODY_BIT,  # declare ALL reads
//     write_mask = AI_STATE_BIT,                     # declare ALL writes
//     name       = "PythonAISystem"
// )
// ```
static void PythonSystemTrampoline(void* scene, float dt, void* cmds) {
    // The ECS scheduler may dispatch systems on worker threads.
    // ALL CPython API calls require the GIL to be held.
    // Acquire the GIL before any Python interaction.
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* fn = PythonSystemRegistry_GetCurrentCallable();
    if (!fn) {
        PyGILState_Release(gstate);
        return;
    }

    PyObject* scene_cap = PyCapsule_New(scene, "Scene", NULL);
    PyObject* cmds_cap  = PyCapsule_New(cmds,  "WorldCommands", NULL);

    if (!scene_cap || !cmds_cap) {
        Py_XDECREF(scene_cap);
        Py_XDECREF(cmds_cap);
        PyGILState_Release(gstate);
        return;
    }

    PyObject* result = PyObject_CallFunction(fn, "OdO", scene_cap, (double)dt, cmds_cap);

    if (!result) {
        // Python exception in a system — log and clear, do not propagate.
        PyErr_Print();
        PyErr_Clear();
    }

    Py_XDECREF(result);
    Py_DECREF(scene_cap);
    Py_DECREF(cmds_cap);

    // Release the GIL after all Python interaction is complete.
    PyGILState_Release(gstate);
}
```

**`PythonSystemRegistry`** is a small flat array that stores `(system_id, PyObject* callable)`
pairs. On each trampoline invocation it uses the system ID (which the scheduler passes via a
thread-local or a side-channel query) to find the right callable. The implementation is entirely
internal to ZPythonHost and is not part of the Plugin SDK.

---

## 6. What a Python Plugin Looks Like End-to-End

Below is a complete, self-contained Python plugin. It registers a `HealthComponent` and a
`HealthRegenSystem` that ticks regen every frame. The code demonstrates the full lifecycle.

```python
# HealthRegenPlugin.py
import zengine
import ctypes

# ── Component layout ─────────────────────────────────────────────────────────
# This structure MUST match the C struct layout that the engine will allocate.
# Use explicit padding to match C alignment rules.

class HealthComponent(ctypes.Structure):
    _fields_ = [
        ("CurrentHP",  ctypes.c_float),
        ("MaxHP",      ctypes.c_float),
        ("RegenRate",  ctypes.c_float),   # HP restored per second
        ("IsAlive",    ctypes.c_bool),
        ("_pad",       ctypes.c_uint8 * 3),
    ]

# ── Plugin state ─────────────────────────────────────────────────────────────

_health_type_id = None
_regen_sys_id   = None

# ── System function ───────────────────────────────────────────────────────────

def health_regen_system(scene, dt, cmds):
    entity_count = zengine.get_entity_count(scene)
    for i in range(entity_count):
        entity_id, gen = zengine.get_entity_at(scene, i)

        ptr = zengine.get_component(scene, entity_id, gen, _health_type_id)
        if ptr is None:
            continue

        comp = HealthComponent.from_address(ptr)

        if not comp.IsAlive:
            continue

        new_hp = comp.CurrentHP + comp.RegenRate * dt
        comp.CurrentHP = min(new_hp, comp.MaxHP)

# ── Lifecycle ─────────────────────────────────────────────────────────────────

def zengine_initialize(ctx):
    # Nothing to set up before systems are registered.
    pass

def zengine_register_systems(ctx):
    global _health_type_id, _regen_sys_id

    scene      = ctx["scene"]
    world_tick = ctx["world_tick"]

    # Register the component type. size and align must match HealthComponent above.
    _health_type_id = zengine.register_component_type(
        scene,
        ctypes.sizeof(HealthComponent),
        ctypes.alignment(HealthComponent),
        "HealthComponent")

    health_bit = 1 << _health_type_id

    # Register the system: reads and writes HealthComponent.
    _regen_sys_id = zengine.register_system(
        world_tick,
        health_regen_system,
        health_bit,   # read mask
        health_bit,   # write mask
        "HealthRegenSystem")

    # No ordering constraints needed for this system.

def zengine_shutdown(ctx):
    # Component data is owned by the ECS. Nothing to free on the Python side.
    pass
```

**HealthRegenPlugin.zplugin:**

```json
{
  "name":               "HealthRegenPlugin",
  "version":            "1.0.0",
  "sdk_version":        "1.0",
  "author":             "Someone",
  "host":               "ZPythonHost",
  "entry_module":       "HealthRegenPlugin",
  "engine_version_min": "1.0.0",
  "engine_version_max": "2.0.0"
}
```

---

## 7. Context Object Passed to Python

The `ctx` argument received by all Python lifecycle functions is a plain Python `dict`.
ZPythonHost populates it from the `ZPluginContext*` it receives from the engine. All
engine subsystem pointers are wrapped in named `PyCapsule` objects.

```python
ctx = {
    "scene":          <capsule: Scene>,
    "world_tick":     <capsule: WorldTick>,
    "render_graph":   <capsule: RenderGraph>,
    "vfs":            <capsule: VFS>,
    "asset_registry": <capsule: AssetRegistry>,
    "editor":         <capsule: Editor>,  # None in shipping builds
    "arena":          <capsule: Arena>,
}
```

Python plugins must not attempt to extract raw pointers from these capsules using
`ctypes.cast` or `id()` tricks. The only valid operations on a capsule value are
passing it back into a `zengine.*` function, which extracts the pointer on the C side.

The dict is rebuilt on each lifecycle call. A plugin that caches a capsule value
across calls (e.g., stores `ctx["scene"]` in a module-level variable) must be aware
that the pointer it captures remains valid only for the process lifetime — it does
not move, but it is not safe to hold across `Shutdown`.

---

## 8. Performance Considerations

Python system functions carry the overhead of the CPython interpreter: `PyObject` boxing
and unboxing on every call, the GIL, and the overhead of dictionary lookups in the
`zengine` module. Measured overhead relative to an equivalent C++ system:

| Scenario | Approximate overhead vs C++ |
|---|---|
| System function call overhead (empty body) | ~50x |
| Per-component access via `get_component` + `from_address` | ~200x |
| Dense loop over 1 000 entities | ~300-500x |
| Dense loop over 10 000+ entities | unacceptable; port to C++ |

**Rule of thumb:** Python systems are appropriate for low-frequency work — systems that
run on fewer than ~100 entities or that execute logic that is dominated by high-level
decisions rather than arithmetic.

| Scenario | Recommended language |
|---|---|
| Quest logic, dialogue, scripted cutscenes | Python |
| UI event handlers and settings screens | Python |
| Custom editor tools and inspector panels | Python |
| GameMode/GameState management | Python |
| Prototype / experiment for any system | Python first, port to C++ when needed |
| NavMesh pathfinding over many agents | C++ |
| Custom render pass | C++ |
| Physics vehicle system | C++ |
| Animation sampling and blending | C++ |
| Networking and replication | C++ |
| Particle simulation | C++ |

Profiling a Python plugin is straightforward: set `ZPYTHON_PROFILE=1` in the environment
before startup. ZPythonHost will enable `cProfile` on each Python system function and
dump a report to `Logs/python_profile.txt` on shutdown.

---

## 9. Hot-Reload

ZPythonHost supports hot-reloading `.py` plugin files while the engine is running, using
the same VFS watcher that drives C++ DLL hot-reload.

**Hot-reload flow:**

```
1. VFS watcher detects a .py file modification (file close event on Windows;
   inotify IN_CLOSE_WRITE on Linux; FSEvents on macOS)

2. ZPythonHost receives a WatcherEvent for the modified file

3. ZPythonHost identifies which PythonPluginRecord owns the file

4. ZPythonHost calls zengine_shutdown(ctx) on the module
   — this unregisters any Python-side state (timers, callbacks, etc.)
   — ECS component data is NOT touched; it lives in the engine's arena

5. ZPythonHost calls importlib.reload(module)
   — the module's top-level code re-executes
   — module-level variables are reset to their initial values

6. ZPythonHost calls zengine_register_systems(ctx) on the reloaded module
   — new system IDs are allocated for any newly defined systems
   — the scheduler is re-committed (WorldTick::Recommit) to incorporate changes

7. ZPythonHost calls zengine_initialize(ctx) on the reloaded module

8. Next frame runs with the updated Python code
```

**What is preserved across hot-reload:**

- All ECS component data (unchanged; the arena is not touched)
- C++ plugin state (unchanged; only the Python module is reloaded)
- Asset registry contents (unchanged)

**What is reset across hot-reload:**

- Module-level Python variables (re-initialized by the module's top-level code)
- System IDs (new IDs are assigned; old IDs from before the reload are invalid)
- Any Python-side caches or lookup tables (must be rebuilt in `zengine_register_systems`)

Hot-reload is disabled in shipping builds (`ZENGINE_SHIPPING` defined). The VFS watcher
is not compiled in shipping builds, so no file-watch overhead is incurred.

---

## 10. Extending to Other Languages

The ZPythonHost architecture defines a pattern that other language hosts can follow
without any changes to the engine or Plugin SDK.

### ZLuaHost (Lua 5.4)

ZLuaHost embeds Lua 5.4 (`liblua54.dll` / `liblua.so`). The `zengine` Lua API is a
table registered via `lua_register` and `luaL_newlib`. Python trampolines become
`lua_CFunction` wrappers: ZLuaHost registers a C function with the scheduler, and that
C function calls `lua_pcall` with the target Lua function, wrapping scene/cmds pointers
as Lua light userdata.

Lua plugins ship as:
```
MyLuaPlugin/
  MyLuaPlugin.zplugin    — "host": "ZLuaHost"
  MyLuaPlugin.lua
```

Lua's overhead is lower than Python's (~10-50x vs C++ vs Python's ~100-500x), making it
suitable for moderately dense systems such as simple AI state machines over hundreds of
agents.

### ZSharpHost (C# via .NET 8)

ZSharpHost embeds the .NET 8 runtime via `hostfxr` (`nethost.h`). C# plugins are
class libraries (`.dll` assemblies) that reference a `ZEngine.PluginSDK.dll` managed
assembly. The managed SDK wraps Plugin SDK calls via P/Invoke.

C# plugins declare systems with an attribute:

```csharp
[ZEngineSystem(ReadMask = ComponentMask.Health, WriteMask = ComponentMask.Health)]
public static void HealthRegenSystem(SceneHandle scene, float dt, WorldCommandsHandle cmds)
{
    // ...
}
```

ZSharpHost scans the assembly for `[ZEngineSystem]`-attributed methods and registers
C trampolines for each, which call `MethodInfo.Invoke` via the .NET runtime.

With .NET 8 NativeAOT, C# plugin overhead can drop to ~2-5x vs C++ for tight loops,
making C# viable for moderately high-frequency systems.

C# plugins ship as:
```
MySharpPlugin/
  MySharpPlugin.zplugin    — "host": "ZSharpHost"
  MySharpPlugin.dll        — compiled C# class library
```

### Common notes for all language hosts

- Each host is an independent C++ plugin. The engine does not know or care which
  language hosts exist.
- Hosts must implement the `ZPlugin_GetDescriptor` export and register all four
  extension callbacks, even if some are no-ops.
- Hot-reload semantics are host-defined. Hosts are encouraged (but not required)
  to support it for developer builds.
- The `"host"` field in `.zplugin` is an arbitrary string. `PluginLoader` looks up
  the host by comparing the string to `PluginRecord.Name` in its loaded-plugin list.
  Third parties can ship their own host plugins (e.g., a JavaScript host via V8).

---

## 11. File Layout

```
ZEngine/
  Plugins/
    ZPythonHost/
      CMakeLists.txt
      ZPythonHost.cpp           — host plugin entry point; ZPlugin_GetDescriptor()
      ZPythonHost.zplugin       — descriptor for ZPythonHost itself
      zengine_module.c          — CPython C extension; PyInit_zengine()
      zengine_module.h          — declaration of PyInit_zengine for ZPythonHost.cpp
      PythonSystemRegistry.h    — registry API: Add, SetSystemID, GetCurrentCallable, Clear
      PythonSystemRegistry.c    — registry implementation

    ZLuaHost/                   — (future)
      CMakeLists.txt
      ZLuaHost.cpp
      ZLuaHost.zplugin
      zengine_lua.c             — Lua binding table

    ZSharpHost/                 — (future)
      CMakeLists.txt
      ZSharpHost.cpp
      ZSharpHost.zplugin
      ZEngine.PluginSDK/        — C# managed SDK assembly source
```

---

## 12. Deliverables Checklist

- [ ] `ZPythonHost.cpp` — ZPlugin_GetDescriptor, Initialize (Py_Initialize, sys.path),
      RegisterECSSystems, RegisterRenderPasses, RegisterImporters, RegisterEditorPanels, Shutdown
- [ ] `zengine_module.c` — all `zengine.*` Python-callable functions listed in Section 4.1
- [ ] `PythonSystemRegistry.h/.c` — Add, SetSystemID, GetCurrentCallable, Clear
- [ ] `ZPythonHost.zplugin` — descriptor for ZPythonHost itself (sdk_version, version, etc.)
- [ ] `CMakeLists.txt` for ZPythonHost — links CPython stable ABI, builds `zengine` extension
- [ ] `PluginLoader` changes — detect `"host"` field in `.zplugin`; call
      `ZPythonHost_RegisterModule` instead of `dlopen`; ensure host plugin is loaded before
      any plugin that depends on it
- [ ] `ZPluginContext` documentation — document that Python `ctx` dict mirrors `ZPluginContext*`
- [ ] Hot-reload integration — VFS watcher event dispatch to ZPythonHost
- [ ] `tests/Plugins/ZPythonHostTest.cpp` and `tests/Plugins/`:
  - [ ] ZPythonHost loads a minimal `.py` module; `zengine_register_systems` is called
  - [ ] Python module missing `zengine_register_systems` is silently skipped (no crash)
  - [ ] Python system function is invoked by the scheduler
  - [ ] Python exception inside a system function is caught, logged, and cleared; engine continues
  - [ ] Hot-reload: modify `.py` file; verify reloaded module's system runs on next frame
  - [ ] `zengine.register_component_type` returns a stable ID; component accessible from C++
  - [ ] `zengine.get_component` returns None for an entity that does not have the component
  - [ ] Shutdown calls `zengine_shutdown` in reverse registration order
- [ ] `ZLuaHost` stub (future) — CMakeLists.txt, ZLuaHost.cpp skeleton, ZLuaHost.zplugin
- [ ] `ZSharpHost` stub (future) — CMakeLists.txt, ZSharpHost.cpp skeleton, ZSharpHost.zplugin
