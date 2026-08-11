# Panzerfaust — Project Launcher

**Priority:** P1 — Already exists and is live; doc captures current state + planned improvements  
**Status:** Partially implemented (core flow works; planned features below)  
**Technology:** C# + .NET 8 + Avalonia (cross-platform desktop UI)  
**Location:** https://github.com/JeanPhilippeKernel/ZodiacEngineHub

---

## 1. What Panzerfaust Is

Panzerfaust is the **project manager and launcher** — the first application users open.
It is the ZEngine equivalent of Unreal Engine's Project Browser or Unity Hub.

It does three things:
1. **Create** a new game project — scaffolds directory structure and writes `projectConfig.json`
2. **Open** an existing project — launches Obelisk with the correct arguments
3. **Delete** a project from the registry

Panzerfaust itself is a thin .NET/Avalonia GUI. It has no engine dependency and no
C++ code. It communicates with the engine exclusively by launching `Obelisk.exe` as
a child process with CLI arguments.

---

## 2. Current Implementation

### 2.1 Launch flow

```
User opens Panzerfaust
  │
  ├─ Shows project list (loaded from Cache/*.pzf files)
  ├─ User clicks "Open" or "New Project"
  │
  ├─ New project:
  │   → Prompts for name + directory
  │   → Scaffolds project structure (§3)
  │   → Writes projectConfig.json (§4)
  │   → Saves project metadata to Cache/<SHA256>.pzf
  │   → Launches Obelisk
  │
  └─ Open project:
      → Launches Obelisk with existing projectConfig.json path

Obelisk launch command:
  Obelisk --launchEditor 1 --projectConfigFile /path/to/projectConfig.json
```

### 2.2 EngineService — how Obelisk is launched

```csharp
// Service/EngineService.cs
const string _launcherCLIAppName = "Obelisk";
const string _configJsonFilename  = "projectConfig.json";

public async Task StartAsync(string projectPath) {
    var args = new List<string> {
        "--launchEditor 1",
        "--projectConfigFile",
        Path.Combine(projectPath, _configJsonFilename)
    };
    var psi = new ProcessStartInfo(_enginePath, string.Join(" ", args)) {
        UseShellExecute = false,
        WorkingDirectory = Environment.CurrentDirectory
    };
    Process.Start(psi);
}
```

Panzerfaust and Obelisk must live in the same directory. `EngineService` resolves
`Obelisk.exe` / `Obelisk` from `Environment.CurrentDirectory`.

### 2.3 Project registry

Projects are stored as `.pzf` (JSON) files in `./Cache/`:
```json
{
  "Name": "MyGame",
  "Fullpath": "/Users/dev/projects/MyGame",
  "CreationDate": "2026-06-25T10:00:00",
  "UpdateDate": "2026-06-25T14:30:00"
}
```
The file is named `<SHA256(Name)>.pzf` to avoid collisions. ProjectService manages
CRUD on these files.

---

## 3. Project Directory Structure (scaffolded at creation)

When a new project is created, Panzerfaust writes this structure to the chosen path:

```
MyGame/
  projectConfig.json     ← engine entry point config
  Scenes/                ← .zscene files (authored in Tetragrama)
  SceneData/             ← baked scene data
  Imported/
    textures/            ← imported .png/.jpg/.hdr (raw source)
    sounds/              ← imported .ogg/.wav
  Scripts/               ← game .lua files (designer layer)
  Source/                ← game .cpp/.h (programmer layer, compiled to game DLL)
  CookedAssets/          ← output of cook pipeline (gitignored)
  Cache/                 ← Panzerfaust project metadata (gitignored)
```

`Scripts/` and `Source/` are the two working directories for the two-layer game
development model (designers in Lua, programmers in C++).

---

## 4. `projectConfig.json` Format

Written by Panzerfaust at project creation. Read by Obelisk at startup to configure
the engine and set working paths.

```json
{
  "projectName": "MyGame",
  "version": "1.0.0",
  "workingSpace": ".",
  "sceneDir": "$(workingSpace)/Scenes",
  "sceneDataDir": "$(workingSpace)/SceneData",
  "assetDirs": {
    "textureDir": "$(workingSpace)/Assets/Textures",
    "soundDir": "$(workingSpace)/Assets/Sounds",
    "meshDir": "$(workingSpace)/Assets/Meshes",
    "materialDir": "$(workingSpace)/Assets/Materials",
    "spriteDir": "$(workingSpace)/Assets/Sprites",
    "environmentMapDir": "$(workingSpace)/Assets/EnvironmentMaps"
  },
  "sky": {
    "mode": "atmosphere"
  },
  "sceneList": [
    { "name": "Default", "isDefault": true }
  ]
}
```

Obelisk reads this file, passes `workingSpace` as `GameApplication::WorkingSpacePath`,
and passes the config path as `GameApplication::ConfigFile`. The VFS mounts
`workingSpace` as the root for all asset resolution. All paths using the
`$(workingSpace)` token are expanded to absolute paths at parse time.

### 4.1 `buildCommand` and `gameDllPath` fields

```json
{
  "projectName": "MyGame",
  "version": "1.0.0",
  "workingSpace": ".",
  "sceneDir": "$(workingSpace)/Scenes",
  "sceneDataDir": "$(workingSpace)/SceneData",
  "assetDirs": {
    "textureDir": "$(workingSpace)/Assets/Textures",
    "soundDir": "$(workingSpace)/Assets/Sounds",
    "meshDir": "$(workingSpace)/Assets/Meshes",
    "materialDir": "$(workingSpace)/Assets/Materials",
    "spriteDir": "$(workingSpace)/Assets/Sprites",
    "environmentMapDir": "$(workingSpace)/Assets/EnvironmentMaps"
  },
  "sky": {
    "mode": "atmosphere"
  },
  "sceneList": [
    { "name": "Default", "isDefault": true }
  ],
  "buildCommand": "cmake --build Source/build --target ZEngineGame --config Release",
  "gameDllPath":  "Source/build/Release/MyGame.dll",
  "schemaVersion": 1
}
```

`buildCommand` is the shell command the editor's Build button runs to produce the game DLL.
`gameDllPath` is the path the editor watches for DLL changes (relative to `workingSpace`).
`schemaVersion` allows Panzerfaust to migrate old project files when the schema evolves.

The Tetragrama Build button runs `buildCommand` as a subprocess, streams stdout/stderr to
the editor's log panel, and on success the VFS file watcher detects `gameDllPath` changed
and triggers `GameDLLLoader::Reload()`.

---

## 5. Planned Features

### 5.1 Project templates

Instead of always scaffolding a blank project, offer starter templates:

| Template | What it includes |
|---|---|
| Blank | Empty project, no scenes, no scripts |
| 3D Starter | Default scene with floor, directional light, camera. Physics enabled. |
| Multiplayer Starter | Networking configured, rollback template systems pre-registered |
| 2D Starter | Orthographic camera, 2D physics layer preset |

Templates are `.zip` archives shipped with the engine SDK. Panzerfaust extracts the
chosen template into the new project directory before writing `projectConfig.json`.

### 5.2 Engine version selector

When multiple engine versions are installed, Panzerfaust lets the user choose which
version to open a project with. Each installed engine registers itself in a shared
manifest at a well-known path:

- Windows: `%LOCALAPPDATA%\ZEngine\installations.json`
- macOS: `~/Library/Application Support/ZEngine/installations.json`
- Linux: `~/.local/share/ZEngine/installations.json`

`EngineService` reads this manifest and launches the selected version's `Obelisk`
binary instead of the one in `Environment.CurrentDirectory`.

### 5.3 Plugin marketplace browser

Before opening a project, users can browse and install plugins from the store:

```
Panzerfaust Plugin Browser:
  Search box + category filter
  Install button → calls StoreClient REST API → downloads .dll + .zplugin to
  <project>/Plugins/ → available immediately when editor opens
```

This requires the `StoreClient` from `plugin-store.md` to be available as a
standalone .NET library or called via HTTP from Panzerfaust directly.

### 5.4 Recent projects with metadata

Show last-opened timestamp, engine version, and project size alongside each project.
Highlight projects with pending plugin updates.

### 5.5 First-launch setup wizard — compiler detection

When Panzerfaust runs for the first time on a machine, it checks whether the required
build toolchain is available. Engine programmers need a C++ compiler to build their game
DLL; designers who only write Lua never need this.

**Setup wizard flow:**

```
First launch detected (no ~/.zengine/toolchain.json)
  │
  ├─ Scan for installed compilers:
  │   Windows:  VSWHERE → find Visual Studio Build Tools / VS 2019/2022
  │   macOS:    xcode-select --print-path → confirm clang is installed
  │   Linux:    which g++ / which clang++
  │
  ├─ If found:
  │   "Found: Visual Studio 2022 (MSVC 19.37) — Build tools ready"
  │   [Continue]
  │
  ├─ If not found:
  │   "No C++ compiler detected."
  │   "You need this to build game logic (C++ DLL)."
  │   "Lua scripting works without it."
  │   [Install Visual Studio Build Tools (free)] → opens browser
  │   [Skip — I only write Lua]
  │
  └─ Save result to ~/.zengine/toolchain.json:
      {
        "compiler": "msvc",
        "version": "19.37",
        "path": "C:/Program Files/Microsoft Visual Studio/2022/BuildTools"
      }
```

Panzerfaust uses the saved toolchain to pre-fill the `buildCommand` in `projectConfig.json`
when creating a new project. The engine programmer never types a build command — it is
generated from the detected toolchain.

**Generated `buildCommand` per platform:**

| Platform + Compiler | Generated buildCommand |
|---|---|
| Windows + MSVC | `cmake --build Source/build --config Release --target ZEngineGame` |
| Windows + Clang | `cmake --build Source/build --config Release --target ZEngineGame` |
| macOS + Clang | `cmake --build Source/build --config Release --target ZEngineGame` |
| Linux + GCC | `cmake --build Source/build -j$(nproc) --target ZEngineGame` |

The `buildCommand` always uses CMake — but CMake is bundled in the SDK (see §5.6),
so the user only needs the compiler, not CMake.

The setup wizard also offers a "Reinstall / Change compiler" option in Panzerfaust's
settings panel for when developers upgrade their toolchain.

### 5.6 Bundled CMake in the SDK

The ZEngine SDK ships with CMake embedded. Engine programmers do not install CMake
separately.

```
ZEngineSDK/
  bin/
    Panzerfaust         (or .exe on Windows)
    Obelisk             (or .exe on Windows)
    cmake/
      bin/
        cmake           (or cmake.exe)
        cpack           (or cpack.exe)
      share/
        cmake-3.x/      (CMake modules)
  PluginSDK/
    include/
      PluginSDK.h
  Templates/
    ...
```

When Panzerfaust generates a `buildCommand`, it uses the bundled CMake:

```json
"buildCommand": "SDK/bin/cmake/bin/cmake --build Source/build --config Release"
```

Where `SDK/` is resolved relative to Panzerfaust's own executable path.

The bundled CMake binary is portable (self-contained, no system install required).
CMake is MIT-licensed and redistributable. Binary size is approximately 50 MB.

This means:
- Engine programmers install only a C++ compiler (MSVC / Clang / GCC).
- No "install CMake" step in the getting-started guide.
- The editor's Build button always works on a fresh machine after SDK install.

### 5.7 Ship wizard — Build → Cook → Package → Steam

The Ship wizard in Panzerfaust sequences the full release pipeline in one click. It is
the last step before players can download and play the game.

**Ship wizard flow:**

```
User clicks [Ship] in Panzerfaust (or via Tetragrama toolbar shortcut):

  ┌─────────────────────────────────────────────────────────────┐
  │  Ship Wizard                                                │
  │                                                             │
  │  Target platform:  [Windows ▼]  [Linux]  [macOS]           │
  │  Configuration:    [Release ▼]                              │
  │  Output directory: /Users/dev/MyGame/Dist/  [Browse]       │
  │                                                             │
  │  [x] Step 1: Build game DLL (Release)                      │
  │  [x] Step 2: Cook assets                                    │
  │  [x] Step 3: Package installer                             │
  │  [ ] Step 4: Upload to Steam  [requires steam credentials] │
  │                                                             │
  │  [Cancel]                              [Start Ship ▶]      │
  └─────────────────────────────────────────────────────────────┘

  On Start:
    Step 1 — Build:
      bundled cmake --build Source/build --config Release --target ZEngineGame
      → MyGame.dll (Release, optimized)

    Step 2 — Cook:
      ZCook --project projectConfig.json --platform PC_Vulkan --config Release
      → CookedAssets/output.pak  (BCn textures, .zscene, .spv, Lua, fonts)

    Step 3 — Package:
      bundled cmake --build --target package
      → Dist/MyGame_1.0_Setup.exe       (Windows NSIS)
      → Dist/MyGame_1.0_Win.zip         (Windows portable)
      → Dist/MyGame_1.0.AppImage        (Linux)
      → Dist/MyGame_1.0.dmg             (macOS)

    Step 4 (optional) — Upload to Steam:
      steamcmd +login <user> +run_app_build app_build.vdf
      → depot uploaded to Steam partner portal
      → build available in partner dashboard for review

  Progress shown in Panzerfaust log panel.
  Each step must succeed before the next starts.
  On failure: error shown with the exact command that failed.
```

**What the player installs (inside the package):**

```
MyGame/
  bin/
    Obelisk.exe        ← engine entry point (no --launchEditor at runtime)
    MyGame.dll         ← compiled game logic
  assets/
    output.pak         ← all cooked content

NOT included:
  Panzerfaust          ← launcher (development tool only)
  Tetragrama           ← editor (development tool only)
  PluginSDK/           ← headers (development tool only)
  Source/              ← C++ source code
  Scripts/             ← raw .lua files (cooked into .pak)
  Cache/               ← project registry
  cook.manifest        ← development artifact
  steam_appid.txt      ← NEVER shipped (CI check enforces this)
```

**Obelisk in shipping mode:**

When the player launches the game, Obelisk runs without `--launchEditor`. This
activates shipping mode:
- No Tetragrama editor systems registered
- No VFS file watcher (no hot-reload)
- VFSPakBackend mounts `output.pak` instead of the raw project directory
- Lua scripts loaded from the pak, not from `Scripts/` on disk
- No `projectConfig.json` needed at runtime (config is baked into the pak)

**Steam integration:**

Step 4 requires:
1. A Steam App ID (obtained from Steamworks partner portal)
2. `steam_appid.txt` present in the project root during development only
3. `app_build.vdf` depot manifest (Panzerfaust generates this from projectConfig.json)
4. `steamcmd` installed by the developer (not bundled — ~50 MB, separate install)

The Ship wizard generates the `app_build.vdf` automatically from:
- `projectConfig.json["projectName"]` → app name
- The selected platform → depot ID
- The output installer path → depot content path

---

## 6. Full system overview (updated)

```
┌──────────────────────────────────────────────────────────────┐
│  Panzerfaust (.NET/Avalonia)  ← users open this first        │
│  - Create / open / delete projects                           │
│  - Scaffold project structure + projectConfig.json           │
│  - Plugin marketplace browser (planned)                      │
│  - Engine version selector (planned)                         │
└─────────────────────┬────────────────────────────────────────┘
                      │ launches with CLI args
                      │ --launchEditor 1
                      │ --projectConfigFile path/to/config.json
┌─────────────────────▼────────────────────────────────────────┐
│  Obelisk (C++ .exe)  ← entry point, owns pre-engine init     │
│  - CrashHandler, MemoryManager, Logger, ThreadPool           │
│  - Reads projectConfig.json                                  │
│  - Creates Editor (Tetragrama) or Game app                   │
│  - Calls app->Initialize(arena) → engine starts              │
└─────────────────────┬────────────────────────────────────────┘
                      │ passes arena + config
┌─────────────────────▼────────────────────────────────────────┐
│  ZEngine Runtime (static lib)                                │
│  - ECS, Physics, Audio, Rendering, VFS, Networking           │
│  - Loads game DLL (MyGame.dll) via GameDLLLoader             │
│  - Loads Lua scripts via ZLuaHost plugin                     │
└─────────────────────┬────────────────────────────────────────┘
                      │ editor systems + game systems
┌─────────────────────▼────────────────────────────────────────┐
│  Tetragrama (C++)  ← the editor, runs on top of the engine   │
│  - Scene hierarchy, asset browser, play mode                 │
│  - Registers editor ECS systems via WorldTick                │
└──────────────────────────────────────────────────────────────┘
                      │ game DLL (programmer layer)
┌─────────────────────▼────────────────────────────────────────┐
│  MyGame.dll  ← C++ game logic, compiled by engine programmer │
│  - ZGame_RegisterSystems, Actor subclasses                   │
└──────────────────────────────────────────────────────────────┘
                      │ Lua scripts (designer layer)
┌─────────────────────▼────────────────────────────────────────┐
│  Scripts/*.lua  ← Lua game logic, written by designers       │
│  - Loaded by ZLuaHost, hot-reloaded on save                  │
└──────────────────────────────────────────────────────────────┘
```

---

## 7. Deliverables (planned features only — existing code is live)

- [ ] Project templates — Blank, 3D Starter, Multiplayer Starter
- [ ] Engine version selector — `installations.json` manifest + version picker UI
- [ ] Plugin marketplace browser — calls `StoreClient` REST API, downloads to `Plugins/`
- [ ] Recent projects metadata — last-opened, engine version, project size
- [ ] Project import — accept an existing `projectConfig.json` without recreating structure
- [ ] `.gitignore` generation — auto-exclude `CookedAssets/`, `Cache/`, `*.spv`
- [ ] `buildCommand` and `gameDllPath` fields added to `projectConfig.json` schema
- [ ] `schemaVersion` field added to `projectConfig.json` for future migration
- [ ] First-launch setup wizard — compiler detection + toolchain save to ~/.zengine/toolchain.json
- [ ] Auto-generate `buildCommand` in projectConfig.json based on detected compiler
- [ ] Bundle CMake binary in SDK package (cmake/bin/ alongside Obelisk and Panzerfaust)
- [ ] Build button in Tetragrama — runs buildCommand as subprocess, streams output to log panel
- [ ] Compiler not found: show install link + "Skip for Lua-only" option
- [ ] "Reinstall / Change compiler" option in Panzerfaust settings
- [ ] Ship wizard UI — platform selector, config selector, output directory picker
- [ ] Ship wizard: sequences Build → Cook → Package with per-step success gate
- [ ] Ship wizard: optional Steam upload step via steamcmd subprocess
- [ ] Ship wizard: generate `app_build.vdf` from projectConfig.json automatically
- [ ] `CPACK_IGNORE_FILES` enforced: no editor, PluginSDK, source, or raw scripts in package
- [ ] Obelisk shipping mode: no `--launchEditor` flag → mounts .pak, no editor systems
