#pragma once
#include <ZEngine/Logging/Logger.h>
#include <fmt/format.h>

#ifndef ZENGINE_LOG_CHANNEL_ENGINE
#define ZENGINE_LOG_CHANNEL_ENGINE 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_ECS
#define ZENGINE_LOG_CHANNEL_ECS 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_RENDER
#define ZENGINE_LOG_CHANNEL_RENDER 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_PHYSICS
#define ZENGINE_LOG_CHANNEL_PHYSICS 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_AUDIO
#define ZENGINE_LOG_CHANNEL_AUDIO 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_NETWORK
#define ZENGINE_LOG_CHANNEL_NETWORK 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_VFS
#define ZENGINE_LOG_CHANNEL_VFS 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_ASSET
#define ZENGINE_LOG_CHANNEL_ASSET 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_UI
#define ZENGINE_LOG_CHANNEL_UI 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_GAME
#define ZENGINE_LOG_CHANNEL_GAME 1
#endif

// Numeric level constants — used in compile-time comparisons below.
// These must match the LogLevel enum ordinals exactly; both are used to populate and
// compare against k_min_level[] in Logger::Log. Any mismatch causes runtime filtering
// to disagree with the compile-time guard. There is no DEBUG level (spdlog has one;
// ZEngine does not expose it).
#define ZENGINE_LOG_LEVEL_TRACE    0
#define ZENGINE_LOG_LEVEL_INFO     1
#define ZENGINE_LOG_LEVEL_WARN     2
#define ZENGINE_LOG_LEVEL_ERR      3
#define ZENGINE_LOG_LEVEL_CRITICAL 4

// Note: this is to help the comprehension the compile-time macro expansion
// if constexpr (ZENGINE_LOG_CHANNEL_##channel && (ZENGINE_LOG_LEVEL_##level >= ZENGINE_LOG_LEVEL_##channel))
//       => if constexpr (1 && (0 >= 1))  // channel=ENGINE, level=TRACE
// it expands in two passes:
// 1. ZENGINE_LOG_CHANNEL_##channel => ZENGINE_LOG_CHANNEL_ENGINE => 1
// 2. ZENGINE_LOG_LEVEL_##level => ZENGINE_LOG_LEVEL_TRACE => 0
#define ZENGINE_LOG(channel, level, ...)                                                                                                             \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if constexpr (ZENGINE_LOG_CHANNEL_##channel && (ZENGINE_LOG_LEVEL_##level >= ZENGINE_LOG_LEVEL_##channel))                                   \
        {                                                                                                                                            \
            ::ZEngine::Logging::Logger::Log(::ZEngine::Logging::LogChannel::channel, ::ZEngine::Logging::LogLevel::level, fmt::format(__VA_ARGS__)); \
        }                                                                                                                                            \
    } while (false); /* trailing ; matches old Logger::Xxx(...); expansion so existing call sites need no change */

#define ZENGINE_LOG_ENGINE_TRACE(...)    ZENGINE_LOG(ENGINE, TRACE, __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_INFO(...)     ZENGINE_LOG(ENGINE, INFO, __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_WARN(...)     ZENGINE_LOG(ENGINE, WARN, __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_ERR(...)      ZENGINE_LOG(ENGINE, ERR, __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_CRITICAL(...) ZENGINE_LOG(ENGINE, CRITICAL, __VA_ARGS__)

#define ZENGINE_LOG_ECS_TRACE(...)       ZENGINE_LOG(ECS, TRACE, __VA_ARGS__)
#define ZENGINE_LOG_ECS_INFO(...)        ZENGINE_LOG(ECS, INFO, __VA_ARGS__)
#define ZENGINE_LOG_ECS_WARN(...)        ZENGINE_LOG(ECS, WARN, __VA_ARGS__)
#define ZENGINE_LOG_ECS_ERR(...)         ZENGINE_LOG(ECS, ERR, __VA_ARGS__)

#define ZENGINE_LOG_RENDER_TRACE(...)    ZENGINE_LOG(RENDER, TRACE, __VA_ARGS__)
#define ZENGINE_LOG_RENDER_INFO(...)     ZENGINE_LOG(RENDER, INFO, __VA_ARGS__)
#define ZENGINE_LOG_RENDER_WARN(...)     ZENGINE_LOG(RENDER, WARN, __VA_ARGS__)
#define ZENGINE_LOG_RENDER_ERR(...)      ZENGINE_LOG(RENDER, ERR, __VA_ARGS__)

#define ZENGINE_LOG_PHYSICS_INFO(...)    ZENGINE_LOG(PHYSICS, INFO, __VA_ARGS__)
#define ZENGINE_LOG_PHYSICS_WARN(...)    ZENGINE_LOG(PHYSICS, WARN, __VA_ARGS__)
#define ZENGINE_LOG_PHYSICS_ERR(...)     ZENGINE_LOG(PHYSICS, ERR, __VA_ARGS__)

#define ZENGINE_LOG_AUDIO_INFO(...)      ZENGINE_LOG(AUDIO, INFO, __VA_ARGS__)
#define ZENGINE_LOG_AUDIO_WARN(...)      ZENGINE_LOG(AUDIO, WARN, __VA_ARGS__)
#define ZENGINE_LOG_AUDIO_ERR(...)       ZENGINE_LOG(AUDIO, ERR, __VA_ARGS__)

#define ZENGINE_LOG_NETWORK_INFO(...)    ZENGINE_LOG(NETWORK, INFO, __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_WARN(...)    ZENGINE_LOG(NETWORK, WARN, __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_ERR(...)     ZENGINE_LOG(NETWORK, ERR, __VA_ARGS__)

#define ZENGINE_LOG_VFS_INFO(...)        ZENGINE_LOG(VFS, INFO, __VA_ARGS__)
#define ZENGINE_LOG_VFS_WARN(...)        ZENGINE_LOG(VFS, WARN, __VA_ARGS__)
#define ZENGINE_LOG_VFS_ERR(...)         ZENGINE_LOG(VFS, ERR, __VA_ARGS__)

#define ZENGINE_LOG_ASSET_INFO(...)      ZENGINE_LOG(ASSET, INFO, __VA_ARGS__)
#define ZENGINE_LOG_ASSET_WARN(...)      ZENGINE_LOG(ASSET, WARN, __VA_ARGS__)

#define ZENGINE_LOG_UI_TRACE(...)        ZENGINE_LOG(UI, TRACE, __VA_ARGS__)
#define ZENGINE_LOG_UI_WARN(...)         ZENGINE_LOG(UI, WARN, __VA_ARGS__)
#define ZENGINE_LOG_UI_ERR(...)          ZENGINE_LOG(UI, ERR, __VA_ARGS__)

#define ZENGINE_LOG_GAME_TRACE(...)      ZENGINE_LOG(GAME, TRACE, __VA_ARGS__)
#define ZENGINE_LOG_GAME_INFO(...)       ZENGINE_LOG(GAME, INFO, __VA_ARGS__)
#define ZENGINE_LOG_GAME_WARN(...)       ZENGINE_LOG(GAME, WARN, __VA_ARGS__)
#define ZENGINE_LOG_GAME_ERR(...)        ZENGINE_LOG(GAME, ERR, __VA_ARGS__)

// Backwards-compatible aliases — route to ENGINE channel, no source changes required.
// Note: ZENGINE_CORE_INFO calls are silenced in Release builds (ENGINE minimum = WARN+).
// See Section 12 for the migration audit step before upgrading.
#define ZENGINE_CORE_INFO(...)           ZENGINE_LOG(ENGINE, INFO, __VA_ARGS__)
#define ZENGINE_CORE_TRACE(...)          ZENGINE_LOG(ENGINE, TRACE, __VA_ARGS__)
#define ZENGINE_CORE_WARN(...)           ZENGINE_LOG(ENGINE, WARN, __VA_ARGS__)
#define ZENGINE_CORE_ERROR(...)          ZENGINE_LOG(ENGINE, ERR, __VA_ARGS__)
#define ZENGINE_CORE_CRITICAL(...)       ZENGINE_LOG(ENGINE, CRITICAL, __VA_ARGS__)